#include "resort/domain/ExportedPokemon.hpp"
#include "resort/domain/ImportedPokemon.hpp"
#include "resort/integration/BridgeImportAdapter.hpp"
#include "resort/persistence/PokemonRepository.hpp"
#include "resort/persistence/SnapshotRepository.hpp"
#include "resort/services/PokemonResortService.hpp"
#include "core/bridge/SaveBridgeClient.hpp"
#include "core/config/Json.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::map<std::string, std::string> parseOptions(int argc, char** argv, int start) {
    std::map<std::string, std::string> options;
    for (int i = start; i < argc; ++i) {
        std::string key = argv[i];
        if (key.rfind("--", 0) != 0 || i + 1 >= argc) {
            continue;
        }
        options[key.substr(2)] = argv[++i];
    }
    return options;
}

std::string require(const std::map<std::string, std::string>& options, const std::string& key) {
    auto it = options.find(key);
    if (it == options.end() || it->second.empty()) {
        throw std::runtime_error("Missing required option --" + key);
    }
    return it->second;
}

int optInt(const std::map<std::string, std::string>& options, const std::string& key, int fallback) {
    auto it = options.find(key);
    return it == options.end() ? fallback : std::stoi(it->second);
}

unsigned long long fnv1a64(const std::vector<unsigned char>& bytes) {
    unsigned long long hash = 14695981039346656037ull;
    for (const unsigned char ch : bytes) {
        hash ^= ch;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string stableHash64Hex(const std::vector<unsigned char>& bytes) {
    const unsigned long long a = fnv1a64(bytes);
    std::vector<unsigned char> bbytes = bytes;
    bbytes.push_back(0x42);
    const unsigned long long b = fnv1a64(bbytes);
    std::ostringstream out;
    out << std::hex << std::setfill('0')
        << std::setw(16) << a
        << std::setw(16) << b
        << std::setw(16) << (a ^ 0x123456789abcdef0ull)
        << std::setw(16) << (b ^ 0x0fedcba987654321ull);
    return out.str();
}

std::vector<unsigned char> bytesForSeedPayload(const std::string& text) {
    return std::vector<unsigned char>(text.begin(), text.end());
}

std::string escapeJson(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::string serializeJson(const pr::JsonValue& value);

std::string serializeJsonObject(const pr::JsonValue::Object& object) {
    std::ostringstream out;
    out << "{";
    bool first = true;
    for (const auto& [key, item] : object) {
        if (!first) {
            out << ",";
        }
        first = false;
        out << "\"" << escapeJson(key) << "\":" << serializeJson(item);
    }
    out << "}";
    return out.str();
}

std::string serializeJsonArray(const pr::JsonValue::Array& array) {
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < array.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << serializeJson(array[i]);
    }
    out << "]";
    return out.str();
}

std::string serializeJson(const pr::JsonValue& value) {
    if (value.isNull()) {
        return "null";
    }
    if (value.isBool()) {
        return value.asBool() ? "true" : "false";
    }
    if (value.isNumber()) {
        std::ostringstream out;
        out << std::setprecision(15) << value.asNumber();
        return out.str();
    }
    if (value.isString()) {
        return "\"" + escapeJson(value.asString()) + "\"";
    }
    if (value.isArray()) {
        return serializeJsonArray(value.asArray());
    }
    return serializeJsonObject(value.asObject());
}

bool isEmptyJsonPayload(const std::string& json) {
    return json.empty() || json == "{}" || json == "{\"schema_version\":1}";
}

pr::JsonValue mergeJsonValue(const pr::JsonValue& existing, const pr::JsonValue& incoming) {
    if (existing.isObject() && incoming.isObject()) {
        pr::JsonValue::Object merged = existing.asObject();
        for (const auto& [key, incoming_value] : incoming.asObject()) {
            auto it = merged.find(key);
            if (it == merged.end()) {
                merged.emplace(key, incoming_value);
            } else {
                it->second = mergeJsonValue(it->second, incoming_value);
            }
        }
        return pr::JsonValue(merged);
    }
    if (existing.isArray() && incoming.isArray()) {
        pr::JsonValue::Array merged = existing.asArray();
        std::set<std::string> seen;
        for (const auto& item : merged) {
            seen.insert(serializeJson(item));
        }
        for (const auto& item : incoming.asArray()) {
            if (seen.insert(serializeJson(item)).second) {
                merged.push_back(item);
            }
        }
        return pr::JsonValue(merged);
    }
    return incoming.isNull() ? existing : incoming;
}

std::string mergeJsonPayload(const std::string& existing, const std::string& incoming) {
    if (isEmptyJsonPayload(incoming)) {
        return isEmptyJsonPayload(existing) ? std::string("{\"schema_version\":1}") : existing;
    }
    if (isEmptyJsonPayload(existing)) {
        return incoming;
    }
    try {
        return serializeJson(mergeJsonValue(pr::parseJsonText(existing), pr::parseJsonText(incoming)));
    } catch (const std::exception&) {
        return existing;
    }
}

fs::path projectRootFromOptions(const std::map<std::string, std::string>& options, const char* argv0) {
    if (options.count("project-root")) {
        return fs::path(options.at("project-root"));
    }
    fs::path executable = argv0 ? fs::path(argv0) : fs::path();
    if (executable.is_relative()) {
        executable = fs::current_path() / executable;
    }
    const fs::path dir = executable.parent_path();
    if (dir.filename() == "build") {
        return dir.parent_path();
    }
    return fs::current_path();
}

std::vector<std::string> allPokemonIds(pr::resort::SqliteConnection& connection) {
    auto stmt = connection.prepare("SELECT pkrid FROM pokemon ORDER BY updated_at ASC, pkrid ASC");
    std::vector<std::string> ids;
    while (stmt.stepRow()) {
        ids.push_back(stmt.columnText(0));
    }
    return ids;
}

void writeBytes(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Could not write temp PKM file: " + path.string());
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void updatePokemonFromInspectedSnapshot(
    pr::resort::SqliteConnection& connection,
    const pr::resort::ResortPokemon& existing,
    const pr::resort::ImportedPokemon& inspected,
    const std::string& warm_json) {
    auto stmt = connection.prepare(R"sql(
UPDATE pokemon SET
    revision = revision + 1,
    updated_at = strftime('%s','now'),
    species_id = ?,
    form_id = ?,
    nickname = ?,
    is_nicknamed = ?,
    level = ?,
    exp = ?,
    gender = ?,
    shiny = ?,
    ability_id = ?,
    ability_slot = ?,
    held_item_id = ?,
    hp_current = ?,
    hp_max = ?,
    status_flags = ?,
    ot_name = ?,
    tid16 = ?,
    sid16 = ?,
    tid32 = ?,
    origin_game = ?,
    language = ?,
    met_location_id = ?,
    met_level = ?,
    met_date = ?,
    ball_id = ?,
    pid = ?,
    encryption_constant = ?,
    home_tracker = ?,
    lineage_root_species = ?,
    dv16 = ?,
    identity_strength = ?,
    warm_json = ?
WHERE pkrid = ?
)sql");

    const pr::resort::PokemonHot& h = inspected.hot;
    stmt.bindInt(1, h.species_id);
    stmt.bindInt(2, h.form_id);
    stmt.bindText(3, h.nickname);
    stmt.bindInt(4, h.is_nicknamed ? 1 : 0);
    stmt.bindInt(5, h.level);
    stmt.bindInt64(6, h.exp);
    stmt.bindInt(7, h.gender);
    stmt.bindInt(8, h.shiny ? 1 : 0);
    if (h.ability_id) stmt.bindInt(9, *h.ability_id); else stmt.bindNull(9);
    if (h.ability_slot) stmt.bindInt(10, *h.ability_slot); else stmt.bindNull(10);
    if (h.held_item_id) stmt.bindInt(11, *h.held_item_id); else stmt.bindNull(11);
    stmt.bindInt(12, h.hp_current);
    stmt.bindInt(13, h.hp_max);
    stmt.bindInt64(14, h.status_flags);
    stmt.bindText(15, h.ot_name.empty() ? existing.hot.ot_name : h.ot_name);
    if (h.tid16) stmt.bindInt(16, *h.tid16); else stmt.bindNull(16);
    if (h.sid16) stmt.bindInt(17, *h.sid16); else stmt.bindNull(17);
    if (h.tid32) stmt.bindInt64(18, *h.tid32); else stmt.bindNull(18);
    stmt.bindInt(19, h.origin_game != 0 ? h.origin_game : existing.hot.origin_game);
    if (h.language) stmt.bindInt(20, *h.language); else stmt.bindNull(20);
    if (h.met_location_id) stmt.bindInt(21, *h.met_location_id); else stmt.bindNull(21);
    if (h.met_level) stmt.bindInt(22, *h.met_level); else stmt.bindNull(22);
    if (h.met_date_unix) stmt.bindInt64(23, *h.met_date_unix); else stmt.bindNull(23);
    if (h.ball_id) stmt.bindInt(24, *h.ball_id); else stmt.bindNull(24);
    if (h.pid) stmt.bindInt64(25, *h.pid); else stmt.bindNull(25);
    if (h.encryption_constant) stmt.bindInt64(26, *h.encryption_constant); else stmt.bindNull(26);
    if (h.home_tracker) stmt.bindText(27, *h.home_tracker); else stmt.bindNull(27);
    stmt.bindInt(28, h.lineage_root_species != 0 ? h.lineage_root_species : h.species_id);
    if (h.dv16) stmt.bindInt(29, *h.dv16); else stmt.bindNull(29);
    stmt.bindInt(30, h.identity_strength);
    stmt.bindBlob(31, warm_json.data(), static_cast<int>(warm_json.size()));
    stmt.bindText(32, existing.id.pkrid);
    stmt.stepDone();
}

int seedCommand(const std::map<std::string, std::string>& options) {
    pr::resort::PokemonResortService service(require(options, "db"));
    const std::string profile = options.count("profile") ? options.at("profile") : "default";
    service.ensureProfile(profile);

    pr::resort::ImportedPokemon imported;
    imported.source_game = static_cast<unsigned short>(optInt(options, "source-game", 0));
    imported.format_name = options.count("format") ? options.at("format") : "seed";
    imported.hot.species_id = static_cast<unsigned short>(std::stoi(require(options, "species")));
    imported.hot.form_id = static_cast<unsigned short>(optInt(options, "form", 0));
    imported.hot.nickname = options.count("nickname") ? options.at("nickname") : ("Species " + std::to_string(imported.hot.species_id));
    imported.hot.is_nicknamed = true;
    imported.hot.level = static_cast<unsigned char>(optInt(options, "level", 5));
    imported.hot.exp = static_cast<unsigned int>(optInt(options, "exp", imported.hot.level * imported.hot.level * imported.hot.level));
    imported.hot.gender = static_cast<unsigned char>(optInt(options, "gender", 0));
    imported.hot.hp_current = static_cast<unsigned short>(optInt(options, "hp", 20));
    imported.hot.hp_max = static_cast<unsigned short>(optInt(options, "hp-max", imported.hot.hp_current));
    imported.hot.ot_name = options.count("ot") ? options.at("ot") : "RESORT";
    imported.hot.tid16 = static_cast<unsigned short>(optInt(options, "tid", 50000));
    imported.hot.sid16 = static_cast<unsigned short>(optInt(options, "sid", 0));
    imported.hot.origin_game = imported.source_game;
    imported.hot.lineage_root_species = imported.hot.species_id;
    imported.identity.tid16 = imported.hot.tid16;
    imported.identity.sid16 = imported.hot.sid16;
    imported.identity.ot_name = imported.hot.ot_name;
    imported.identity.lineage_root_species = imported.hot.lineage_root_species;

    if (options.count("pid")) {
        imported.hot.pid = static_cast<unsigned int>(std::stoul(options.at("pid")));
        imported.identity.pid = imported.hot.pid;
    }
    if (options.count("ec")) {
        imported.hot.encryption_constant = static_cast<unsigned int>(std::stoul(options.at("ec")));
        imported.identity.encryption_constant = imported.hot.encryption_constant;
    }

    const std::string seed_json =
        "{\"seed_schema\":1,\"species\":" + std::to_string(imported.hot.species_id) +
        ",\"nickname\":\"" + imported.hot.nickname + "\"}";
    imported.raw_bytes = bytesForSeedPayload(seed_json);
    imported.raw_hash_sha256 = stableHash64Hex(imported.raw_bytes);
    imported.warm_json = "{\"schema_version\":1,\"seeded\":true}";
    imported.suspended_json = "{\"schema_version\":1,\"seed_payload\":\"backend_tool\"}";

    pr::resort::ImportContext context;
    context.profile_id = profile;
    if (options.count("box") || options.count("slot")) {
        context.target_location = pr::resort::BoxLocation{
            profile,
            optInt(options, "box", 0),
            optInt(options, "slot", 0)
        };
    }

    const auto result = service.importParsedPokemon(imported, context);
    if (!result.success) {
        std::cerr << result.error << '\n';
        return 1;
    }

    std::cout << "{\"success\":true,\"pkrid\":\"" << result.pkrid
              << "\",\"created\":" << (result.created ? "true" : "false")
              << ",\"merged\":" << (result.merged ? "true" : "false")
              << "}\n";
    return 0;
}

int exportCommand(const std::map<std::string, std::string>& options) {
    pr::resort::PokemonResortService service(require(options, "db"));
    pr::resort::ExportContext context;
    context.target_game = static_cast<unsigned short>(std::stoi(require(options, "target-game")));
    context.target_format_name = options.count("format") ? options.at("format") : "projection-json";
    context.use_gen12_beacon = options.count("gen12-beacon") && options.at("gen12-beacon") == "true";
    const auto result = service.exportPokemon(require(options, "pkrid"), context);
    if (!result.success) {
        std::cerr << result.error << '\n';
        return 1;
    }
    if (options.count("out")) {
        std::ofstream out(options.at("out"), std::ios::binary);
        out.write(reinterpret_cast<const char*>(result.raw_payload.data()), static_cast<std::streamsize>(result.raw_payload.size()));
    }
    std::cout << "{\"success\":true,\"pkrid\":\"" << result.pkrid
              << "\",\"snapshot_id\":\"" << result.snapshot_id
              << "\",\"mirror_session_id\":\"" << result.mirror_session_id
              << "\"}\n";
    return 0;
}

int recoverCommand(const std::map<std::string, std::string>& options) {
    pr::resort::PokemonResortService service(require(options, "db"));
    const std::string profile = options.count("profile") ? options.at("profile") : "default";
    const auto result = service.recoverPokemonToFirstAvailableSlot(profile, require(options, "pkrid"));
    if (!result.success) {
        std::cerr << result.error << '\n';
        return 1;
    }

    std::cout << "{\"success\":true,\"pkrid\":\"" << require(options, "pkrid")
              << "\",\"profile_id\":\"" << result.location.profile_id
              << "\",\"box_id\":" << result.location.box_id
              << ",\"slot_index\":" << result.location.slot_index
              << ",\"already_boxed\":" << (result.already_boxed ? "true" : "false")
              << ",\"closed_active_mirror\":" << (result.closed_active_mirror ? "true" : "false")
              << "}\n";
    return 0;
}

int resetCommand(const std::map<std::string, std::string>& options) {
    pr::resort::PokemonResortService service(require(options, "db"));
    const std::string profile = options.count("profile") ? options.at("profile") : "default";
    const std::string confirm = options.count("confirm") ? options.at("confirm") : "";
    if (confirm != "RESET") {
        std::cerr << "Refusing to reset Resort profile without --confirm RESET\n";
        return 2;
    }

    const std::string backup = options.count("backup") ? options.at("backup") : "";
    const auto result = service.resetProfileToEmpty(profile, backup);
    if (!result.success) {
        std::cerr << result.error << '\n';
        return 1;
    }
    std::cout << "{\"success\":true,\"profile_id\":\"" << profile
              << "\",\"backup_path\":\"" << result.backup_path
              << "\"}\n";
    return 0;
}

int backfillWarmMetadataCommand(const std::map<std::string, std::string>& options, const char* argv0) {
    const fs::path db_path = require(options, "db");
    const fs::path project_root = projectRootFromOptions(options, argv0);
    const bool dry_run = options.count("dry-run") && options.at("dry-run") == "true";
    const bool no_backup = options.count("no-backup") && options.at("no-backup") == "true";

    if (!dry_run && !no_backup) {
        const fs::path backup_path = options.count("backup")
            ? fs::path(options.at("backup"))
            : fs::path(db_path.string() + ".bak.backfill-warm-metadata");
        fs::copy_file(db_path, backup_path, fs::copy_options::overwrite_existing);
        std::cerr << "Backed up Resort DB to " << backup_path << '\n';
    }

    pr::resort::SqliteConnection connection(db_path);
    pr::resort::PokemonRepository pokemon_repo(connection);
    pr::resort::SnapshotRepository snapshot_repo(connection);

    int inspected = 0;
    int updated = 0;
    int skipped = 0;
    int failed = 0;

    const fs::path temp_dir = fs::temp_directory_path() / "pokemon-resort-backfill";
    fs::create_directories(temp_dir);

    for (const std::string& pkrid : allPokemonIds(connection)) {
        const auto existing = pokemon_repo.findById(pkrid);
        if (!existing) {
            ++skipped;
            continue;
        }
        const auto snapshot = snapshot_repo.findLatestRawForPokemon(pkrid);
        if (!snapshot || snapshot->raw_bytes.empty()) {
            ++skipped;
            continue;
        }

        const int source_game = snapshot->game_id
            ? static_cast<int>(*snapshot->game_id)
            : static_cast<int>(existing->hot.origin_game);
        const fs::path pkm_path = temp_dir / (pkrid + ".pkm");
        writeBytes(pkm_path, snapshot->raw_bytes);

        const pr::SaveBridgeProbeResult bridge =
            pr::inspectPkmWithBridge(project_root.string(), argv0, pkm_path.string(), source_game);
        std::error_code rm_error;
        fs::remove(pkm_path, rm_error);
        ++inspected;

        if (!bridge.success) {
            ++failed;
            std::cerr << "Could not inspect " << pkrid << ": " << pr::formatBridgeRunFailureMessage(bridge) << '\n';
            continue;
        }

        const pr::resort::BridgeImportParseResult parsed = pr::resort::parseBridgeImportPayload(bridge.stdout_text);
        if (!parsed.success || parsed.pokemon.empty()) {
            ++failed;
            std::cerr << "Could not parse bridge metadata for " << pkrid << ": " << parsed.error << '\n';
            continue;
        }

        const pr::resort::ImportedPokemon& imported = parsed.pokemon.front();
        const std::string merged_warm = mergeJsonPayload(existing->warm.json, imported.warm_json);
        if (!dry_run) {
            updatePokemonFromInspectedSnapshot(connection, *existing, imported, merged_warm);
        }
        ++updated;
    }

    std::cout << "{\"success\":true"
              << ",\"dry_run\":" << (dry_run ? "true" : "false")
              << ",\"inspected\":" << inspected
              << ",\"updated\":" << updated
              << ",\"skipped\":" << skipped
              << ",\"failed\":" << failed
              << "}\n";
    return failed == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: resort_backend_tool seed|export|recover|reset|backfill-warm-metadata --db <profile.resort.db> ...\n";
        return 2;
    }

    try {
        const std::string command = argv[1];
        const auto options = parseOptions(argc, argv, 2);
        if (command == "seed") {
            return seedCommand(options);
        }
        if (command == "export") {
            return exportCommand(options);
        }
        if (command == "recover") {
            return recoverCommand(options);
        }
        if (command == "reset") {
            return resetCommand(options);
        }
        if (command == "backfill-warm-metadata") {
            return backfillWarmMetadataCommand(options, argv[0]);
        }
        std::cerr << "Unknown command: " << command << '\n';
        return 2;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
