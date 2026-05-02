#include "resort/services/PokemonImportService.hpp"

#include "resort/domain/Ids.hpp"
#include "resort/integration/Gen12DvBytes.hpp"

#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>

namespace pr::resort {

namespace {

constexpr const char* kDefaultJsonPayload = "{\"schema_version\":1}";
constexpr const char* kTempTransferLog = "[TEMP_TRANSFER_LOG_DELETE]";

thread_local std::mt19937 kGen12DvRng{std::random_device{}()};

std::string escapeJson(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::string createdDiffJson(const ImportedPokemon& imported, const ImportContext& context) {
    std::ostringstream out;
    out << "{"
        << "\"event\":\"created_from_import\","
        << "\"source_game\":" << imported.source_game << ","
        << "\"format\":\"" << escapeJson(imported.format_name) << "\","
        << "\"profile_id\":\"" << escapeJson(context.profile_id) << "\""
        << "}";
    return out.str();
}

std::string snapshotNotesJson(const PokemonMatchResult& match) {
    std::ostringstream out;
    out << "{"
        << "\"schema_version\":1,"
        << "\"match_reason\":\"" << escapeJson(match.reason) << "\","
        << "\"matched\":" << (match.matched ? "true" : "false")
        << "}";
    return out.str();
}

std::string optionalU16ForLog(const std::optional<std::uint16_t>& value) {
    return value ? std::to_string(*value) : std::string("none");
}

std::string movesForLog(const PokemonHot& hot) {
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < hot.move_ids.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << (hot.move_ids[i] ? std::to_string(*hot.move_ids[i]) : std::string("none"));
    }
    out << "]";
    return out.str();
}

ResortPokemon canonicalFromImport(
    const ImportedPokemon& imported,
    const std::string& pkrid,
    long long now) {
    ResortPokemon pokemon;
    pokemon.id.pkrid = pkrid;
    pokemon.id.origin_fingerprint = fingerprintForFirstSeenPokemon(
        imported.source_game,
        imported.format_name,
        imported.hot.ot_name,
        imported.hot.species_id,
        imported.raw_hash_sha256);
    pokemon.hot = imported.hot;
    pokemon.hot.origin_game = imported.hot.origin_game == 0
        ? imported.source_game
        : imported.hot.origin_game;
    pokemon.hot.lineage_root_species = imported.hot.lineage_root_species == 0
        ? imported.hot.species_id
        : imported.hot.lineage_root_species;
    if (!pokemon.hot.pid && imported.identity.pid) {
        pokemon.hot.pid = imported.identity.pid;
    }
    if (!pokemon.hot.encryption_constant && imported.identity.encryption_constant) {
        pokemon.hot.encryption_constant = imported.identity.encryption_constant;
    }
    if (!pokemon.hot.home_tracker && imported.identity.home_tracker) {
        pokemon.hot.home_tracker = imported.identity.home_tracker;
    }
    if (!pokemon.hot.tid16 && imported.identity.tid16) {
        pokemon.hot.tid16 = imported.identity.tid16;
    }
    if (!pokemon.hot.sid16 && imported.identity.sid16) {
        pokemon.hot.sid16 = imported.identity.sid16;
    }
    if (pokemon.hot.ot_name.empty() && !imported.identity.ot_name.empty()) {
        pokemon.hot.ot_name = imported.identity.ot_name;
    }
    if (pokemon.hot.lineage_root_species == 0 && imported.identity.lineage_root_species != 0) {
        pokemon.hot.lineage_root_species = imported.identity.lineage_root_species;
    }
    pokemon.warm.json = imported.warm_json.empty() ? kDefaultJsonPayload : imported.warm_json;
    pokemon.cold.suspended_json = imported.suspended_json.empty() ? kDefaultJsonPayload : imported.suspended_json;
    pokemon.created_at_unix = now;
    pokemon.updated_at_unix = now;
    return pokemon;
}

} // namespace

PokemonImportService::PokemonImportService(
    SqliteConnection& connection,
    PokemonRepository& pokemon,
    BoxRepository& boxes,
    SnapshotRepository& snapshots,
    HistoryRepository& history,
    PokemonMatcher& matcher,
    PokemonMergeService& merge,
    MirrorSessionService& mirror_sessions)
    : connection_(connection),
      pokemon_(pokemon),
      boxes_(boxes),
      snapshots_(snapshots),
      history_(history),
      matcher_(matcher),
      merge_(merge),
      mirror_sessions_(mirror_sessions) {}

ImportResult PokemonImportService::importParsedPokemon(
    const ImportedPokemon& imported_in,
    const ImportContext& context) {
    ImportedPokemon imported = imported_in;
    ImportResult result;
    std::cerr << kTempTransferLog
              << " Backend import start source_game=" << imported.source_game
              << " format=" << imported.format_name
              << " species=" << imported.hot.species_id
              << " level=" << static_cast<int>(imported.hot.level)
              << " exp=" << imported.hot.exp
              << " held_item=" << optionalU16ForLog(imported.hot.held_item_id)
              << " dv16=" << optionalU16ForLog(imported.hot.dv16)
              << " moves=" << movesForLog(imported.hot)
              << " hp=" << imported.hot.hp_current << "/" << imported.hot.hp_max
              << " status_flags=" << imported.hot.status_flags
              << " lineage_root=" << imported.hot.lineage_root_species
              << " raw_bytes=" << imported.raw_bytes.size()
              << " hash=" << imported.raw_hash_sha256
              << " target_location="
              << (context.target_location ? (std::to_string(context.target_location->box_id) + ":" +
                                             std::to_string(context.target_location->slot_index))
                                          : std::string("none"))
              << '\n';
    if (imported.raw_bytes.empty()) {
        result.error = "ImportedPokemon.raw_bytes is required for no-loss snapshot preservation";
        return result;
    }
    if (imported.raw_hash_sha256.empty()) {
        result.error = "ImportedPokemon.raw_hash_sha256 is required; bridge/native adapter must provide a real hash";
        return result;
    }
    if (imported.format_name.empty()) {
        result.error = "ImportedPokemon.format_name is required";
        return result;
    }

    const long long now = unixNow();
    const std::string snapshot_id = generateId("snap");
    bool created_canonical = false;
    bool merged_canonical = false;
    std::string final_pkrid;
    std::string final_match_reason;

    try {
        SqliteTransaction tx(connection_);
        const PokemonMatchResult match = matcher_.findBestMatch(imported);
        const std::string pkrid = match.matched ? match.pkrid : generateId("pkr");
        std::optional<ResortPokemon> existing_canonical;
        if (match.matched) {
            existing_canonical = pokemon_.findById(pkrid);
        }
        resolveGen12ZeroDvForResortImport(existing_canonical, imported, kGen12DvRng);
        final_pkrid = pkrid;
        final_match_reason = match.reason;
        std::cerr << kTempTransferLog
                  << " Backend import match pkrid=" << pkrid
                  << " matched=" << (match.matched ? "true" : "false")
                  << " reason=" << match.reason
                  << " mirror_session_id=" << match.mirror_session_id << '\n';

        PokemonSnapshot snapshot;
        snapshot.snapshot_id = snapshot_id;
        snapshot.pkrid = pkrid;
        snapshot.kind = SnapshotKind::ImportedRaw;
        snapshot.format_name = imported.format_name;
        snapshot.game_id = imported.source_game;
        snapshot.captured_at_unix = now;
        snapshot.raw_bytes = imported.raw_bytes;
        snapshot.raw_hash_sha256 = imported.raw_hash_sha256;
        snapshot.parsed_json = imported.warm_json.empty() ? kDefaultJsonPayload : imported.warm_json;
        snapshot.notes_json = snapshotNotesJson(match);

        // The snapshot row is written first. Its FK is deferred until commit so canonical insert
        // can follow while preserving the no-loss ordering inside the transaction.
        snapshots_.insert(snapshot);
        std::cerr << kTempTransferLog
                  << " Backend import snapshot inserted snapshot_id=" << snapshot.snapshot_id
                  << " pkrid=" << pkrid
                  << " kind=ImportedRaw\n";

        if (match.matched) {
            auto canonical = pokemon_.findById(pkrid);
            if (!canonical) {
                throw std::runtime_error("Matched Pokemon no longer exists: " + pkrid);
            }
            const PokemonHot before_hot = canonical->hot;
            const PokemonMergeResult merged = merge_.mergeImported(*canonical, imported, now);
            pokemon_.updateAfterMerge(*canonical);

            PokemonHistoryEvent event;
            event.event_id = generateId("hist");
            event.pkrid = pkrid;
            event.event_type = HistoryEventType::Merged;
            event.timestamp_unix = now;
            event.source_snapshot_id = snapshot_id;
            event.diff_json = merged.diff_json;
            history_.insert(event);
            if (!match.mirror_session_id.empty()) {
                mirror_sessions_.closeReturned(match.mirror_session_id, now);
                std::cerr << kTempTransferLog
                          << " Backend import closed returned mirror_session_id="
                          << match.mirror_session_id << '\n';
            }
            merged_canonical = true;
            std::cerr << kTempTransferLog
                      << " Backend import merged canonical pkrid=" << pkrid
                      << " level=" << static_cast<int>(canonical->hot.level)
                      << " exp=" << canonical->hot.exp << '\n';
            std::cerr << kTempTransferLog
                      << " Backend import merge hot fields pkrid=" << pkrid
                      << " before_species=" << before_hot.species_id
                      << " after_species=" << canonical->hot.species_id
                      << " before_level=" << static_cast<int>(before_hot.level)
                      << " after_level=" << static_cast<int>(canonical->hot.level)
                      << " before_exp=" << before_hot.exp
                      << " after_exp=" << canonical->hot.exp
                      << " before_held_item=" << optionalU16ForLog(before_hot.held_item_id)
                      << " after_held_item=" << optionalU16ForLog(canonical->hot.held_item_id)
                      << " before_dv16=" << optionalU16ForLog(before_hot.dv16)
                      << " after_dv16=" << optionalU16ForLog(canonical->hot.dv16)
                      << " before_moves=" << movesForLog(before_hot)
                      << " after_moves=" << movesForLog(canonical->hot)
                      << " before_hp=" << before_hot.hp_current << "/" << before_hot.hp_max
                      << " after_hp=" << canonical->hot.hp_current << "/" << canonical->hot.hp_max
                      << " before_status_flags=" << before_hot.status_flags
                      << " after_status_flags=" << canonical->hot.status_flags
                      << " before_lineage_root=" << before_hot.lineage_root_species
                      << " after_lineage_root=" << canonical->hot.lineage_root_species << '\n';
        } else {
            ResortPokemon canonical = canonicalFromImport(imported, pkrid, now);
            pokemon_.insert(canonical);

            PokemonHistoryEvent event;
            event.event_id = generateId("hist");
            event.pkrid = pkrid;
            event.event_type = HistoryEventType::Created;
            event.timestamp_unix = now;
            event.source_snapshot_id = snapshot_id;
            event.diff_json = createdDiffJson(imported, context);
            history_.insert(event);
            created_canonical = true;
            std::cerr << kTempTransferLog
                      << " Backend import created canonical pkrid=" << pkrid << '\n';
        }

        if (context.target_location) {
            boxes_.placePokemon(*context.target_location, pkrid, context.placement_policy);
            std::cerr << kTempTransferLog
                      << " Backend import placed pkrid=" << pkrid
                      << " box=" << context.target_location->box_id
                      << " slot=" << context.target_location->slot_index << '\n';
            PokemonHistoryEvent moved;
            moved.event_id = generateId("hist");
            moved.pkrid = pkrid;
            moved.event_type = HistoryEventType::MovedBox;
            moved.timestamp_unix = now;
            moved.diff_json = "{\"event\":\"placed_on_import\"}";
            history_.insert(moved);
        }
        tx.commit();
        std::cerr << kTempTransferLog
                  << " Backend import commit success pkrid=" << final_pkrid
                  << " snapshot_id=" << snapshot_id
                  << " created=" << (created_canonical ? "true" : "false")
                  << " merged=" << (merged_canonical ? "true" : "false") << '\n';
    } catch (const std::exception& ex) {
        std::cerr << kTempTransferLog
                  << " Backend import rollback error=" << ex.what() << '\n';
        result.error = ex.what();
        return result;
    }

    result.success = true;
    result.created = created_canonical;
    result.merged = merged_canonical;
    result.pkrid = final_pkrid;
    result.match_reason = final_match_reason;
    result.snapshot_id = snapshot_id;
    return result;
}

} // namespace pr::resort
