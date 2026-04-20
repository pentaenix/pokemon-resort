#include "resort/services/PokemonExportService.hpp"

#include "resort/domain/Ids.hpp"

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace pr::resort {

namespace {

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
    SnapshotRepository& snapshots,
    HistoryRepository& history,
    MirrorSessionService& mirror_sessions)
    : connection_(connection),
      pokemon_(pokemon),
      snapshots_(snapshots),
      history_(history),
      mirror_sessions_(mirror_sessions) {}

ExportResult PokemonExportService::exportPokemon(
    const std::string& pkrid,
    const ExportContext& context) {
    ExportResult result;
    const auto pokemon = pokemon_.findById(pkrid);
    if (!pokemon) {
        result.error = "Pokemon not found: " + pkrid;
        return result;
    }

    try {
        const long long now = unixNow();
        const std::optional<std::uint16_t> beacon_tid = context.use_gen12_beacon
            ? std::optional<std::uint16_t>(beaconTidForPkrid(pkrid))
            : std::nullopt;
        const std::string beacon_ot = context.use_gen12_beacon ? "RESORT" : pokemon->hot.ot_name;
        const std::string projection = projectionJson(*pokemon, context, beacon_tid, beacon_ot);
        const std::vector<unsigned char> raw(projection.begin(), projection.end());
        const std::string raw_hash = stableProjectionHash(raw);

        SqliteTransaction tx(connection_);

        PokemonSnapshot snapshot;
        snapshot.snapshot_id = generateId("snap");
        snapshot.pkrid = pkrid;
        snapshot.kind = SnapshotKind::ExportProjection;
        snapshot.format_name = context.target_format_name;
        snapshot.game_id = context.target_game;
        snapshot.captured_at_unix = now;
        snapshot.raw_bytes = raw;
        snapshot.raw_hash_sha256 = raw_hash;
        snapshot.parsed_json = projection;
        snapshot.notes_json = "{\"schema_version\":1,\"synthetic_projection\":true}";
        snapshots_.insert(snapshot);

        MirrorOpenContext mirror_context;
        mirror_context.beacon_tid16 = beacon_tid;
        mirror_context.beacon_ot_name = context.use_gen12_beacon
            ? std::optional<std::string>(beacon_ot)
            : std::nullopt;
        mirror_context.projection_metadata_json = projection;
        MirrorSession mirror = mirror_sessions_.openMirrorSession(pkrid, context.target_game, mirror_context);

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
        result.format_name = context.target_format_name;
        result.raw_payload = raw;
        result.raw_hash = raw_hash;
        return result;
    } catch (const std::exception& ex) {
        result.error = ex.what();
        return result;
    }
}

} // namespace pr::resort
