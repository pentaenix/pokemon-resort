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

std::string canonicalCheckpointNotes(const std::string& source_snapshot_id, bool mirror_return) {
    return std::string("{\"schema_version\":1,\"from_snapshot_id\":\"") + source_snapshot_id +
           "\",\"reason\":\"" + (mirror_return ? "mirror_return_checkpoint" : "import_checkpoint") + "\"}";
}

void insertCanonicalCheckpointSnapshot(
    SnapshotRepository& snapshots,
    const std::string& pkrid,
    const ImportedPokemon& imported,
    const std::string& source_snapshot_id,
    long long now,
    bool mirror_return) {
    if (imported.raw_bytes.empty()) {
        return;
    }
    PokemonSnapshot cp;
    cp.snapshot_id = generateId("snap");
    cp.pkrid = pkrid;
    cp.kind = SnapshotKind::CanonicalCheckpoint;
    cp.format_name = imported.format_name;
    cp.game_id = imported.source_game;
    cp.captured_at_unix = now;
    cp.raw_bytes = imported.raw_bytes;
    cp.raw_hash_sha256 = imported.raw_hash_sha256;
    cp.parsed_json = imported.warm_json.empty() ? kDefaultJsonPayload : imported.warm_json;
    cp.notes_json = canonicalCheckpointNotes(source_snapshot_id, mirror_return);
    snapshots.insert(cp);
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
        const bool mirror_return = !match.mirror_session_id.empty();
        std::cerr << kTempTransferLog
                  << " import in fmt=" << imported.format_name
                  << " species=" << imported.hot.species_id
                  << " lvl=" << static_cast<int>(imported.hot.level)
                  << " exp=" << imported.hot.exp
                  << " matched=" << (match.matched ? "yes" : "no")
                  << " reason=" << match.reason
                  << (mirror_return ? " mirror_return" : "")
                  << '\n';

        PokemonSnapshot snapshot;
        snapshot.snapshot_id = snapshot_id;
        snapshot.pkrid = pkrid;
        snapshot.kind = match.mirror_session_id.empty() ? SnapshotKind::ImportedRaw
                                                         : SnapshotKind::ReturnRaw;
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

        insertCanonicalCheckpointSnapshot(
            snapshots_,
            pkrid,
            imported,
            snapshot_id,
            now,
            !match.mirror_session_id.empty());

        if (match.matched) {
            auto canonical = pokemon_.findById(pkrid);
            if (!canonical) {
                throw std::runtime_error("Matched Pokemon no longer exists: " + pkrid);
            }
            const ImportMergeKind merge_kind = mirror_return ? ImportMergeKind::MirrorReturnGameplaySync
                                                             : ImportMergeKind::FullReplaceFromImport;
            const PokemonMergeResult merged =
                merge_.mergeImported(*canonical, imported, now, merge_kind);
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
            }
            merged_canonical = true;
            std::cerr << kTempTransferLog
                      << " import merged pkrid=" << pkrid
                      << " changed=" << (merged.changed ? "yes" : "no")
                      << " " << merged.diff_json << '\n';
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
            std::cerr << kTempTransferLog << " import created pkrid=" << pkrid << '\n';
        }

        if (context.target_location) {
            boxes_.placePokemon(*context.target_location, pkrid, context.placement_policy);
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
                  << " import done pkrid=" << final_pkrid
                  << " created=" << (created_canonical ? "yes" : "no")
                  << " merged=" << (merged_canonical ? "yes" : "no") << '\n';
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
