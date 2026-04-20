#include "resort/domain/ExportedPokemon.hpp"
#include "resort/domain/ImportedPokemon.hpp"
#include "resort/services/PokemonResortService.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
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

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: resort_backend_tool seed|export --db <profile.resort.db> ...\n";
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
        std::cerr << "Unknown command: " << command << '\n';
        return 2;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
