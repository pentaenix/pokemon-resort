#include "core/save/SaveLibrary.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

std::string readText(const fs::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw TestFailure("Could not read file: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        throw TestFailure("Could not write file: " + path.string());
    }
    out << text;
}

int readCounter(const fs::path& path) {
    if (!fs::exists(path)) {
        return 0;
    }
    return std::stoi(readText(path));
}

void replaceAll(std::string& text, const std::string& from, const std::string& to) {
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

struct CacheFixture {
    fs::path root;
    fs::path project_root;
    fs::path saves_dir;
    fs::path cache_dir;
    fs::path cache_file;
    fs::path counter_file;
    fs::path bridge_script;
    fs::path save_file;

    explicit CacheFixture(const std::string& name) {
        root = fs::temp_directory_path() / ("pokemon_resort_cache_" + name);
        fs::remove_all(root);
        project_root = root / "pokemon-resort";
        saves_dir = root / "saves";
        cache_dir = root / "cache";
        cache_file = cache_dir / "transfer_save_cache.json";
        counter_file = root / "bridge_counter.txt";
        bridge_script = root / "fake_bridge.sh";
        save_file = saves_dir / "test.sav";

        fs::create_directories(project_root);
        fs::create_directories(saves_dir);
        fs::create_directories(cache_dir);
        writeText(save_file, "first-save-payload");
        writeFakeBridge();
        setenv("PKHEX_BRIDGE_EXECUTABLE", bridge_script.c_str(), 1);
    }

    ~CacheFixture() {
        unsetenv("PKHEX_BRIDGE_EXECUTABLE");
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    pr::SaveLibrary makeLibrary() const {
        return pr::SaveLibrary(project_root.string(), cache_dir.string(), nullptr);
    }

    const pr::SaveFileRecord& singleRecord(pr::SaveLibrary& library) const {
        library.scanAndProbeProjectSaves();
        const auto& records = library.records();
        expect(records.size() == 1, "expected exactly one discovered save record");
        return records.front();
    }

    void writeFakeBridge() {
        writeText(bridge_script, R"script(#!/bin/zsh
set -euo pipefail
COUNTER_FILE=")script" + counter_file.string() + R"script("
COUNT=0
if [[ -f "$COUNTER_FILE" ]]; then
  COUNT="$(cat "$COUNTER_FILE")"
fi
COUNT=$((COUNT + 1))
echo "$COUNT" > "$COUNTER_FILE"
cat <<JSON
{"bridge_probe_schema":5,"success":true,"game_id":"pokemon_ruby","player_name":"CacheTester","party":["pikachu"],"play_time":"01:23","pokedex_count":25,"badges":1,"status":"OK","all_pokemon":[{"SpeciesSlug":"pikachu","SpeciesId":25,"Nickname":"Pika","Location":{"Area":"party","Slot":0,"GlobalIndex":0}}],"boxes":[{"Index":0,"Name":"BOX 1","Slots":[{"Slot":0,"Pokemon":{"SpeciesSlug":"pikachu","SpeciesId":25,"Nickname":"Pika","Location":{"Area":"box","Box":0,"Slot":0,"GlobalIndex":0}}}]}]}
JSON
)script");
        fs::permissions(
            bridge_script,
            fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write,
            fs::perm_options::add);
    }
};

void testFirstScanGeneratesCacheAndMissesBridge() {
    CacheFixture fixture("first_scan");
    pr::SaveLibrary library = fixture.makeLibrary();

    const pr::SaveFileRecord& record = fixture.singleRecord(library);

    expect(!record.used_cache, "first scan should be a cache miss");
    expect(record.probe_status == pr::SaveProbeStatus::ValidSave, "first scan should probe as valid save");
    expect(record.transfer_summary.has_value(), "first scan should produce transfer summary");
    expect(record.transfer_summary->game_id == "pokemon_ruby", "first scan summary should come from fake bridge");
    expect(record.bridge_result.command != "cache_hit", "first scan should not report cache_hit command");
    expect(readCounter(fixture.counter_file) == 1, "first scan should launch fake bridge once");
    expect(fs::exists(fixture.cache_file), "first scan should write transfer_save_cache.json");
    const std::string cache_text = readText(fixture.cache_file);
    expect(cache_text.find("\"box_1_slots\"") != std::string::npos, "cache should serialize a box_1_slots field");
    expect(cache_text.find("\"area\": \"box\"") == std::string::npos,
           "menu cache should omit box-detail payloads so transfer-system details require a fresh probe");
}

void testSecondScanUsesCacheHitWithoutLaunchingBridge() {
    CacheFixture fixture("cache_hit");
    {
        pr::SaveLibrary first = fixture.makeLibrary();
        (void)fixture.singleRecord(first);
    }

    pr::SaveLibrary second = fixture.makeLibrary();
    const pr::SaveFileRecord& record = fixture.singleRecord(second);

    expect(record.used_cache, "second scan with unchanged file should use cache");
    expect(record.bridge_result.bridge_path == "cache", "cache hit should set bridge_path=cache for diagnostics");
    expect(record.bridge_result.command == "cache_hit", "cache hit should set command=cache_hit for diagnostics");
    expect(record.probe_status == pr::SaveProbeStatus::ValidSave, "cache hit should preserve valid status");
    expect(record.transfer_summary.has_value(), "cache hit should restore transfer summary");
    expect(record.transfer_summary->party_slots.size() == 1, "cache hit should restore party slots for ticket sprites");
    expect(readCounter(fixture.counter_file) == 1, "cache hit should not launch fake bridge again");
}

void testChangedSaveHashForcesCacheMissAndRegeneratesCache() {
    CacheFixture fixture("hash_miss");
    {
        pr::SaveLibrary first = fixture.makeLibrary();
        (void)fixture.singleRecord(first);
    }

    writeText(fixture.save_file, "changed-save-payload");

    pr::SaveLibrary second = fixture.makeLibrary();
    const pr::SaveFileRecord& record = fixture.singleRecord(second);

    expect(!record.used_cache, "changed save bytes should force cache miss");
    expect(record.bridge_result.command != "cache_hit", "hash miss should not report cache_hit command");
    expect(record.probe_status == pr::SaveProbeStatus::ValidSave, "hash miss should re-probe as valid save");
    expect(readCounter(fixture.counter_file) == 2, "hash miss should launch fake bridge again");
}

void testStaleCacheWithoutTicketFieldsReprobes() {
    CacheFixture fixture("stale_missing_fields");
    {
        pr::SaveLibrary first = fixture.makeLibrary();
        (void)fixture.singleRecord(first);
    }

    std::string cache = readText(fixture.cache_file);
    replaceAll(cache, "\"player_name\": \"CacheTester\"", "\"player_name\": \"\"");
    replaceAll(cache, "\"party\": [\"pikachu\"]", "\"party\": []");
    writeText(fixture.cache_file, cache);

    pr::SaveLibrary second = fixture.makeLibrary();
    const pr::SaveFileRecord& record = fixture.singleRecord(second);

    expect(!record.used_cache, "stale cache missing ticket fields should be ignored");
    expect(record.transfer_summary.has_value(), "stale cache re-probe should restore summary");
    expect(record.transfer_summary->player_name == "CacheTester", "re-probe should replace stale missing player name");
    expect(readCounter(fixture.counter_file) == 2, "stale cache should launch fake bridge again");
}

void testKnownBadCachedGameIdReprobes() {
    CacheFixture fixture("known_bad_game_id");
    {
        pr::SaveLibrary first = fixture.makeLibrary();
        (void)fixture.singleRecord(first);
    }

    std::string cache = readText(fixture.cache_file);
    replaceAll(cache, "\"game_id\": \"pokemon_ruby\"", "\"game_id\": \"pokemon_or\"");
    writeText(fixture.cache_file, cache);

    pr::SaveLibrary second = fixture.makeLibrary();
    const pr::SaveFileRecord& record = fixture.singleRecord(second);

    expect(!record.used_cache, "known bad cached game_id should be ignored");
    expect(record.transfer_summary.has_value(), "known bad game id re-probe should restore summary");
    expect(record.transfer_summary->game_id == "pokemon_ruby", "re-probe should replace known bad cached game id");
    expect(readCounter(fixture.counter_file) == 2, "known bad cached game_id should launch fake bridge again");
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests{
        {"first scan generates cache and misses bridge", testFirstScanGeneratesCacheAndMissesBridge},
        {"second scan uses cache hit without launching bridge", testSecondScanUsesCacheHitWithoutLaunchingBridge},
        {"changed save hash forces cache miss and regenerates cache", testChangedSaveHashForcesCacheMissAndRegeneratesCache},
        {"stale cache without ticket fields reprobes", testStaleCacheWithoutTicketFieldsReprobes},
        {"known bad cached game id reprobes", testKnownBadCachedGameIdReprobes},
    };

    int failures = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "[PASS] " << test.first << '\n';
        } catch (const std::exception& ex) {
            ++failures;
            std::cerr << "[FAIL] " << test.first << ": " << ex.what() << '\n';
        }
    }

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
