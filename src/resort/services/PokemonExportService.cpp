#include "resort/services/PokemonExportService.hpp"

#include "core/crypto/Sha256.hpp"
#include "resort/domain/Ids.hpp"
#include "resort/domain/PkmFormat.hpp"
#include "resort/integration/Gen12DvBytes.hpp"
#include "resort/services/MirrorProjectionService.hpp"

#include <filesystem>
#include <iostream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace pr::resort {

namespace {

constexpr const char* kTempTransferLog = "[TEMP_TRANSFER_LOG_DELETE]";

thread_local std::mt19937 kGen12ExportDvRng{std::random_device{}()};

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

unsigned long long fnv1a64(const std::vector<unsigned char>& bytes) {
    unsigned long long hash = 14695981039346656037ull;
    for (const unsigned char ch : bytes) {
        hash ^= ch;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string stableProjectionHash(const std::vector<unsigned char>& bytes) {
    const unsigned long long a = fnv1a64(bytes);
    std::vector<unsigned char> salted = bytes;
    salted.push_back(0x70);
    salted.push_back(0x72);
    const unsigned long long b = fnv1a64(salted);
    std::ostringstream out;
    out << std::hex << std::setfill('0')
        << std::setw(16) << a
        << std::setw(16) << b
        << std::setw(16) << (a ^ 0xa5a5a5a5a5a5a5a5ull)
        << std::setw(16) << (b ^ 0x5a5a5a5a5a5a5a5aull);
    return out.str();
}

std::uint16_t beaconTidForPkrid(const std::string& pkrid) {
    unsigned int seed = 0;
    for (const unsigned char ch : pkrid) {
        seed = (seed * 131u) + ch;
    }
    return static_cast<std::uint16_t>(50000u + (seed % 10000u));
}

std::string projectionJson(
    const ResortPokemon& pokemon,
    const ExportContext& context,
    std::optional<std::uint16_t> beacon_tid,
    const std::string& beacon_ot) {
    const PokemonHot& h = pokemon.hot;
    std::ostringstream out;
    out << "{"
        << "\"projection_schema\":1,"
        << "\"projection_kind\":\"resort_backend_projection\","
        << "\"pkrid\":\"" << escapeJson(pokemon.id.pkrid) << "\","
        << "\"target_game\":" << context.target_game << ","
        << "\"target_format\":\"" << escapeJson(context.target_format_name) << "\","
        << "\"hot\":{"
        << "\"species_id\":" << h.species_id << ","
        << "\"form_id\":" << h.form_id << ","
        << "\"nickname\":\"" << escapeJson(h.nickname) << "\","
        << "\"level\":" << static_cast<int>(h.level) << ","
        << "\"exp\":" << h.exp << ","
        << "\"ot_name\":\"" << escapeJson(h.ot_name) << "\","
        << "\"tid16\":" << (h.tid16 ? std::to_string(*h.tid16) : "null") << ","
        << "\"sid16\":" << (h.sid16 ? std::to_string(*h.sid16) : "null")
        << "},"
        << "\"managed_mirror\":" << (context.managed_mirror ? "true" : "false") << ","
        << "\"beacon_tid16\":" << (beacon_tid ? std::to_string(*beacon_tid) : "null") << ","
        << "\"beacon_ot_name\":\"" << escapeJson(beacon_ot) << "\","
        << "\"note\":\"Synthetic backend projection; bridge PKM conversion is intentionally separate.\""
        << "}";
    return out.str();
}

std::string exportDiffJson(
    const ExportContext& context,
    const std::string& mirror_session_id) {
    std::ostringstream out;
    out << "{"
        << "\"event\":\"export_projection\","
        << "\"target_game\":" << context.target_game << ","
        << "\"target_format\":\"" << escapeJson(context.target_format_name) << "\","
        << "\"mirror_session_id\":\"" << escapeJson(mirror_session_id) << "\""
        << "}";
    return out.str();
}

} // namespace

PokemonExportService::PokemonExportService(
    SqliteConnection& connection,
    PokemonRepository& pokemon,
    BoxRepository& boxes,
    SnapshotRepository& snapshots,
    HistoryRepository& history,
    MirrorSessionService& mirror_sessions,
    MirrorProjectionService& projection,
    PidTransportRegistryRepository& pid_transport)
    : connection_(connection),
      pokemon_(pokemon),
      boxes_(boxes),
      snapshots_(snapshots),
      history_(history),
      mirror_sessions_(mirror_sessions),
      projection_(projection),
      pid_transport_(pid_transport) {}

ExportResult PokemonExportService::exportPokemon(
    const std::string& pkrid,
    const ExportContext& context) {
    ExportResult result;
    std::cerr << kTempTransferLog
              << " Backend export start pkrid=" << pkrid
              << " target_game=" << context.target_game
              << " target_format=" << context.target_format_name
              << " managed_mirror=" << (context.managed_mirror ? "true" : "false") << '\n';
    auto pokemon = pokemon_.findById(pkrid);
    if (!pokemon) {
        result.error = "Pokemon not found: " + pkrid;
        return result;
    }
    if (context.managed_mirror) {
        if (auto active = mirror_sessions_.getActiveForPokemon(pkrid)) {
            if (boxes_.findPokemonLocation("default", pkrid).has_value()) {
                mirror_sessions_.closeReturned(active->mirror_session_id, unixNow());
                std::cerr << kTempTransferLog
                          << " Backend export retired stale active mirror for boxed Pokemon pkrid=" << pkrid
                          << " mirror_session_id=" << active->mirror_session_id << '\n';
            } else {
                result.error = "Pokemon already has an active mirror: " + pkrid;
                std::cerr << kTempTransferLog
                          << " Backend export rejected; active mirror already exists pkrid=" << pkrid << '\n';
                return result;
            }
        }
    }

    try {
        const long long now = unixNow();
        if (pokemon->hot.pid) {
            pokemon_.ensureOriginalPidIfUnset(pkrid, *pokemon->hot.pid);
            if (!pokemon->original_pid) {
                pokemon->original_pid = *pokemon->hot.pid;
            }
        }
        const std::optional<std::uint16_t> beacon_tid = context.use_gen12_beacon
            ? std::optional<std::uint16_t>(beaconTidForPkrid(pkrid))
            : std::nullopt;
        const std::string beacon_ot = context.use_gen12_beacon ? "RESORT" : pokemon->hot.ot_name;
        std::string projection;
        std::vector<unsigned char> raw;
        std::string raw_hash;
        const std::string inferred_target_format = pkmStorageFormatNameForGameId(context.target_game);
        const std::string target_format_name = context.target_format_name.empty()
                                                   ? inferred_target_format
                                                   : context.target_format_name;
        std::string format_name = target_format_name.empty() ? std::string("projection-json")
                                                             : target_format_name;
        bool canonical_dv_repaired = false;
        std::optional<std::uint32_t> mirror_transport_pid;
        int source_constraint_gen = 0;
        // Latest snapshot first (see prepareLatestRawSnapshotForGameWrite): filtering by target_game +
        // target_format returns an older same-format import instead of a newer ReturnRaw from another gen.
        std::optional<PokemonSnapshot> compatible_raw = snapshots_.findLatestRawForPokemon(pkrid, std::nullopt, {});

        bool have_import_grade_raw = false;

        if (compatible_raw && !compatible_raw->raw_bytes.empty()) {
            const bool can_bridge_project =
                !target_format_name.empty() &&
                !context.bridge_project_root.empty() &&
                context.bridge_argv0 != nullptr;
            const bool format_matches_target =
                target_format_name.empty() ||
                pkmFormatNamesEqual(compatible_raw->format_name, target_format_name);
            if (!can_bridge_project && format_matches_target) {
                have_import_grade_raw = true;
                format_name = compatible_raw->format_name;
                raw = compatible_raw->raw_bytes;
                raw_hash = compatible_raw->raw_hash_sha256;
                std::cerr << kTempTransferLog
                          << " Backend export using compatible raw snapshot pkrid=" << pkrid
                          << " source_snapshot_id=" << compatible_raw->snapshot_id
                          << " format=" << format_name
                          << " raw_bytes=" << raw.size()
                          << " hash=" << raw_hash << '\n';
                if (isGen12StorageFormat(format_name)) {
                    if (const auto from_raw = readPk12Dv16FromRaw(raw, format_name)) {
                        if (*from_raw == 0 || !pokemon->hot.dv16 || *pokemon->hot.dv16 == 0) {
                            std::optional<std::uint16_t> use_dv;
                            if (pokemon->hot.dv16 && *pokemon->hot.dv16 != 0) {
                                use_dv = *pokemon->hot.dv16;
                            } else if (*from_raw != 0) {
                                use_dv = *from_raw;
                            } else {
                                use_dv = randomNonZeroGen12Dv16(kGen12ExportDvRng);
                            }
                            if (*from_raw == 0) {
                                if (!patchPk12DvBytes(raw, format_name, *use_dv)) {
                                    throw std::runtime_error("failed to patch Gen 1/2 DV bytes during export");
                                }
                                raw_hash = pr::sha256HexLowercase(raw);
                                std::cerr << kTempTransferLog
                                          << " Backend export repaired Gen12 zero DV in raw "
                                          << formatGen12Dv16ForLog(*use_dv)
                                          << " source_snapshot_id=" << compatible_raw->snapshot_id
                                          << " pkrid=" << pkrid << '\n';
                            }
                            if (!pokemon->hot.dv16 || *pokemon->hot.dv16 == 0) {
                                pokemon->hot.dv16 = *use_dv;
                                pokemon->revision += 1;
                                pokemon->updated_at_unix = now;
                                canonical_dv_repaired = true;
                            }
                        }
                        std::cerr << kTempTransferLog << " Backend export Gen12 DV in exported raw "
                                  << formatGen12Dv16ForLog(*readPk12Dv16FromRaw(raw, format_name))
                                  << " hot.dv16="
                                  << (pokemon->hot.dv16 ? std::to_string(*pokemon->hot.dv16)
                                                        : std::string("null"))
                                  << " pkrid=" << pkrid << '\n';
                    } else {
                        std::cerr << kTempTransferLog
                                  << " Backend export Gen12 DV unreadable in raw (too small?) "
                                  << "format=" << format_name << " pkrid=" << pkrid << '\n';
                    }
                }
                std::ostringstream out;
                out << "{"
                    << "\"projection_schema\":1,"
                    << "\"projection_kind\":\"resort_same_game_raw_snapshot\","
                    << "\"pkrid\":\"" << escapeJson(pokemon->id.pkrid) << "\","
                    << "\"source_snapshot_id\":\"" << escapeJson(compatible_raw->snapshot_id) << "\","
                    << "\"target_game\":" << context.target_game << ","
                    << "\"target_format\":\"" << escapeJson(format_name) << "\","
                    << "\"lossy\":false"
                    << "}";
                projection = out.str();
                mirror_transport_pid = pokemon->hot.pid;
                source_constraint_gen = constraintGenerationFromStorageFormat(format_name);
            } else if (can_bridge_project) {
                MirrorBridgeProjectInput bridge_in;
                bridge_in.pkrid = pkrid;
                bridge_in.target_game = context.target_game;
                bridge_in.target_format_name = target_format_name;
                const std::filesystem::path req_path =
                    std::filesystem::temp_directory_path() / ("pr_export_proj_" + pkrid + ".json");
                const MirrorProjectDecodedResult decoded = projection_.projectLatestSnapshotToTargetDecoded(
                    bridge_in,
                    context.bridge_project_root,
                    context.bridge_argv0,
                    req_path);
                if (!decoded.success) {
                    result.error =
                        decoded.error.empty() ? std::string("bridge cross-gen projection failed") : decoded.error;
                    return result;
                }
                have_import_grade_raw = true;
                format_name = decoded.target_format_name;
                raw = std::move(decoded.raw_bytes);
                raw_hash = decoded.raw_hash_sha256;
                mirror_transport_pid = decoded.bridge_target_pid;
                source_constraint_gen = constraintGenerationFromStorageFormat(decoded.source_format_name);
                std::cerr << kTempTransferLog
                          << " Backend export using bridge projection pkrid=" << pkrid
                          << " source_snapshot_id=" << decoded.source_snapshot_id
                          << " source_format=" << decoded.source_format_name
                          << " format=" << format_name
                          << " raw_bytes=" << raw.size()
                          << " hash=" << raw_hash << '\n';
                if (isGen12StorageFormat(format_name)) {
                    if (const auto from_raw = readPk12Dv16FromRaw(raw, format_name)) {
                        if (*from_raw == 0 || !pokemon->hot.dv16 || *pokemon->hot.dv16 == 0) {
                            std::optional<std::uint16_t> use_dv;
                            if (pokemon->hot.dv16 && *pokemon->hot.dv16 != 0) {
                                use_dv = *pokemon->hot.dv16;
                            } else if (*from_raw != 0) {
                                use_dv = *from_raw;
                            } else {
                                use_dv = randomNonZeroGen12Dv16(kGen12ExportDvRng);
                            }
                            if (*from_raw == 0) {
                                if (!patchPk12DvBytes(raw, format_name, *use_dv)) {
                                    throw std::runtime_error("failed to patch Gen 1/2 DV bytes during export");
                                }
                                raw_hash = pr::sha256HexLowercase(raw);
                                std::cerr << kTempTransferLog
                                          << " Backend export repaired Gen12 zero DV after cross-gen raw "
                                          << formatGen12Dv16ForLog(*use_dv)
                                          << " source_snapshot_id=" << compatible_raw->snapshot_id
                                          << " pkrid=" << pkrid << '\n';
                            }
                            if (!pokemon->hot.dv16 || *pokemon->hot.dv16 == 0) {
                                pokemon->hot.dv16 = *use_dv;
                                pokemon->revision += 1;
                                pokemon->updated_at_unix = now;
                                canonical_dv_repaired = true;
                            }
                        }
                        std::cerr << kTempTransferLog << " Backend export Gen12 DV in exported raw "
                                  << formatGen12Dv16ForLog(*readPk12Dv16FromRaw(raw, format_name))
                                  << " hot.dv16="
                                  << (pokemon->hot.dv16 ? std::to_string(*pokemon->hot.dv16)
                                                        : std::string("null"))
                                  << " pkrid=" << pkrid << '\n';
                    } else {
                        std::cerr << kTempTransferLog
                                  << " Backend export Gen12 DV unreadable after cross-gen (too small?) "
                                  << "format=" << format_name << " pkrid=" << pkrid << '\n';
                    }
                }
                std::ostringstream out;
                out << "{"
                    << "\"projection_schema\":1,"
                    << "\"projection_kind\":\"resort_bridge_projection\","
                    << "\"pkrid\":\"" << escapeJson(pokemon->id.pkrid) << "\","
                    << "\"source_snapshot_id\":\"" << escapeJson(decoded.source_snapshot_id) << "\","
                    << "\"source_format\":\"" << escapeJson(decoded.source_format_name) << "\","
                    << "\"target_game\":" << context.target_game << ","
                    << "\"target_format\":\"" << escapeJson(format_name) << "\","
                    << "\"lossy\":true"
                    << "}";
                projection = out.str();
            }
        }

        if (!have_import_grade_raw) {
            projection = projectionJson(*pokemon, context, beacon_tid, beacon_ot);
            raw.assign(projection.begin(), projection.end());
            raw_hash = stableProjectionHash(raw);
            std::cerr << kTempTransferLog
                      << " Backend export using synthetic projection pkrid=" << pkrid
                      << " format=" << format_name
                      << " raw_bytes=" << raw.size()
                      << " hash=" << raw_hash << '\n';
        }

        if (source_constraint_gen == 0) {
            const std::string nm = pkmStorageFormatNameForGameId(pokemon->hot.origin_game);
            source_constraint_gen =
                constraintGenerationFromStorageFormat(nm.empty() ? std::string_view("pk9") : std::string_view(nm));
        }

        SqliteTransaction tx(connection_);
        if (canonical_dv_repaired) {
            pokemon_.updateAfterMerge(*pokemon);
            std::cerr << kTempTransferLog << " Backend export persisted Gen12 DV repair to canonical "
                      << formatGen12Dv16ForLog(*pokemon->hot.dv16)
                      << " pkrid=" << pkrid << '\n';
        }

        PokemonSnapshot snapshot;
        snapshot.snapshot_id = generateId("snap");
        snapshot.pkrid = pkrid;
        snapshot.kind = SnapshotKind::ExportProjection;
        snapshot.format_name = format_name;
        snapshot.game_id = context.target_game;
        snapshot.captured_at_unix = now;
        snapshot.raw_bytes = raw;
        snapshot.raw_hash_sha256 = raw_hash;
        snapshot.parsed_json = projection;
        snapshot.notes_json = "{\"schema_version\":1,\"synthetic_projection\":true}";
        snapshots_.insert(snapshot);
        std::cerr << kTempTransferLog
                  << " Backend export snapshot inserted snapshot_id=" << snapshot.snapshot_id
                  << " pkrid=" << pkrid
                  << " kind=ExportProjection\n";

        MirrorOpenContext mirror_context;
        mirror_context.beacon_tid16 = beacon_tid;
        mirror_context.beacon_ot_name = context.use_gen12_beacon
            ? std::optional<std::string>(beacon_ot)
            : std::nullopt;
        mirror_context.mirror_canonical_pid =
            pokemon->original_pid ? pokemon->original_pid : pokemon->hot.pid;
        mirror_context.transport_pid = mirror_transport_pid;
        mirror_context.projection_metadata_json = projection;
        MirrorSession mirror = mirror_sessions_.openMirrorSession(pkrid, context.target_game, mirror_context);
        std::cerr << kTempTransferLog
                  << " Backend export mirror opened mirror_session_id=" << mirror.mirror_session_id
                  << " pkrid=" << pkrid << '\n';

        const std::optional<std::uint32_t> canonical_personality =
            pokemon->original_pid ? pokemon->original_pid : pokemon->hot.pid;
        const int target_constraint_gen = constraintGenerationFromStorageFormat(format_name);
        if (mirror.transport_pid && canonical_personality && target_constraint_gen > 0 &&
            *mirror.transport_pid != *canonical_personality) {
            pid_transport_.insertActiveMapping(
                *mirror.transport_pid,
                pkrid,
                *canonical_personality,
                source_constraint_gen,
                target_constraint_gen,
                now,
                mirror.mirror_session_id);
            pokemon_.appendPidHistoryEntry(
                pkrid,
                *mirror.transport_pid,
                source_constraint_gen,
                target_constraint_gen,
                now);
        }
        if (context.managed_mirror) {
            boxes_.removePokemon("default", pkrid);
            std::cerr << kTempTransferLog
                      << " Backend export unplaced pkrid=" << pkrid
                      << " from Resort boxes (off-Pokemon active mirror)\n";
        }

        PokemonHistoryEvent event;
        event.event_id = generateId("hist");
        event.pkrid = pkrid;
        event.event_type = HistoryEventType::Exported;
        event.timestamp_unix = now;
        event.source_snapshot_id = snapshot.snapshot_id;
        event.mirror_session_id = mirror.mirror_session_id;
        event.diff_json = exportDiffJson(context, mirror.mirror_session_id);
        history_.insert(event);

        tx.commit();
        std::cerr << kTempTransferLog
                  << " Backend export commit success pkrid=" << pkrid
                  << " mirror_session_id=" << mirror.mirror_session_id
                  << " snapshot_id=" << snapshot.snapshot_id << '\n';

        result.success = true;
        result.pkrid = pkrid;
        result.snapshot_id = snapshot.snapshot_id;
        result.mirror_session_id = mirror.mirror_session_id;
        result.format_name = format_name;
        result.raw_payload = raw;
        result.raw_hash = raw_hash;
        result.transport_pid = mirror_transport_pid;
        return result;
    } catch (const std::exception& ex) {
        std::cerr << kTempTransferLog
                  << " Backend export rollback error=" << ex.what()
                  << " pkrid=" << pkrid << '\n';
        result.error = ex.what();
        return result;
    }
}

ExportResult PokemonExportService::commitPreparedMirrorExport(
    const std::string& pkrid,
    const ExportContext& context,
    const std::vector<unsigned char>& raw_payload,
    const std::string& raw_hash,
    const std::string& format_name,
    std::optional<std::uint32_t> transport_pid) {
    ExportResult result;
    if (raw_payload.empty() || raw_hash.empty() || format_name.empty()) {
        result.error = "Prepared mirror export requires raw payload, hash, and format";
        return result;
    }

    auto pokemon = pokemon_.findById(pkrid);
    if (!pokemon) {
        result.error = "Pokemon not found: " + pkrid;
        return result;
    }
    if (context.managed_mirror) {
        if (auto active = mirror_sessions_.getActiveForPokemon(pkrid)) {
            if (boxes_.findPokemonLocation("default", pkrid).has_value()) {
                mirror_sessions_.closeReturned(active->mirror_session_id, unixNow());
            } else {
                result.error = "Pokemon already has an active mirror: " + pkrid;
                return result;
            }
        }
    }

    try {
        const long long now = unixNow();
        if (pokemon->hot.pid) {
            pokemon_.ensureOriginalPidIfUnset(pkrid, *pokemon->hot.pid);
            if (!pokemon->original_pid) {
                pokemon->original_pid = *pokemon->hot.pid;
            }
        }
        if (!transport_pid) {
            transport_pid = pokemon->hot.pid;
        }

        const std::optional<std::uint16_t> beacon_tid = context.use_gen12_beacon
            ? std::optional<std::uint16_t>(beaconTidForPkrid(pkrid))
            : std::nullopt;
        const std::string beacon_ot = context.use_gen12_beacon ? "RESORT" : pokemon->hot.ot_name;

        std::ostringstream projection;
        projection << "{"
                   << "\"projection_schema\":1,"
                   << "\"projection_kind\":\"resort_prepared_bridge_projection\","
                   << "\"pkrid\":\"" << escapeJson(pokemon->id.pkrid) << "\","
                   << "\"target_game\":" << context.target_game << ","
                   << "\"target_format\":\"" << escapeJson(format_name) << "\","
                   << "\"transport_pid\":"
                   << (transport_pid ? std::to_string(*transport_pid) : "null")
                   << "}";

        SqliteTransaction tx(connection_);

        PokemonSnapshot snapshot;
        snapshot.snapshot_id = generateId("snap");
        snapshot.pkrid = pkrid;
        snapshot.kind = SnapshotKind::ExportProjection;
        snapshot.format_name = format_name;
        snapshot.game_id = context.target_game;
        snapshot.captured_at_unix = now;
        snapshot.raw_bytes = raw_payload;
        snapshot.raw_hash_sha256 = raw_hash;
        snapshot.parsed_json = projection.str();
        snapshot.notes_json = "{\"schema_version\":1,\"prepared_for_game_write\":true}";
        snapshots_.insert(snapshot);

        MirrorOpenContext mirror_context;
        mirror_context.beacon_tid16 = beacon_tid;
        mirror_context.beacon_ot_name = context.use_gen12_beacon
            ? std::optional<std::string>(beacon_ot)
            : std::nullopt;
        mirror_context.mirror_canonical_pid =
            pokemon->original_pid ? pokemon->original_pid : pokemon->hot.pid;
        mirror_context.transport_pid = transport_pid;
        mirror_context.projection_metadata_json = projection.str();
        MirrorSession mirror = mirror_sessions_.openMirrorSession(pkrid, context.target_game, mirror_context);

        const std::optional<std::uint32_t> canonical_personality =
            pokemon->original_pid ? pokemon->original_pid : pokemon->hot.pid;
        const int target_constraint_gen = constraintGenerationFromStorageFormat(format_name);
        int source_constraint_gen = 0;
        if (const std::string origin_format = pkmStorageFormatNameForGameId(pokemon->hot.origin_game);
            !origin_format.empty()) {
            source_constraint_gen = constraintGenerationFromStorageFormat(origin_format);
        }
        if (source_constraint_gen == 0) {
            source_constraint_gen = target_constraint_gen;
        }
        if (transport_pid && canonical_personality && target_constraint_gen > 0 &&
            *transport_pid != *canonical_personality) {
            pid_transport_.insertActiveMapping(
                *transport_pid,
                pkrid,
                *canonical_personality,
                source_constraint_gen,
                target_constraint_gen,
                now,
                mirror.mirror_session_id);
            pokemon_.appendPidHistoryEntry(
                pkrid,
                *transport_pid,
                source_constraint_gen,
                target_constraint_gen,
                now);
        }

        if (context.managed_mirror) {
            boxes_.removePokemon("default", pkrid);
        }

        PokemonHistoryEvent event;
        event.event_id = generateId("hist");
        event.pkrid = pkrid;
        event.event_type = HistoryEventType::Exported;
        event.timestamp_unix = now;
        event.source_snapshot_id = snapshot.snapshot_id;
        event.mirror_session_id = mirror.mirror_session_id;
        event.diff_json = exportDiffJson(context, mirror.mirror_session_id);
        history_.insert(event);

        tx.commit();

        result.success = true;
        result.pkrid = pkrid;
        result.snapshot_id = snapshot.snapshot_id;
        result.mirror_session_id = mirror.mirror_session_id;
        result.format_name = format_name;
        result.raw_payload = raw_payload;
        result.raw_hash = raw_hash;
        result.transport_pid = transport_pid;
        return result;
    } catch (const std::exception& ex) {
        result.error = ex.what();
        return result;
    }
}

} // namespace pr::resort
