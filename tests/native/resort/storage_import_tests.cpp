#include "core/crypto/Sha256.hpp"
#include "resort/domain/Ids.hpp"
#include "resort/domain/PkmFormat.hpp"
#include "resort/domain/ResortPokemonRecord.hpp"
#include "resort/integration/BridgeImportAdapter.hpp"
#include "resort/openhome/OpenHomeStorageBridge.hpp"
#include "resort/openhome/OpenHomeMovementBridge.hpp"
#include "resort/integration/Gen12DvBytes.hpp"
#include "resort/persistence/BoxRepository.hpp"
#include "resort/persistence/Migrations.hpp"
#include "resort/persistence/PidTransportRegistryRepository.hpp"
#include "resort/persistence/PokemonRepository.hpp"
#include "resort/persistence/SnapshotRepository.hpp"
#include "resort/persistence/SqliteConnection.hpp"
#include "resort/services/BridgeImportService.hpp"
#include "resort/services/PokemonResortService.hpp"

#include <filesystem>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <cstdlib>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr const char* kHashA = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
constexpr const char* kHashB = "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

fs::path tempDbPath(const std::string& name) {
    const fs::path path = fs::temp_directory_path() / ("pokemon_resort_" + name + ".db");
    fs::remove(path);
    fs::remove(path.string() + "-wal");
    fs::remove(path.string() + "-shm");
    fs::remove(path.parent_path() / (path.stem().string() + ".transfer.jsonl"));
    return path;
}

int scalarInt(const fs::path& path, const std::string& sql) {
    pr::resort::SqliteConnection connection(path);
    pr::resort::runResortMigrations(connection);
    auto stmt = connection.prepare(sql);
    expect(stmt.stepRow(), "Expected scalar query row");
    return stmt.columnInt(0);
}

std::string scalarText(const fs::path& path, const std::string& sql) {
    pr::resort::SqliteConnection connection(path);
    pr::resort::runResortMigrations(connection);
    auto stmt = connection.prepare(sql);
    expect(stmt.stepRow(), "Expected scalar text row");
    return stmt.columnText(0);
}

std::string scalarBlob(const fs::path& path, const std::string& sql) {
    pr::resort::SqliteConnection connection(path);
    pr::resort::runResortMigrations(connection);
    auto stmt = connection.prepare(sql);
    expect(stmt.stepRow(), "Expected scalar blob row");
    return stmt.columnBlobAsString(0);
}

fs::path repositoryRoot() {
    fs::path current = fs::current_path();
    while (!current.empty()) {
        if (fs::exists(current / "CMakeLists.txt") &&
            fs::exists(current / "tests" / "test-data" / "saves")) {
            return current;
        }
        current = current.parent_path();
    }
    throw TestFailure("Could not locate repository root");
}

fs::path backendToolPath() {
    const fs::path cwd = fs::current_path();
    if (fs::exists(cwd / "resort_backend_tool")) {
        return cwd / "resort_backend_tool";
    }
    if (fs::exists(cwd / "build" / "resort_backend_tool")) {
        return cwd / "build" / "resort_backend_tool";
    }
    const fs::path root = repositoryRoot();
    if (fs::exists(root / "build" / "resort_backend_tool")) {
        return root / "build" / "resort_backend_tool";
    }
    throw TestFailure("Could not locate resort_backend_tool");
}

pr::resort::ImportedPokemon makeImported(
    unsigned short species,
    const std::string& nickname,
    const std::string& hash = kHashA,
    std::vector<unsigned char> raw = {1, 2, 3, 4}) {
    pr::resort::ImportedPokemon imported;
    imported.source_game = 3;
    imported.format_name = "pk3";
    imported.raw_bytes = std::move(raw);
    imported.raw_hash_sha256 = hash;
    imported.hot.species_id = species;
    imported.hot.form_id = 0;
    imported.hot.nickname = nickname;
    imported.hot.is_nicknamed = !nickname.empty();
    imported.hot.level = 12;
    imported.hot.exp = 1234;
    imported.hot.gender = 1;
    imported.hot.shiny = false;
    imported.hot.ability_id = 65;
    imported.hot.held_item_id = 42;
    imported.hot.move_ids[0] = 33;
    imported.hot.move_pp[0] = 35;
    imported.hot.move_pp_ups[0] = 0;
    imported.hot.hp_current = 30;
    imported.hot.hp_max = 35;
    imported.hot.status_flags = 0;
    imported.hot.ot_name = "ASH";
    imported.hot.tid16 = 1234;
    imported.hot.sid16 = 5678;
    imported.hot.origin_game = 3;
    imported.hot.lineage_root_species = species;
    imported.hot.identity_strength = 1;
    imported.hot.dv16 = 0x1234;
    imported.warm_json = "{\"schema_version\":1,\"source\":\"test\"}";
    imported.suspended_json = "{\"schema_version\":1}";
    return imported;
}

void applyExactIdentity(
    pr::resort::ImportedPokemon& imported,
    unsigned int pid,
    unsigned int encryption_constant,
    const std::string& home_tracker = {}) {
    imported.hot.pid = pid;
    imported.hot.encryption_constant = encryption_constant;
    if (!home_tracker.empty()) {
        imported.hot.home_tracker = home_tracker;
    }
    imported.identity.pid = imported.hot.pid;
    imported.identity.encryption_constant = imported.hot.encryption_constant;
    imported.identity.home_tracker = imported.hot.home_tracker;
    imported.identity.dv16 = imported.hot.dv16;
    imported.identity.tid16 = imported.hot.tid16;
    imported.identity.sid16 = imported.hot.sid16;
    imported.identity.ot_name = imported.hot.ot_name;
    imported.identity.lineage_root_species = imported.hot.lineage_root_species;
}

pr::resort::ImportContext placeAt(int box, int slot) {
    pr::resort::ImportContext ctx;
    ctx.profile_id = "default";
    ctx.target_location = pr::resort::BoxLocation{"default", box, slot};
    return ctx;
}

void testSha256MatchesKnownVector() {
    const std::vector<unsigned char> msg = {'a', 'b', 'c'};
    const std::string hash = pr::sha256HexLowercase(msg);
    expect(
        hash == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 incorrect");
}

void testGen12DvPatchReadAndLogFormat() {
    std::vector<unsigned char> raw(73, 0);
    const std::uint16_t dv = 0xA9F3;
    expect(pr::resort::patchPk12DvBytes(raw, "pk2", dv), "patch pk2 DV offset");
    const auto read = pr::resort::readPk12Dv16FromRaw(raw, "pk2");
    expect(read.has_value() && *read == dv, "readPk12Dv16FromRaw should match patched BE word");
    const std::string formatted = pr::resort::formatGen12Dv16ForLog(*read);
    expect(formatted.find("0xa9f3") != std::string::npos, "log format should include lowercase hex word");
    expect(formatted.find("atk/def/spd/spe=10/9/15/3") != std::string::npos, "log format should expand nibbles");
}

void testGen12PlaceholderDvGetsAssignedOnFirstResortImport() {
    const fs::path path = tempDbPath("gen12_placeholder_dv");
    pr::resort::PokemonResortService service(path);
    auto imported = makeImported(1, "Bul", kHashA, std::vector<unsigned char>(73, 0));
    imported.source_game = 41;
    imported.format_name = "pk2";
    imported.hot.origin_game = 41;
    imported.hot.dv16 = std::nullopt;
    imported.identity = {};
    const auto result = service.importParsedPokemon(imported, placeAt(0, 0));
    expect(result.success, std::string("import failed: ") + result.error);
    const auto loaded = service.getPokemonById(result.pkrid);
    expect(loaded.has_value() && loaded->hot.dv16.has_value() && *loaded->hot.dv16 != 0,
           "placeholder DV16=0 should become non-zero once stored in Resort");
}

void testGen12LegacyZeroDvExportRepairsCanonicalAndRaw() {
    const fs::path path = tempDbPath("gen12_legacy_zero_dv_export");
    std::string pkrid;
    {
        pr::resort::PokemonResortService service(path);
        auto imported = makeImported(1, "Bul", kHashA, std::vector<unsigned char>(73, 0));
        imported.source_game = 41;
        imported.format_name = "pk2";
        imported.hot.origin_game = 41;
        imported.hot.dv16 = std::nullopt;
        imported.identity = {};
        const auto result = service.importParsedPokemon(imported, placeAt(0, 0));
        expect(result.success, std::string("import failed: ") + result.error);
        pkrid = result.pkrid;
    }

    const std::vector<unsigned char> zero_raw(73, 0);
    {
        pr::resort::SqliteConnection connection(path);
        auto pokemon_stmt = connection.prepare("UPDATE pokemon SET dv16 = 0 WHERE pkrid = ?");
        pokemon_stmt.bindText(1, pkrid);
        pokemon_stmt.stepDone();

        auto snapshot_stmt = connection.prepare(
            "UPDATE pokemon_snapshots SET raw_bytes = ?, raw_hash_sha256 = ? WHERE pkrid = ? AND kind = 0");
        snapshot_stmt.bindBlob(1, zero_raw.data(), static_cast<int>(zero_raw.size()));
        snapshot_stmt.bindText(2, kHashA);
        snapshot_stmt.bindText(3, pkrid);
        snapshot_stmt.stepDone();
    }

    pr::resort::PokemonResortService service(path);
    pr::resort::ExportContext context;
    context.target_game = 41;
    context.target_format_name = "pk2";
    auto exported = service.exportPokemon(pkrid, context);
    expect(exported.success, "export failed: " + exported.error);
    const auto exported_dv = pr::resort::readPk12Dv16FromRaw(exported.raw_payload, exported.format_name);
    expect(exported_dv.has_value() && *exported_dv != 0, "legacy zero-DV export should patch outgoing raw bytes");
    const auto loaded = service.getPokemonById(pkrid);
    expect(loaded.has_value() && loaded->hot.dv16 == exported_dv,
           "legacy zero-DV export should persist repaired DV into canonical hot data");
    expect(exported.raw_hash != kHashA, "patched export should recompute the raw payload hash");
}

void testPrepareFindsSnapshotWhenExternalSaveGameIdDiffersFromImportedSnapshot() {
    const fs::path path = tempDbPath("prepare_resolves_snapshot_across_game_ids");
    pr::resort::PokemonResortService service(path);
    const auto result = service.importParsedPokemon(makeImported(25, "Pika"), placeAt(0, 0));
    expect(result.success, std::string("import failed: ") + result.error);
    // Snapshot rows store the source game (e.g. 3 for RS). Save prepare is invoked with the active
    // external save id (e.g. Gen 4) — we must still resolve the pk3 bytes before PKHeX projects them.
    const std::uint16_t active_save_game_id_other_than_import = 12;
    const auto prepared = service.prepareLatestRawSnapshotForGameWrite(
        result.pkrid, active_save_game_id_other_than_import, "pk3");
    expect(prepared.has_value(), "prepare must find latest raw snapshot when game_id is the destination save");
    expect(prepared->format_name == "pk3", "expected pk3 snapshot format");
}

void testPrepareInfersBlackTargetFormatWhenSlotCarriesOlderMirrorFormat() {
    const fs::path path = tempDbPath("prepare_black_ignores_stale_slot_format");
    pr::resort::PokemonResortService service(path);
    auto imported = makeImported(1, "Bulba", kHashA, std::vector<unsigned char>{5, 5, 5});
    imported.source_game = 21;
    imported.format_name = "pk5";
    imported.hot.origin_game = 21;
    const auto result = service.importParsedPokemon(imported, placeAt(0, 0));
    expect(result.success, std::string("import failed: ") + result.error);

    {
        pr::resort::SqliteConnection connection(path);
        pr::resort::runResortMigrations(connection);
        const std::vector<unsigned char> emerald_raw{3, 3, 3};
        auto stmt = connection.prepare(R"sql(
INSERT INTO pokemon_snapshots (
    snapshot_id, pkrid, kind, format_name, game_id, captured_at,
    raw_bytes, raw_hash_sha256, parsed_json, notes_json
) VALUES ('snap_later_pk3', ?, 2, 'pk3', 3, 9999999999, ?, ?, '{}', '{}')
)sql");
        stmt.bindText(1, result.pkrid);
        stmt.bindBlob(2, emerald_raw.data(), static_cast<int>(emerald_raw.size()));
        stmt.bindText(3, kHashB);
        stmt.stepDone();
    }

    const std::string black_storage_format = pr::resort::pkmStorageFormatNameForGameId(21);
    const auto prepared = service.prepareLatestRawSnapshotForGameWrite(
        result.pkrid,
        21,
        black_storage_format);
    expect(prepared.has_value(), "Black game write should find the native pk5 snapshot despite stale pk3 slot format");
    expect(prepared->format_name == "pk5", "Black game id should infer pk5 instead of using stale pk3 slot format");
    expect(prepared->raw_bytes == std::vector<unsigned char>({5, 5, 5}),
           "Black game write should not reuse the later Emerald mirror bytes as-is");
}

void testProjectionSourcePolicyPrefersTargetFormatThenMostAdvancedSnapshot() {
    const fs::path path = tempDbPath("projection_source_target_then_advanced");
    pr::resort::PokemonResortService service(path);
    auto imported = makeImported(1, "Bulba", kHashA, std::vector<unsigned char>{5, 5, 5});
    imported.source_game = 21;
    imported.format_name = "pk5";
    imported.hot.origin_game = 21;
    const auto result = service.importParsedPokemon(imported, placeAt(0, 0));
    expect(result.success, std::string("import failed: ") + result.error);

    {
        pr::resort::SqliteConnection connection(path);
        pr::resort::runResortMigrations(connection);
        const std::vector<unsigned char> later_pk3{3, 3, 3};
        auto stmt = connection.prepare(R"sql(
INSERT INTO pokemon_snapshots (
    snapshot_id, pkrid, kind, format_name, game_id, captured_at,
    raw_bytes, raw_hash_sha256, parsed_json, notes_json
) VALUES ('snap_later_pk3_return', ?, 2, 'pk3', 3, 9999999999, ?, ?, '{}', '{}')
)sql");
        stmt.bindText(1, result.pkrid);
        stmt.bindBlob(2, later_pk3.data(), static_cast<int>(later_pk3.size()));
        stmt.bindText(3, kHashB);
        stmt.stepDone();

        const std::vector<unsigned char> advanced_pk7{7, 7, 7};
        auto advanced_stmt = connection.prepare(R"sql(
INSERT INTO pokemon_snapshots (
    snapshot_id, pkrid, kind, format_name, game_id, captured_at,
    raw_bytes, raw_hash_sha256, parsed_json, notes_json
) VALUES ('snap_pk7_advanced', ?, 2, 'pk7', 30, 8888888888, ?, ?, '{}', '{}')
)sql");
        advanced_stmt.bindText(1, result.pkrid);
        advanced_stmt.bindBlob(2, advanced_pk7.data(), static_cast<int>(advanced_pk7.size()));
        advanced_stmt.bindText(3, kHashA);
        advanced_stmt.stepDone();
    }

    pr::resort::SqliteConnection connection(path);
    pr::resort::runResortMigrations(connection);
    pr::resort::SnapshotRepository snapshots(connection);
    const auto target_pk5 = snapshots.findLatestRawForPokemon(result.pkrid, std::nullopt, "pk5");
    expect(target_pk5.has_value(), "projection source policy should find the target/native pk5 snapshot");
    expect(target_pk5->format_name == "pk5", "projection source policy should prefer pk5 for a Black target");
    expect(target_pk5->raw_bytes == std::vector<unsigned char>({5, 5, 5}),
           "projection source policy should use the pk5 bytes as the base, not the later pk3 return");

    const auto most_advanced = snapshots.findMostAdvancedRawForPokemon(result.pkrid);
    expect(most_advanced.has_value(), "projection fallback should find a raw snapshot");
    expect(most_advanced->format_name == "pk7",
           "projection fallback should prefer the most advanced generation snapshot when no target-format snapshot exists");
}

void testGen12PrepareGameWritePatchesPayloadBeforeBridgeProjection() {
    const fs::path path = tempDbPath("gen12_prepare_write_repairs_payload");
    pr::resort::PokemonResortService service(path);
    auto imported = makeImported(1, "Bul", kHashA, std::vector<unsigned char>(73, 0));
    imported.source_game = 41;
    imported.format_name = "pk2";
    imported.hot.origin_game = 41;
    imported.hot.dv16 = std::nullopt;
    imported.identity = {};
    const auto result = service.importParsedPokemon(imported, placeAt(0, 0));
    expect(result.success, std::string("import failed: ") + result.error);

    const std::vector<unsigned char> zero_raw(73, 0);
    {
        pr::resort::SqliteConnection connection(path);
        auto pokemon_stmt = connection.prepare("UPDATE pokemon SET dv16 = 0 WHERE pkrid = ?");
        pokemon_stmt.bindText(1, result.pkrid);
        pokemon_stmt.stepDone();

        auto snapshot_stmt = connection.prepare(
            "UPDATE pokemon_snapshots SET raw_bytes = ?, raw_hash_sha256 = ? WHERE pkrid = ? AND kind = 0");
        snapshot_stmt.bindBlob(1, zero_raw.data(), static_cast<int>(zero_raw.size()));
        snapshot_stmt.bindText(2, kHashA);
        snapshot_stmt.bindText(3, result.pkrid);
        snapshot_stmt.stepDone();
    }

    const auto prepared = service.prepareLatestRawSnapshotForGameWrite(result.pkrid, 41, "pk2");
    expect(prepared.has_value(), "prepare game write should return a compatible snapshot");
    const auto prepared_dv = pr::resort::readPk12Dv16FromRaw(prepared->raw_bytes, prepared->format_name);
    expect(prepared_dv.has_value() && *prepared_dv != 0,
           "prepare game write must patch the exact payload used by bridge projection");
    expect(prepared->raw_hash_sha256 != kHashA, "prepare game write should return patched payload hash");

    const auto latest = service.getLatestRawSnapshotForPokemon(result.pkrid, 41, "pk2");
    const auto latest_dv = latest ? pr::resort::readPk12Dv16FromRaw(latest->raw_bytes, latest->format_name)
                                  : std::optional<std::uint16_t>{};
    expect(latest_dv == prepared_dv, "prepare game write should persist patched snapshot before bridge write");
    const auto loaded = service.getPokemonById(result.pkrid);
    expect(loaded.has_value() && loaded->hot.dv16 == prepared_dv,
           "prepare game write should persist the same DV into canonical hot data");
}

void testMigrationsCreateExpectedSchema() {
    const fs::path path = tempDbPath("schema");
    pr::resort::SqliteConnection connection(path);
    pr::resort::runResortMigrations(connection);
    auto stmt = connection.prepare(
        "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name IN "
        "('pokemon','boxes','box_slots','pokemon_snapshots','pokemon_history','mirror_sessions','schema_version')");
    expect(stmt.stepRow(), "schema count missing");
    expect(stmt.columnInt(0) == 7, "not all schema tables were created");
    auto idx = connection.prepare("SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND name = 'idx_box_slots_profile_pkrid'");
    expect(idx.stepRow(), "index count missing");
    expect(idx.columnInt(0) == 1, "box slot pkrid unique index missing");
    expect(scalarInt(path, "SELECT MAX(version) FROM schema_version") == pr::resort::kCurrentResortSchemaVersion,
           "schema version should match current");
    expect(scalarInt(path, "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'pid_transport_registry'") ==
               1,
           "pid_transport_registry table missing");
    expect(scalarInt(path,
                     "SELECT COUNT(*) FROM pragma_table_info('mirror_sessions') WHERE name = 'mirror_canonical_pid'") ==
               1,
           "mirror_canonical_pid column missing");
    expect(scalarInt(path,
                     "SELECT COUNT(*) FROM pragma_table_info('mirror_sessions') WHERE name = 'transport_pid'") == 1,
           "transport_pid column missing");
    expect(scalarInt(path,
                     "SELECT COUNT(*) FROM pragma_table_info('pokemon') WHERE name = 'original_pid'") == 1,
           "original_pid column missing");
    expect(scalarInt(path,
                     "SELECT COUNT(*) FROM pragma_table_info('pokemon') WHERE name = 'pid_history_json'") == 1,
           "pid_history_json column missing");
}

void testEnsureProfileIsIdempotent() {
    const fs::path path = tempDbPath("profile");
    pr::resort::PokemonResortService service(path);
    service.ensureProfile("default");
    service.ensureProfile("default");
    expect(
        scalarInt(path, "SELECT COUNT(*) FROM boxes WHERE profile_id = 'default'") == pr::resort::kDefaultResortPcBoxCount,
        "expected default Resort PC box count (matches transfer UI resort_pc_box_count)");
    expect(
        scalarInt(path, "SELECT COUNT(*) FROM box_slots WHERE profile_id = 'default'") ==
            pr::resort::kDefaultResortPcBoxCount * 30,
        "expected default slots (boxes x 30)");
}

void testListProfileBoxesMatchesBoxTable() {
    const fs::path path = tempDbPath("list_boxes");
    pr::resort::PokemonResortService service(path);
    service.ensureProfile("default");
    const auto rows = service.listProfileBoxes("default");
    expect(
        static_cast<int>(rows.size()) == pr::resort::kDefaultResortPcBoxCount,
        "listProfileBoxes should return one row per Resort PC box");
    expect(rows.front().first == 0, "first box_id should be zero");
    expect(rows.back().first == pr::resort::kDefaultResortPcBoxCount - 1, "last box_id should be count-1");
}

void testRenameResortBoxPersistsName() {
    const fs::path path = tempDbPath("rename_box");
    pr::resort::PokemonResortService service(path);
    service.ensureProfile("default");
    service.renameResortBox("default", 1, "Favorites");

    const auto rows = service.listProfileBoxes("default");
    expect(rows.size() > 1, "renameResortBox test requires at least two boxes");
    expect(rows[1].second == "Favorites", "renameResortBox should persist the box display name");
}

void testPokemonRepositoryRoundTrip() {
    const fs::path path = tempDbPath("roundtrip");
    pr::resort::SqliteConnection connection(path);
    pr::resort::runResortMigrations(connection);
    pr::resort::PokemonRepository repo(connection);

    pr::resort::ResortPokemon pokemon;
    pokemon.id.pkrid = "pkr_test";
    pokemon.id.origin_fingerprint = pr::resort::fingerprintForFirstSeenPokemon(3, "pk3", "ASH", 25, kHashA);
    pokemon.hot = makeImported(25, "Pika").hot;
    pokemon.warm.json = "{\"schema_version\":1}";
    pokemon.cold.suspended_json = "{\"schema_version\":1}";
    pokemon.created_at_unix = 100;
    pokemon.updated_at_unix = 100;
    repo.insert(pokemon);

    const auto loaded = repo.findById("pkr_test");
    expect(loaded.has_value(), "round-trip Pokemon not found");
    expect(loaded->hot.species_id == 25, "species did not round-trip");
    expect(loaded->hot.nickname == "Pika", "nickname did not round-trip");
    expect(loaded->hot.move_ids[0] == 33, "move did not round-trip");
    expect(loaded->hot.held_item_id == 42, "held item did not round-trip");
}

void testImportValidationRequiresRawPayloadAndHash() {
    const fs::path path = tempDbPath("validation");
    pr::resort::PokemonResortService service(path);

    auto missing_raw = makeImported(25, "Pika");
    missing_raw.raw_bytes.clear();
    auto result = service.importParsedPokemon(missing_raw, placeAt(0, 0));
    expect(!result.success, "import should fail without raw bytes");

    auto missing_hash = makeImported(25, "Pika");
    missing_hash.raw_hash_sha256.clear();
    result = service.importParsedPokemon(missing_hash, placeAt(0, 0));
    expect(!result.success, "import should fail without raw hash");
}

void testImportRollsBackOnPlacementFailure() {
    const fs::path path = tempDbPath("rollback");
    pr::resort::PokemonResortService service(path);
    auto result = service.importParsedPokemon(makeImported(25, "Pika"), placeAt(99, 0));
    expect(!result.success, "import should fail for invalid placement");
    expect(scalarInt(path, "SELECT COUNT(*) FROM pokemon") == 0, "pokemon row should rollback");
    expect(scalarInt(path, "SELECT COUNT(*) FROM pokemon_snapshots") == 0, "snapshot row should rollback");
    expect(scalarInt(path, "SELECT COUNT(*) FROM pokemon_history") == 0, "history row should rollback");
}

void testImportNoMatchCreatesNewCanonical() {
    const fs::path path = tempDbPath("match_no_match");
    pr::resort::PokemonResortService service(path);
    auto result = service.importParsedPokemon(makeImported(25, "Pika"), placeAt(0, 0));
    expect(result.success, "import failed: " + result.error);
    expect(result.created && !result.merged, "no-match import should report created");
    expect(result.match_reason == "no_stable_identifier_match", "unexpected no-match reason: " + result.match_reason);
    expect(scalarInt(path, "SELECT COUNT(*) FROM pokemon") == 1, "expected one canonical Pokemon");
}

void testExactMatchImportMergesInsteadOfDuplicating() {
    const fs::path path = tempDbPath("exact_match_merge");
    pr::resort::PokemonResortService service(path);
    auto first_import = makeImported(25, "Pika", kHashA, {1, 2, 3});
    applyExactIdentity(first_import, 0x12345678, 0x87654321);
    auto first = service.importParsedPokemon(first_import, placeAt(0, 0));
    expect(first.success, "first import failed: " + first.error);

    auto second_import = makeImported(25, "Pika", kHashB, {4, 5, 6});
    applyExactIdentity(second_import, 0x12345678, 0x87654321);
    second_import.hot.level = 24;
    second_import.hot.exp = 9000;
    auto second = service.importParsedPokemon(second_import, placeAt(0, 0));

    expect(second.success, "second import failed: " + second.error);
    expect(second.merged && !second.created, "exact match import should report merged");
    expect(second.pkrid == first.pkrid, "exact match should reuse canonical pkrid");
    expect(scalarInt(path, "SELECT COUNT(*) FROM pokemon") == 1, "exact match should not duplicate canonical row");
    expect(
        scalarInt(path, "SELECT COUNT(*) FROM pokemon_snapshots") == 3,
        "stable-identity merge should preserve import evidence without promoting the return raw to canonical");
    const auto loaded = service.getPokemonById(first.pkrid);
    expect(loaded.has_value(), "merged Pokemon missing");
    expect(loaded->hot.level == 24 && loaded->hot.exp == 9000, "merge did not update mutable level/exp");
    expect(loaded->revision == 2, "merge should increment revision");
}

void testHighBitPidAndEncryptionConstantStillMatch() {
    const fs::path path = tempDbPath("high_bit_identity_match");
    pr::resort::PokemonResortService service(path);
    auto first_import = makeImported(121, "Starmie", kHashA, {1, 2, 3});
    applyExactIdentity(first_import, 0xB3ADCAFEu, 0x87654321u);
    auto first = service.importParsedPokemon(first_import, placeAt(0, 0));
    expect(first.success, "first import failed: " + first.error);

    auto second_import = makeImported(121, "Starmie", kHashB, {4, 5, 6});
    applyExactIdentity(second_import, 0xB3ADCAFEu, 0x87654321u);
    second_import.hot.level = 45;
    auto second = service.importParsedPokemon(second_import, placeAt(0, 1));

    expect(second.success, "second import failed: " + second.error);
    expect(second.merged && !second.created, "high-bit pid/ec import should merge");
    expect(second.pkrid == first.pkrid, "high-bit identity should reuse canonical pkrid");
    expect(scalarInt(path, "SELECT COUNT(*) FROM pokemon") == 1, "high-bit identity should not duplicate canonical row");
}

void testMergeUpdatesMutableGameplayFields() {
    const fs::path path = tempDbPath("merge_mutable");
    pr::resort::PokemonResortService service(path);
    auto first_import = makeImported(25, "Pika", kHashA);
    applyExactIdentity(first_import, 100, 200);
    auto first = service.importParsedPokemon(first_import, placeAt(0, 0));
    expect(first.success, "first import failed: " + first.error);

    auto second_import = makeImported(26, "Raichu", kHashB, {9, 9, 9});
    applyExactIdentity(second_import, 100, 200);
    second_import.hot.level = 42;
    second_import.hot.exp = 424242;
    second_import.hot.move_ids[0] = 85;
    second_import.hot.held_item_id = 99;
    auto second = service.importParsedPokemon(second_import, placeAt(0, 0));
    expect(second.success, "second import failed: " + second.error);

    const auto loaded = service.getPokemonById(first.pkrid);
    expect(loaded.has_value(), "merged Pokemon missing");
    expect(loaded->hot.species_id == 26, "merge did not update evolved species");
    expect(loaded->hot.nickname == "Raichu", "merge did not update nickname");
    expect(loaded->hot.level == 42, "merge did not update level");
    expect(loaded->hot.move_ids[0] == 85, "merge did not update moves");
    expect(loaded->hot.held_item_id == 99, "merge did not update held item");
}

void testMergePreservesWarmAndSuspendedWhenIncomingLacksThem() {
    const fs::path path = tempDbPath("merge_preserve_warm_cold");
    pr::resort::PokemonResortService service(path);
    auto first_import = makeImported(25, "Pika", kHashA);
    applyExactIdentity(first_import, 300, 400);
    first_import.warm_json = "{\"schema_version\":1,\"memories\":{\"met\":\"forest\"},\"ribbons\":[\"champion\"]}";
    first_import.suspended_json = "{\"schema_version\":1,\"future_field\":\"kept\"}";
    auto first = service.importParsedPokemon(first_import, placeAt(0, 0));
    expect(first.success, "first import failed: " + first.error);

    auto second_import = makeImported(25, "Pika", kHashB);
    applyExactIdentity(second_import, 300, 400);
    second_import.warm_json = "{\"schema_version\":1}";
    second_import.suspended_json = "{\"schema_version\":1}";
    auto second = service.importParsedPokemon(second_import, placeAt(0, 0));
    expect(second.success, "second import failed: " + second.error);

    const auto loaded = service.getPokemonById(first.pkrid);
    expect(loaded.has_value(), "merged Pokemon missing");
    expect(loaded->warm.json.find("forest") != std::string::npos, "merge cleared existing warm data");
    expect(loaded->warm.json.find("champion") != std::string::npos, "merge cleared existing ribbons");
    expect(loaded->cold.suspended_json.find("future_field") != std::string::npos, "merge cleared suspended data");
}

void testMergeUnionsModeledWarmCollections() {
    const fs::path path = tempDbPath("merge_union_warm");
    pr::resort::PokemonResortService service(path);
    auto first_import = makeImported(25, "Pika", kHashA);
    applyExactIdentity(first_import, 500, 600);
    first_import.warm_json = "{\"schema_version\":1,\"ribbons\":[\"champion\"]}";
    auto first = service.importParsedPokemon(first_import, placeAt(0, 0));
    expect(first.success, "first import failed: " + first.error);

    auto second_import = makeImported(25, "Pika", kHashB);
    applyExactIdentity(second_import, 500, 600);
    second_import.warm_json = "{\"schema_version\":1,\"ribbons\":[\"artist\",\"champion\"],\"marks\":[\"rare\"]}";
    auto second = service.importParsedPokemon(second_import, placeAt(0, 0));
    expect(second.success, "second import failed: " + second.error);

    const auto loaded = service.getPokemonById(first.pkrid);
    expect(loaded.has_value(), "merged Pokemon missing");
    expect(loaded->warm.json.find("champion") != std::string::npos, "existing ribbon missing");
    expect(loaded->warm.json.find("artist") != std::string::npos, "incoming ribbon missing");
    expect(loaded->warm.json.find("rare") != std::string::npos, "incoming mark missing");
}

void testMatchedImportRollsBackCanonicalUpdateOnFailure() {
    const fs::path path = tempDbPath("merge_rollback");
    pr::resort::PokemonResortService service(path);
    auto first_import = makeImported(25, "Pika", kHashA);
    applyExactIdentity(first_import, 700, 800);
    auto first = service.importParsedPokemon(first_import, placeAt(0, 0));
    expect(first.success, "first import failed: " + first.error);

    auto second_import = makeImported(25, "Pika", kHashB);
    applyExactIdentity(second_import, 700, 800);
    second_import.hot.level = 99;
    auto second = service.importParsedPokemon(second_import, placeAt(99, 0));
    expect(!second.success, "matched import should fail for invalid placement");

    const auto loaded = service.getPokemonById(first.pkrid);
    expect(loaded.has_value(), "original Pokemon missing after rollback");
    expect(loaded->hot.level == 12, "failed merge should rollback canonical level update");
    expect(
        scalarInt(path, "SELECT COUNT(*) FROM pokemon_snapshots") == 2,
        "failed merge should rollback second import but keep first import+checkpoint");
    expect(scalarInt(path, "SELECT COUNT(*) FROM pokemon_history") == 2, "failed merge should rollback merge history but keep create/move history");
}

void testGen12BestEffortDoesNotClaimExactIdentity() {
    const fs::path path = tempDbPath("gen12_no_exact");
    pr::resort::PokemonResortService service(path);
    auto first_import = makeImported(25, "Pika", kHashA);
    first_import.source_game = 1;
    first_import.format_name = "pk1";
    first_import.hot.pid.reset();
    first_import.hot.encryption_constant.reset();
    first_import.identity.pid.reset();
    first_import.identity.encryption_constant.reset();
    auto first = service.importParsedPokemon(first_import, placeAt(0, 0));
    expect(first.success, "first Gen1 import failed: " + first.error);

    auto second_import = makeImported(25, "Pika", kHashB, {8, 8, 8});
    second_import.source_game = 1;
    second_import.format_name = "pk1";
    second_import.hot.pid.reset();
    second_import.hot.encryption_constant.reset();
    second_import.identity.pid.reset();
    second_import.identity.encryption_constant.reset();
    auto second = service.importParsedPokemon(second_import, placeAt(0, 1));
    expect(second.success, "second Gen1 import failed: " + second.error);
    expect(second.created && !second.merged, "Gen1 best-effort path should create until exact rules exist");
    expect(first.pkrid != second.pkrid, "Gen1 best-effort path should not silently claim exact identity");
    expect(scalarInt(path, "SELECT COUNT(*) FROM pokemon") == 2, "Gen1 best-effort imports should not merge yet");
}

void testMirrorSessionOpenQueryClose() {
    const fs::path path = tempDbPath("mirror_lifecycle");
    pr::resort::PokemonResortService service(path);
    auto import = makeImported(25, "Pika", kHashA);
    auto result = service.importParsedPokemon(import, placeAt(0, 0));
    expect(result.success, "import failed: " + result.error);

    pr::resort::MirrorOpenContext context;
    context.beacon_tid16 = 54321;
    context.beacon_ot_name = "RESORT";
    auto session = service.openMirrorSession(result.pkrid, 1, context);
    expect(!session.mirror_session_id.empty(), "mirror id missing");
    const auto active = service.getActiveMirrorForPokemon(result.pkrid);
    expect(active.has_value() && active->mirror_session_id == session.mirror_session_id, "active mirror query failed");

    service.closeMirrorSessionReturned(session.mirror_session_id);
    const auto closed = service.getMirrorSession(session.mirror_session_id);
    expect(closed.has_value(), "closed mirror missing");
    expect(closed->status == pr::resort::MirrorStatus::Returned, "mirror did not close as returned");
}

void testReturningManagedImportMatchesMirrorBeforeGenericIdentity() {
    const fs::path path = tempDbPath("mirror_return_match");
    pr::resort::PokemonResortService service(path);
    auto first_import = makeImported(25, "Pika", kHashA);
    auto created = service.importParsedPokemon(first_import, placeAt(0, 0));
    expect(created.success, "initial import failed: " + created.error);

    pr::resort::ExportContext export_context;
    export_context.target_game = 1;
    export_context.target_format_name = "pk1";
    export_context.use_gen12_beacon = true;
    auto exported = service.exportPokemon(created.pkrid, export_context);
    expect(exported.success, "export failed: " + exported.error);
    const auto active = service.getActiveMirrorForPokemon(created.pkrid);
    expect(active.has_value() && active->beacon_tid16.has_value(), "export did not open beacon mirror");

    auto returning = makeImported(25, "Pika", kHashB, {9, 8, 7});
    returning.source_game = 1;
    returning.format_name = "pk1";
    returning.hot.ot_name = *active->beacon_ot_name;
    returning.hot.tid16 = *active->beacon_tid16;
    returning.hot.sid16.reset();
    returning.hot.pid.reset();
    returning.hot.encryption_constant.reset();
    returning.hot.level = 18;
    returning.hot.exp = active->sent_exp + 1000;
    returning.identity.ot_name = returning.hot.ot_name;
    returning.identity.tid16 = returning.hot.tid16;
    returning.identity.sid16.reset();
    returning.identity.pid.reset();
    returning.identity.encryption_constant.reset();
    auto returned = service.importParsedPokemon(returning, placeAt(0, 0));

    expect(returned.success, "return import failed: " + returned.error);
    expect(returned.merged && returned.pkrid == created.pkrid, "managed return should merge into original canonical Pokemon");
    expect(returned.match_reason == "active_mirror_beacon", "mirror should win matching before generic identity");
    const auto closed = service.getMirrorSession(exported.mirror_session_id);
    expect(closed.has_value() && closed->status == pr::resort::MirrorStatus::Returned, "return import should close mirror");
    const auto loaded = service.getPokemonById(created.pkrid);
    expect(loaded.has_value() && loaded->hot.level == 18, "return merge did not update canonical level");
}

void testCrossGenExactIdentityImportPreservesCanonicalStaticFieldsWithoutActiveMirror() {
    const fs::path path = tempDbPath("cross_gen_exact_static_preserve");
    pr::resort::PokemonResortService service(path);
    auto black = makeImported(25, "Pika", kHashA, {5, 5, 5});
    black.source_game = 21;
    black.format_name = "pk5";
    black.hot.origin_game = 21;
    black.hot.met_location_id = 40001;
    black.hot.met_level = 5;
    black.hot.ball_id = 4;
    black.warm_json =
        "{\"schema_version\":1,\"source_game_key\":\"pokemon_black\","
        "\"source_context\":{\"game_key\":\"pokemon_black\"},"
        "\"resort_catalog\":{\"static_fields\":{\"origin_game\":21,\"met_location_id\":40001}}}";
    applyExactIdentity(black, 0x11223344u, 0x55667788u);

    const auto created = service.importParsedPokemon(black, placeAt(0, 0));
    expect(created.success, "initial Black import failed: " + created.error);

    auto emerald_return = makeImported(25, "Pika", kHashB, {3, 3, 3});
    emerald_return.source_game = 3;
    emerald_return.format_name = "pk3";
    emerald_return.hot.origin_game = 3;
    emerald_return.hot.met_location_id = 201;
    emerald_return.hot.met_level = 70;
    emerald_return.hot.ball_id = 1;
    emerald_return.hot.level = 100;
    emerald_return.hot.exp = black.hot.exp + 1000;
    emerald_return.warm_json =
        "{\"schema_version\":1,\"source_game_key\":\"pokemon_emerald\","
        "\"source_context\":{\"game_key\":\"pokemon_emerald\"},"
        "\"resort_catalog\":{\"static_fields\":{\"origin_game\":3,\"met_location_id\":201}}}";
    applyExactIdentity(emerald_return, 0x11223344u, 0x55667788u);
    emerald_return.identity.lineage_root_species = black.identity.lineage_root_species;

    auto ctx = placeAt(0, 1);
    ctx.placement_policy = pr::resort::BoxPlacementPolicy::ReplaceOccupied;
    const auto returned = service.importParsedPokemon(emerald_return, ctx);
    expect(returned.success, "cross-gen exact import failed: " + returned.error);
    expect(returned.pkrid == created.pkrid, "cross-gen exact import should match original canonical Pokemon");

    const auto loaded = service.getPokemonById(created.pkrid);
    expect(loaded.has_value(), "loaded Pokemon missing after cross-gen exact import");
    expect(loaded->hot.origin_game == 21, "cross-gen exact import must preserve canonical origin game");
    expect(loaded->hot.met_location_id && *loaded->hot.met_location_id == 40001,
           "cross-gen exact import must preserve canonical met location");
    expect(loaded->hot.met_level && *loaded->hot.met_level == 5,
           "cross-gen exact import must preserve canonical met level");
    expect(loaded->hot.ball_id && *loaded->hot.ball_id == 4,
           "cross-gen exact import must preserve canonical ball");
    expect(loaded->hot.exp == emerald_return.hot.exp,
           "cross-gen exact import should still update mutable EXP");
    expect(loaded->warm.json.find("pokemon_black") != std::string::npos,
           "cross-gen exact import should preserve original warm source context");
    expect(loaded->warm.json.find("pokemon_emerald") == std::string::npos,
           "cross-gen exact import should strip returning save source context from warm JSON");
}

void testPidTransportMirrorReturnPreservesCanonicalIdentity() {
    const fs::path path = tempDbPath("pid_transport_return_preserve_identity");
    pr::resort::PokemonResortService service(path);

    auto black = makeImported(25, "Pika", kHashA, {5, 5, 5});
    black.source_game = 21;
    black.format_name = "pk5";
    black.hot.origin_game = 21;
    black.hot.ot_name = "Ray";
    black.hot.tid16 = 2222;
    black.hot.sid16 = 3333;
    black.hot.language = 2;
    black.hot.met_location_id = 40001;
    black.hot.met_level = 5;
    black.hot.ball_id = 4;
    applyExactIdentity(black, 3998925365u, 0x55667788u);

    const auto created = service.importParsedPokemon(black, placeAt(0, 0));
    expect(created.success, "initial Black import failed: " + created.error);

    pr::resort::ExportContext mirror_context;
    mirror_context.target_game = 3;
    mirror_context.target_format_name = "pk3";
    const std::vector<unsigned char> prepared_pk3{3, 3, 3};
    const auto mirror = service.commitPreparedMirrorExport(
        created.pkrid,
        mirror_context,
        prepared_pk3,
        kHashB,
        "pk3",
        3998925372u);
    expect(mirror.success, "prepared mirror export failed: " + mirror.error);
    expect(mirror.transport_pid && *mirror.transport_pid == 3998925372u,
           "prepared mirror export should commit the exact transport PID from the written payload");
    expect(scalarInt(path, "SELECT COUNT(*) FROM pid_transport_registry WHERE active = 1") == 1,
           "prepared mirror export should create an active temporary PID mapping");

    auto returning = makeImported(25, "Pika", kHashB, prepared_pk3);
    returning.source_game = 3;
    returning.format_name = "pk3";
    returning.hot.origin_game = 3;
    returning.hot.ot_name = "ァグト";
    returning.hot.tid16 = 2222;
    returning.hot.sid16 = 0;
    returning.hot.pid = 3998925372u;
    returning.hot.encryption_constant = 0x11111111u;
    returning.hot.language = 1;
    returning.hot.met_location_id = 201;
    returning.hot.met_level = 70;
    returning.hot.ball_id = 1;
    returning.hot.level = 44;
    returning.hot.exp = black.hot.exp + 5000;
    returning.identity.pid = returning.hot.pid;
    returning.identity.encryption_constant = returning.hot.encryption_constant;
    returning.identity.tid16 = returning.hot.tid16;
    returning.identity.sid16 = returning.hot.sid16;
    returning.identity.ot_name = returning.hot.ot_name;
    returning.identity.lineage_root_species = black.identity.lineage_root_species;

    auto ctx = placeAt(0, 1);
    ctx.placement_policy = pr::resort::BoxPlacementPolicy::ReplaceOccupied;
    const auto returned = service.importParsedPokemon(returning, ctx);

    expect(returned.success, "transport PID return import failed: " + returned.error);
    expect(returned.merged && returned.pkrid == created.pkrid,
           "transport PID return should resolve to the existing pkrid");
    expect(returned.match_reason == "pid_transport_registry",
           "return should use temporary PID registry rather than full replacement matching");

    const auto loaded = service.getPokemonById(created.pkrid);
    expect(loaded.has_value(), "canonical missing after transport PID return");
    expect(loaded->hot.pid && *loaded->hot.pid == 3998925365u,
           "canonical PID must restore to original PID after temp PID return");
    expect(loaded->original_pid && *loaded->original_pid == 3998925365u,
           "original_pid must remain canonical");
    expect(loaded->hot.ot_name == "Ray", "mirror OT bytes must not overwrite canonical OT");
    expect(loaded->hot.sid16 && *loaded->hot.sid16 == 3333, "mirror zero SID must not overwrite canonical SID");
    expect(loaded->hot.origin_game == 21, "mirror origin game must not overwrite canonical origin");
    expect(loaded->hot.language && *loaded->hot.language == 2, "mirror language must not overwrite canonical language");
    expect(loaded->hot.met_location_id && *loaded->hot.met_location_id == 40001,
           "mirror met location must not overwrite canonical met location");
    expect(loaded->hot.ball_id && *loaded->hot.ball_id == 4, "mirror ball must not overwrite canonical ball");
    expect(loaded->hot.level == 44 && loaded->hot.exp == black.hot.exp + 5000,
           "mirror return should still merge mutable progression");

    const auto closed = service.getMirrorSession(mirror.mirror_session_id);
    expect(closed.has_value() && closed->status == pr::resort::MirrorStatus::Returned,
           "recognized mirror return should close the active mirror");
    expect(scalarInt(path, "SELECT COUNT(*) FROM pokemon_snapshots WHERE kind = 3") == 1,
           "mirror return raw must not become a new canonical checkpoint");
    expect(scalarInt(path, "SELECT COUNT(*) FROM pid_transport_registry WHERE active = 1") == 0,
           "temporary PID mapping should be inactive after recognized return");
}

void testReturningManagedImportRollbackRestoresMirrorState() {
    const fs::path path = tempDbPath("mirror_return_rollback");
    pr::resort::PokemonResortService service(path);
    auto created = service.importParsedPokemon(makeImported(25, "Pika", kHashA), placeAt(0, 0));
    expect(created.success, "initial import failed: " + created.error);

    pr::resort::ExportContext export_context;
    export_context.target_game = 1;
    export_context.target_format_name = "pk1";
    export_context.use_gen12_beacon = true;
    auto exported = service.exportPokemon(created.pkrid, export_context);
    expect(exported.success, "export failed: " + exported.error);
    auto active = service.getActiveMirrorForPokemon(created.pkrid);
    expect(active.has_value(), "active mirror missing");

    auto returning = makeImported(25, "Pika", kHashB, {5, 5, 5});
    returning.source_game = 1;
    returning.format_name = "pk1";
    returning.hot.ot_name = *active->beacon_ot_name;
    returning.hot.tid16 = *active->beacon_tid16;
    returning.hot.level = 50;
    returning.hot.exp = active->sent_exp + 5000;
    returning.identity.ot_name = returning.hot.ot_name;
    returning.identity.tid16 = returning.hot.tid16;
    auto failed = service.importParsedPokemon(returning, placeAt(99, 0));
    expect(!failed.success, "return import should fail for invalid placement");

    const auto mirror = service.getMirrorSession(exported.mirror_session_id);
    expect(mirror.has_value() && mirror->status == pr::resort::MirrorStatus::Active, "failed return should rollback mirror close");
    const auto loaded = service.getPokemonById(created.pkrid);
    expect(loaded.has_value() && loaded->hot.level == 12, "failed return should rollback canonical merge");
}

void testExportProjectionOpensMirrorAndWritesAudit() {
    const fs::path path = tempDbPath("export_projection");
    pr::resort::PokemonResortService service(path);
    auto created = service.importParsedPokemon(makeImported(25, "Pika", kHashA), placeAt(0, 0));
    expect(created.success, "import failed: " + created.error);

    pr::resort::ExportContext context;
    context.target_game = 3;
    context.target_format_name = "pk3";
    auto exported = service.exportPokemon(created.pkrid, context);
    expect(exported.success, "export failed: " + exported.error);
    expect(!exported.raw_payload.empty(), "export projection payload missing");
    expect(!exported.mirror_session_id.empty(), "export did not open mirror");
    expect(scalarInt(path, "SELECT COUNT(*) FROM pokemon_snapshots WHERE kind = 1") == 1, "export snapshot missing");
    expect(scalarInt(path, "SELECT COUNT(*) FROM pokemon_history WHERE event_type = 2") == 1, "export history missing");
    expect(scalarInt(path, "SELECT COUNT(*) FROM mirror_sessions WHERE status = 0") == 1, "active mirror missing");
}

void testSameGameExportUsesLatestRawSnapshotPayload() {
    const fs::path path = tempDbPath("same_game_export_raw");
    pr::resort::PokemonResortService service(path);
    const std::vector<unsigned char> raw{7, 6, 5, 4};
    auto imported = makeImported(25, "Pika", kHashA, raw);
    auto created = service.importParsedPokemon(imported, placeAt(0, 0));
    expect(created.success, "import failed: " + created.error);

    pr::resort::ExportContext context;
    context.target_game = imported.source_game;
    context.target_format_name = imported.format_name;
    auto exported = service.exportPokemon(created.pkrid, context);

    expect(exported.success, "export failed: " + exported.error);
    expect(exported.format_name == imported.format_name, "same-game export should preserve format");
    expect(exported.raw_payload == raw, "same-game export should reuse exact raw snapshot bytes");
    expect(exported.raw_hash == kHashA, "same-game export should reuse exact raw snapshot hash");
    expect(!exported.mirror_session_id.empty(), "same-game export should open mirror");
}

void testManagedExportMovesPokemonToOffPokemonStorage() {
    const fs::path path = tempDbPath("managed_export_unplaces");
    pr::resort::PokemonResortService service(path);
    auto created = service.importParsedPokemon(makeImported(25, "Pika", kHashA), placeAt(0, 0));
    expect(created.success, "import failed: " + created.error);
    expect(service.getPokemonLocation("default", created.pkrid).has_value(), "Pokemon should start boxed");

    pr::resort::ExportContext context;
    context.target_game = 3;
    context.target_format_name = "pk3";
    auto exported = service.exportPokemon(created.pkrid, context);

    expect(exported.success, "export failed: " + exported.error);
    expect(!service.getPokemonLocation("default", created.pkrid).has_value(),
           "active mirror Pokemon should be off-Pokemon, not in box_slots");
    expect(service.getPokemonById(created.pkrid).has_value(), "off-Pokemon canonical row should remain safe");
    expect(service.getActiveMirrorForPokemon(created.pkrid).has_value(), "off-Pokemon should have active mirror");
}

void testManagedExportRejectsSecondActiveMirror() {
    const fs::path path = tempDbPath("managed_export_active_reject");
    pr::resort::PokemonResortService service(path);
    auto created = service.importParsedPokemon(makeImported(25, "Pika", kHashA), placeAt(0, 0));
    expect(created.success, "import failed: " + created.error);

    pr::resort::ExportContext context;
    context.target_game = 3;
    context.target_format_name = "pk3";
    auto first = service.exportPokemon(created.pkrid, context);
    expect(first.success, "first export failed: " + first.error);
    auto second = service.exportPokemon(created.pkrid, context);

    expect(!second.success, "second export should reject an already-away Pokemon");
    expect(service.getActiveMirrorForPokemon(created.pkrid).has_value(), "active mirror should remain");
    expect(!service.getPokemonLocation("default", created.pkrid).has_value(), "second export should not rebox Pokemon");
}

void testManagedExportRepairsStaleActiveMirrorWhenPokemonIsBoxed() {
    const fs::path path = tempDbPath("managed_export_repairs_stale_active");
    pr::resort::PokemonResortService service(path);
    auto created = service.importParsedPokemon(makeImported(25, "Pika", kHashA), placeAt(0, 0));
    expect(created.success, "import failed: " + created.error);
    pr::resort::MirrorOpenContext mirror_context;
    auto stale = service.openMirrorSession(created.pkrid, 3, mirror_context);
    expect(service.getPokemonLocation("default", created.pkrid).has_value(), "test setup should keep Pokemon boxed");

    pr::resort::ExportContext context;
    context.target_game = 3;
    context.target_format_name = "pk3";
    auto exported = service.exportPokemon(created.pkrid, context);

    expect(exported.success, "export should repair stale active mirror: " + exported.error);
    const auto old_mirror = service.getMirrorSession(stale.mirror_session_id);
    expect(old_mirror.has_value() && old_mirror->status == pr::resort::MirrorStatus::Returned,
           "stale active mirror should be closed");
    const auto active = service.getActiveMirrorForPokemon(created.pkrid);
    expect(active.has_value() && active->mirror_session_id == exported.mirror_session_id,
           "export should open a new active mirror");
    expect(!service.getPokemonLocation("default", created.pkrid).has_value(), "export should unbox Pokemon");
}

void testRecoverPlacesOffPokemonInFirstAvailableSlot() {
    const fs::path path = tempDbPath("recover_first_available");
    pr::resort::PokemonResortService service(path);
    auto first = service.importParsedPokemon(makeImported(25, "Pika", kHashA), placeAt(0, 0));
    auto lost = service.importParsedPokemon(makeImported(133, "Eevee", kHashB), placeAt(0, 2));
    expect(first.success && lost.success, "imports failed: " + first.error + " | " + lost.error);

    pr::resort::ExportContext context;
    context.target_game = 3;
    context.target_format_name = "pk3";
    auto exported = service.exportPokemon(lost.pkrid, context);
    expect(exported.success, "export failed: " + exported.error);
    expect(!service.getPokemonLocation("default", lost.pkrid).has_value(), "export should unbox Pokemon first");

    const auto recovered = service.recoverPokemonToFirstAvailableSlot("default", lost.pkrid);

    expect(recovered.success, "recovery failed: " + recovered.error);
    expect(!recovered.already_boxed, "lost Pokemon should be newly placed");
    expect(recovered.closed_active_mirror, "recovery should close the stale active mirror");
    expect(recovered.location.box_id == 0 && recovered.location.slot_index == 1, "recovery should use first available slot");
    const auto first_location = service.getPokemonLocation("default", first.pkrid);
    expect(first_location.has_value() && first_location->slot_index == 0, "recovery overwrote occupied slot");
    const auto mirror = service.getMirrorSession(exported.mirror_session_id);
    expect(mirror.has_value() && mirror->status == pr::resort::MirrorStatus::Returned, "recovery should retire active mirror");
}

void testResetProfileWipesAllPokemonAndPlacements() {
    const fs::path path = tempDbPath("reset_profile");
    pr::resort::PokemonResortService service(path);
    auto a = service.importParsedPokemon(makeImported(25, "Pika", kHashA), placeAt(0, 0));
    auto b = service.importParsedPokemon(makeImported(133, "Eevee", kHashB), placeAt(0, 1));
    expect(a.success && b.success, "imports failed");
    expect(scalarInt(path, "SELECT COUNT(*) FROM pokemon") == 2, "expected 2 pokemon rows");
    expect(scalarInt(path, "SELECT COUNT(*) FROM box_slots WHERE pkrid IS NOT NULL") == 2, "expected 2 occupied slots");

    const auto reset = service.resetProfileToEmpty("default");
    expect(reset.success, "reset failed: " + reset.error);
    expect(scalarInt(path, "SELECT COUNT(*) FROM pokemon") == 0, "reset should wipe pokemon rows");
    expect(scalarInt(path, "SELECT COUNT(*) FROM pokemon_snapshots") == 0, "reset should wipe snapshots");
    expect(scalarInt(path, "SELECT COUNT(*) FROM pokemon_history") == 0, "reset should wipe history");
    expect(scalarInt(path, "SELECT COUNT(*) FROM mirror_sessions") == 0, "reset should wipe mirrors");
    expect(scalarInt(path, "SELECT COUNT(*) FROM box_slots WHERE pkrid IS NOT NULL") == 0, "reset should clear placements");
    expect(scalarInt(path, "SELECT COUNT(*) FROM boxes WHERE profile_id = 'default'") > 0, "reset should keep boxes");
}

void testSameGameReturnClosesActiveMirrorViaExactIdentity() {
    const fs::path path = tempDbPath("same_game_return_identity");
    pr::resort::PokemonResortService service(path);
    auto imported = makeImported(25, "Pika", kHashA);
    applyExactIdentity(imported, 123456, 654321);
    auto created = service.importParsedPokemon(imported, placeAt(0, 0));
    expect(created.success, "import failed: " + created.error);

    pr::resort::ExportContext context;
    context.target_game = imported.source_game;
    context.target_format_name = imported.format_name;
    auto exported = service.exportPokemon(created.pkrid, context);
    expect(exported.success, "export failed: " + exported.error);

    auto returning = imported;
    returning.raw_bytes = {8, 8, 8};
    returning.raw_hash_sha256 = kHashB;
    returning.hot.level = 22;
    returning.hot.exp += 2000;
    auto returned = service.importParsedPokemon(returning, placeAt(0, 0));

    expect(returned.success, "return import failed: " + returned.error);
    expect(returned.merged && returned.pkrid == created.pkrid, "same-game return should merge into canonical");
    expect(returned.match_reason == "active_mirror_pid_ec_tid_sid_ot", "active mirror should attach to exact identity match");
    const auto mirror = service.getMirrorSession(exported.mirror_session_id);
    expect(mirror.has_value() && mirror->status == pr::resort::MirrorStatus::Returned, "same-game return should close mirror");
    const auto loaded = service.getPokemonById(created.pkrid);
    expect(loaded.has_value() && loaded->hot.level == 22, "same-game return should update mutable fields");
}

void testEvolvedSameGameReturnClosesActiveMirrorViaExactIdentity() {
    const fs::path path = tempDbPath("same_game_return_evolved_identity");
    pr::resort::PokemonResortService service(path);
    auto imported = makeImported(140, "Kabuto", kHashA);
    applyExactIdentity(imported, 123456, 654321);
    imported.hot.level = 6;
    imported.hot.exp = 100;
    auto created = service.importParsedPokemon(imported, placeAt(0, 0));
    expect(created.success, "import failed: " + created.error);

    pr::resort::ExportContext context;
    context.target_game = imported.source_game;
    context.target_format_name = imported.format_name;
    auto exported = service.exportPokemon(created.pkrid, context);
    expect(exported.success, "export failed: " + exported.error);

    auto returning = imported;
    returning.raw_bytes = {8, 8, 8};
    returning.raw_hash_sha256 = kHashB;
    returning.hot.species_id = 141;
    returning.hot.lineage_root_species = 141; // Bridge currently reports current species here after evolution.
    returning.identity.lineage_root_species = 141;
    returning.hot.level = 40;
    returning.hot.exp = 200000;
    auto returned = service.importParsedPokemon(returning, placeAt(0, 0));

    expect(returned.success, "evolved return import failed: " + returned.error);
    expect(returned.merged && returned.pkrid == created.pkrid, "evolved return should merge into canonical");
    expect(returned.match_reason == "active_mirror_pid_ec_tid_sid_ot", "evolved exact identity should close active mirror");
    const auto mirror = service.getMirrorSession(exported.mirror_session_id);
    expect(mirror.has_value() && mirror->status == pr::resort::MirrorStatus::Returned, "evolved return should close mirror");
    const auto loaded = service.getPokemonById(created.pkrid);
    expect(loaded.has_value() && loaded->hot.species_id == 141 && loaded->hot.level == 40,
           "evolved return should update species and level");
    expect(loaded->hot.lineage_root_species == 140,
           "evolved return should preserve original lineage root for future fallback matching");
}

void testNativeGen12ReturnMatchesActiveMirrorByOriginalTrainerIdentity() {
    const fs::path path = tempDbPath("gen12_native_return_identity");
    pr::resort::PokemonResortService service(path);
    auto imported = makeImported(10, "Caterpie", kHashA, {1, 2, 3});
    imported.source_game = 1;
    imported.format_name = "pk1";
    imported.hot.origin_game = 1;
    imported.hot.ot_name = "ASH";
    imported.hot.tid16 = 777;
    imported.hot.sid16 = std::nullopt;
    imported.hot.level = 5;
    imported.hot.exp = 125;
    imported.hot.lineage_root_species = 10;
    imported.identity = {};
    auto created = service.importParsedPokemon(imported, placeAt(0, 0));
    expect(created.success, "native Gen 1 import failed: " + created.error);

    pr::resort::ExportContext context;
    context.target_game = imported.source_game;
    context.target_format_name = imported.format_name;
    auto exported = service.exportPokemon(created.pkrid, context);
    expect(exported.success, "native Gen 1 export failed: " + exported.error);

    auto returning = imported;
    returning.raw_bytes = {8, 8, 8};
    returning.raw_hash_sha256 = kHashB;
    returning.hot.species_id = 12;
    returning.hot.lineage_root_species = 12; // Gen 1/2 bridge reports current species here after evolution.
    returning.hot.level = 12;
    returning.hot.exp = 4096;
    returning.hot.move_ids[0] = 33;
    returning.hot.move_ids[1] = 93;
    returning.hot.move_ids[2] = 77;
    returning.hot.move_ids[3] = std::nullopt;
    auto returned = service.importParsedPokemon(returning, placeAt(0, 1));

    expect(returned.success, "native Gen 1 return import failed: " + returned.error);
    expect(returned.merged && returned.pkrid == created.pkrid,
           "native Gen 1/2 return should merge by active mirror OT/TID identity");
    expect(returned.match_reason == "active_mirror_gen12_native_beacon",
           "native Gen 1/2 return should use the dedicated active mirror matcher");
    const auto mirror = service.getMirrorSession(exported.mirror_session_id);
    expect(mirror.has_value() && mirror->status == pr::resort::MirrorStatus::Returned,
           "native Gen 1/2 return should close mirror");
    const auto loaded = service.getPokemonById(created.pkrid);
    expect(loaded.has_value() && loaded->hot.species_id == 12 && loaded->hot.level == 12,
           "native Gen 1/2 return should update evolved species and level");
    expect(loaded->hot.lineage_root_species == 10,
           "native Gen 1/2 return should preserve original lineage root");
}

void testNativeGen12ReturnRejectsAmbiguousOriginalTrainerIdentity() {
    const fs::path path = tempDbPath("gen12_native_ambiguous_identity");
    pr::resort::PokemonResortService service(path);
    auto first = makeImported(16, "Pidgey", kHashA, {1, 1, 1});
    first.source_game = 1;
    first.format_name = "pk1";
    first.hot.origin_game = 1;
    first.hot.ot_name = "ASH";
    first.hot.tid16 = 999;
    first.hot.sid16 = std::nullopt;
    first.hot.level = 5;
    first.hot.exp = 125;
    first.hot.lineage_root_species = 16;
    first.identity = {};

    auto second = first;
    second.raw_bytes = {2, 2, 2};
    second.raw_hash_sha256 = kHashB;

    const auto created_first = service.importParsedPokemon(first, placeAt(0, 0));
    expect(created_first.success, "first native Gen 1 import failed: " + created_first.error);
    const auto created_second = service.importParsedPokemon(second, placeAt(0, 1));
    expect(created_second.success, "second native Gen 1 import failed: " + created_second.error);

    pr::resort::ExportContext context;
    context.target_game = first.source_game;
    context.target_format_name = first.format_name;
    const auto exported_first = service.exportPokemon(created_first.pkrid, context);
    expect(exported_first.success, "first native Gen 1 export failed: " + exported_first.error);
    const auto exported_second = service.exportPokemon(created_second.pkrid, context);
    expect(exported_second.success, "second native Gen 1 export failed: " + exported_second.error);

    auto returning = first;
    returning.raw_bytes = {9, 9, 9};
    returning.raw_hash_sha256 = "9999999999999999999999999999999999999999999999999999999999999999";
    returning.hot.exp = 500;
    const auto returned = service.importParsedPokemon(returning, placeAt(0, 2));

    expect(returned.success, "ambiguous native Gen 1 return should still import safely: " + returned.error);
    expect(returned.created && !returned.merged,
           "ambiguous native Gen 1/2 return should not guess between active mirrors");
    expect(returned.match_reason == "gen12_active_mirror_beacon_ambiguous",
           "ambiguous native Gen 1/2 return should report ambiguity");
}

void testNativeGen12ReturnUsesDv16ToDisambiguateSimilarActiveMirrors() {
    const fs::path path = tempDbPath("gen12_native_dv_identity");
    pr::resort::PokemonResortService service(path);
    auto first = makeImported(16, "Pidgey", kHashA, {1, 1, 1});
    first.source_game = 2;
    first.format_name = "pk2";
    first.hot.origin_game = 2;
    first.hot.ot_name = "ASH";
    first.hot.tid16 = 999;
    first.hot.sid16 = std::nullopt;
    first.hot.level = 5;
    first.hot.exp = 125;
    first.hot.lineage_root_species = 16;
    first.hot.dv16 = 0x1111;
    first.identity = {};

    auto second = first;
    second.raw_bytes = {2, 2, 2};
    second.raw_hash_sha256 = kHashB;
    second.hot.dv16 = 0x2222;

    const auto created_first = service.importParsedPokemon(first, placeAt(0, 0));
    expect(created_first.success, "first native Gen 2 import failed: " + created_first.error);
    const auto created_second = service.importParsedPokemon(second, placeAt(0, 1));
    expect(created_second.success, "second native Gen 2 import failed: " + created_second.error);

    pr::resort::ExportContext context;
    context.target_game = first.source_game;
    context.target_format_name = first.format_name;
    const auto exported_first = service.exportPokemon(created_first.pkrid, context);
    expect(exported_first.success, "first native Gen 2 export failed: " + exported_first.error);
    const auto exported_second = service.exportPokemon(created_second.pkrid, context);
    expect(exported_second.success, "second native Gen 2 export failed: " + exported_second.error);

    auto returning = second;
    returning.raw_bytes = {9, 9, 9};
    returning.raw_hash_sha256 = "9999999999999999999999999999999999999999999999999999999999999999";
    returning.hot.exp = 500;
    returning.hot.level = 9;
    const auto returned = service.importParsedPokemon(returning, placeAt(0, 2));

    expect(returned.success, "DV-assisted native Gen 2 return should import: " + returned.error);
    expect(returned.merged && returned.pkrid == created_second.pkrid,
           "DV16 should disambiguate otherwise similar active Gen 1/2 mirrors");
    expect(returned.match_reason == "active_mirror_gen12_native_beacon",
           "DV-assisted native Gen 1/2 return should still use active mirror matcher");
    const auto first_mirror = service.getMirrorSession(exported_first.mirror_session_id);
    const auto second_mirror = service.getMirrorSession(exported_second.mirror_session_id);
    expect(first_mirror.has_value() && first_mirror->status == pr::resort::MirrorStatus::Active,
           "non-matching DV mirror should remain active");
    expect(second_mirror.has_value() && second_mirror->status == pr::resort::MirrorStatus::Returned,
           "matching DV mirror should close as returned");
}

void testCanonicalPreservedDataSurvivesOlderProjection() {
    const fs::path path = tempDbPath("export_preserve");
    pr::resort::PokemonResortService service(path);
    auto imported = makeImported(25, "Pika", kHashA);
    imported.warm_json = "{\"schema_version\":1,\"memories\":{\"gen6\":\"kept\"},\"marks\":[\"rare\"]}";
    imported.suspended_json = "{\"schema_version\":1,\"future\":\"kept\"}";
    auto created = service.importParsedPokemon(imported, placeAt(0, 0));
    expect(created.success, "import failed: " + created.error);

    pr::resort::ExportContext context;
    context.target_game = 1;
    context.target_format_name = "pk1";
    context.use_gen12_beacon = true;
    auto exported = service.exportPokemon(created.pkrid, context);
    expect(exported.success, "export failed: " + exported.error);

    const auto loaded = service.getPokemonById(created.pkrid);
    expect(loaded.has_value(), "canonical missing after export");
    expect(loaded->warm.json.find("gen6") != std::string::npos, "older projection cleared warm data");
    expect(loaded->cold.suspended_json.find("future") != std::string::npos, "older projection cleared cold data");
}

void testEndToEndExportReturnFlow() {
    const fs::path path = tempDbPath("e2e_export_return");
    pr::resort::PokemonResortService service(path);
    auto created = service.importParsedPokemon(makeImported(25, "Pika", kHashA), placeAt(0, 0));
    expect(created.success, "import failed: " + created.error);

    pr::resort::ExportContext export_context;
    export_context.target_game = 1;
    export_context.target_format_name = "pk1";
    export_context.use_gen12_beacon = true;
    auto exported = service.exportPokemon(created.pkrid, export_context);
    expect(exported.success, "export failed: " + exported.error);
    auto active = service.getActiveMirrorForPokemon(created.pkrid);
    expect(active.has_value(), "active mirror missing");

    auto returning = makeImported(25, "Pika", kHashB, {1, 9, 9});
    returning.source_game = 1;
    returning.format_name = "pk1";
    returning.hot.ot_name = *active->beacon_ot_name;
    returning.hot.tid16 = *active->beacon_tid16;
    returning.hot.level = 30;
    returning.hot.exp = active->sent_exp + 9999;
    returning.identity.ot_name = returning.hot.ot_name;
    returning.identity.tid16 = returning.hot.tid16;
    auto returned = service.importParsedPokemon(returning, placeAt(0, 0));
    expect(returned.success, "return import failed: " + returned.error);

    expect(scalarInt(path, "SELECT COUNT(*) FROM pokemon") == 1, "e2e flow duplicated canonical Pokemon");
    expect(
        scalarInt(path, "SELECT COUNT(*) FROM pokemon_snapshots") == 4,
        "e2e: import+checkpoint, export projection, return evidence only");
    const auto mirror = service.getMirrorSession(exported.mirror_session_id);
    expect(mirror.has_value() && mirror->status == pr::resort::MirrorStatus::Returned, "e2e mirror not returned");
    const auto loaded = service.getPokemonById(created.pkrid);
    expect(loaded.has_value() && loaded->hot.level == 30, "e2e return did not update canonical");
}

void testBackendSeedToolCreatesPokemon() {
    const fs::path path = tempDbPath("seed_tool");
    const fs::path tool = backendToolPath();
    const std::string command = "\"" + tool.string() + "\" seed --db \"" + path.string() + "\" --species 25 --nickname Toolchu --box 0 --slot 0";
    const int rc = std::system(command.c_str());
    expect(rc == 0, "seed tool command failed");
    expect(scalarInt(path, "SELECT COUNT(*) FROM pokemon") == 1, "seed tool did not create canonical Pokemon");
    expect(scalarText(path, "SELECT nickname FROM pokemon LIMIT 1") == "Toolchu", "seed tool nickname wrong");
}

void testRejectOccupiedPlacementPolicy() {
    const fs::path path = tempDbPath("reject_occupied");
    pr::resort::PokemonResortService service(path);
    auto first = service.importParsedPokemon(makeImported(25, "Pika", kHashA), placeAt(0, 0));
    expect(first.success, "first import failed: " + first.error);
    auto second = service.importParsedPokemon(makeImported(133, "Eevee", kHashB), placeAt(0, 0));
    expect(!second.success, "second import should reject occupied slot");
    expect(scalarInt(path, "SELECT COUNT(*) FROM pokemon") == 1, "rejected import should rollback canonical row");
    const auto views = service.getBoxSlotViews("default", 0);
    expect(views.size() == 1 && views[0].pkrid == first.pkrid, "occupied slot should still contain first Pokemon");
}

void testReplaceOccupiedPlacementPolicy() {
    const fs::path path = tempDbPath("replace_occupied");
    pr::resort::PokemonResortService service(path);
    auto first = service.importParsedPokemon(makeImported(25, "Pika", kHashA), placeAt(0, 0));
    expect(first.success, "first import failed: " + first.error);
    auto ctx = placeAt(0, 0);
    ctx.placement_policy = pr::resort::BoxPlacementPolicy::ReplaceOccupied;
    auto second = service.importParsedPokemon(makeImported(133, "Eevee", kHashB), ctx);
    expect(second.success, "replace import failed");
    const auto views = service.getBoxSlotViews("default", 0);
    expect(views.size() == 1 && views[0].pkrid == second.pkrid, "slot should contain replacement Pokemon");
    expect(!service.getPokemonLocation("default", first.pkrid).has_value(), "first Pokemon should be unplaced after replacement");
}

void testOnePokemonCannotOccupyTwoSlots() {
    const fs::path path = tempDbPath("one_location");
    pr::resort::PokemonResortService service(path);
    auto result = service.importParsedPokemon(makeImported(25, "Pika", kHashA), placeAt(0, 0));
    expect(result.success, "import failed: " + result.error);

    pr::resort::SqliteConnection connection(path);
    pr::resort::runResortMigrations(connection);
    pr::resort::BoxRepository boxes(connection);
    boxes.placePokemon(pr::resort::BoxLocation{"default", 0, 1}, result.pkrid, pr::resort::BoxPlacementPolicy::RejectIfOccupied);
    expect(scalarInt(path, "SELECT COUNT(*) FROM box_slots WHERE pkrid IS NOT NULL") == 1, "Pokemon should occupy only one slot");
    const auto location = service.getPokemonLocation("default", result.pkrid);
    expect(location.has_value() && location->slot_index == 1, "Pokemon should move to the new slot");
}

void testSlotViewOrderingAndLightweightData() {
    const fs::path path = tempDbPath("slot_views");
    pr::resort::PokemonResortService service(path);
    auto later = service.importParsedPokemon(makeImported(133, "Eevee", kHashB), placeAt(0, 2));
    auto imported = makeImported(25, "Pika", kHashA);
    imported.warm_json =
        "{\"schema_version\":1,\"source_context\":{\"schema_version\":1,\"game_key\":\"pokemon_heartgold\"},"
        "\"species_slug\":\"pikachu\",\"species_name\":\"Pikachu\",\"form_key\":\"$\","
        "\"held_item_name\":\"Light Ball\",\"nature\":\"Jolly\",\"ability_name\":\"Static\","
        "\"primary_type\":\"Electric\",\"secondary_type\":\"Flying\",\"tera_type\":\"Electric\","
        "\"mark_icon\":\"lunch-time\",\"pokerus_status\":\"infected\",\"is_alpha\":true,"
        "\"is_gigantamax\":true,\"markings\":3}";
    auto first = service.importParsedPokemon(imported, placeAt(0, 0));
    expect(later.success && first.success, "imports failed: " + later.error + " | " + first.error);
    const auto views = service.getBoxSlotViews("default", 0);
    expect(views.size() == 2, "expected two occupied slot views");
    expect(views[0].slot_index == 0 && views[0].species_id == 25, "slot view 0 wrong");
    expect(views[1].slot_index == 2 && views[1].species_id == 133, "slot view 1 wrong");
    expect(views[0].display_name == "Pika", "slot view display name wrong");
    expect(views[0].source_game_key == "pokemon_heartgold",
           "slot views should expose the concrete source game key stored in Resort warm metadata");
    expect(views[0].species_slug == "pikachu" && views[0].species_name == "Pikachu",
           "slot views should expose species metadata stored in Resort warm metadata");
    expect(views[0].held_item_name == "Light Ball" && views[0].nature == "Jolly",
           "slot views should expose held item and nature metadata stored in Resort warm metadata");
    expect(views[0].ability_name == "Static",
           "slot views should expose ability names stored in Resort warm metadata");
    expect(views[0].primary_type == "Electric" && views[0].secondary_type == "Flying",
           "slot views should expose type names stored in Resort warm metadata");
    expect(views[0].tera_type == "Electric" && views[0].mark_icon == "lunch-time",
           "slot views should expose tera type and mark metadata stored in Resort warm metadata");
    expect(views[0].pokerus_status == "infected" && views[0].is_alpha && views[0].is_gigantamax,
           "slot views should expose special feature metadata stored in Resort warm metadata");
    expect(views[0].markings == 3, "slot views should expose marking metadata stored in Resort warm metadata");
}

void testSnapshotPreservesExactRawPayload() {
    const fs::path path = tempDbPath("snapshot");
    pr::resort::PokemonResortService service(path);
    std::vector<unsigned char> raw{0, 1, 2, 3, 255};
    auto result = service.importParsedPokemon(makeImported(25, "Pika", kHashA, raw), placeAt(0, 0));
    expect(result.success, "import failed: " + result.error);
    const std::string blob = scalarBlob(path, "SELECT raw_bytes FROM pokemon_snapshots LIMIT 1");
    expect(blob.size() == raw.size(), "snapshot raw size wrong");
    for (std::size_t i = 0; i < raw.size(); ++i) {
        expect(static_cast<unsigned char>(blob[i]) == raw[i], "snapshot raw byte mismatch");
    }
}

void testBridgeImportAdapterRequiresImportGradePayload() {
    const auto summary = pr::resort::parseBridgeImportPayload(R"json({"success":true,"party":["pikachu"]})json");
    expect(!summary.success, "summary payload must not parse as import-grade data");

    const auto parsed = pr::resort::parseBridgeImportPayload(R"json({
        "bridge_import_schema": 1,
        "pokemon": [{
            "source_game": 3,
            "format_name": "pk3",
            "raw_payload_base64": "AQIDBA==",
            "raw_hash_sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
            "hot": {
                "species_id": 25,
                "form_id": 0,
                "nickname": "Pika",
                "is_nicknamed": true,
                "level": 12,
                "exp": 1234,
                "gender": 1,
                "shiny": false,
                "hp_current": 30,
                "hp_max": 35,
                "ot_name": "ASH",
                "origin_game": 3,
                "dv16": 4660,
                "lineage_root_species": 25,
                "moves": [{"move_id": 33, "pp": 35, "pp_ups": 0}]
            },
            "warm_json": "{\"schema_version\":1}",
            "suspended_json": "{\"schema_version\":1}"
        }]
    })json");
    expect(parsed.success, parsed.error);
    expect(parsed.pokemon.size() == 1, "expected one parsed Pokemon");
    expect(parsed.pokemon[0].raw_bytes.size() == 4, "base64 raw payload size wrong");
    expect(parsed.pokemon[0].hot.species_id == 25, "adapter species wrong");
    expect(parsed.pokemon[0].hot.dv16 == 4660, "adapter DV16 wrong");
    expect(parsed.pokemon[0].hot.move_ids[0] == 33, "adapter move wrong");
}

void testBackendBridgeImportFromRealSave() {
    const fs::path root = repositoryRoot();
    const fs::path save = root / "tests" / "test-data" / "saves" / "pokemon blue - ASH.sav";
    const fs::path path = tempDbPath("bridge_real_save");
    pr::resort::PokemonResortService service(path);
    pr::resort::BridgeImportService bridge(service, root.string(), nullptr);
    pr::resort::BridgeSaveImportOptions options;
    options.profile_id = "default";
    options.max_pokemon = 1;

    const auto result = bridge.importSave(save.string(), options);

    expect(result.success, "bridge backend import failed: " + result.error);
    expect(result.imported_count == 1, "expected one imported Pokemon");
    expect(scalarInt(path, "SELECT COUNT(*) FROM pokemon") == 1, "expected one canonical Pokemon");
    expect(scalarInt(path, "SELECT COUNT(*) FROM pokemon_snapshots") == 2, "import evidence + canonical checkpoint");
    const std::string raw = scalarBlob(
        path,
        "SELECT raw_bytes FROM pokemon_snapshots WHERE kind = 0 ORDER BY captured_at DESC LIMIT 1");
    expect(!raw.empty(), "bridge-imported snapshot raw payload is empty");
    const auto views = service.getBoxSlotViews("default", 0);
    expect(views.size() == 1, "bridge-imported Pokemon should be placed in box 0");
}

void testOpenHomePayloadCoexistsWithResortMetadata() {
    pr::resort::ResortPokemonRecord record;
    record.openhome_id = "0025-04d2162e-78563412-03";
    record.legacy_canonical.id.pkrid = record.openhome_id;
    record.openhome_payload.openhome_format_version = "OHPKM";
    record.openhome_payload.openhome_id = record.openhome_id;
    record.openhome_payload.serialized_identity_or_ohpkm = {0x4f, 0x48, 0x50, 0x4b, 0x4d};
    record.memories_json = "{\"first_resort_memory\":\"arrived\"}";
    record.visited_games.push_back("emerald");
    record.relationships_json = "{\"buddy\":\"azurill\"}";

    expect(record.hasOpenHomePayload(), "record should retain opaque OpenHome OHPKM bytes");
    expect(record.openhome_payload.hasIdentityKey(), "record should expose OpenHome identity key");
    expect(record.visibleInNormalBoxes(), "available record should be visible in normal boxes");
    expect(record.memories_json.find("arrived") != std::string::npos, "Resort memories should stay outside OpenHome payload");
}

void testPresenceStateHidesAwayPokemonButKeepsInternalLookup() {
    pr::resort::ResortPokemonRecord record;
    record.openhome_id = "0001-00010002-00000003-03";
    record.legacy_canonical.id.pkrid = record.openhome_id;
    record.openhome_payload.openhome_id = record.openhome_id;
    record.openhome_payload.serialized_identity_or_ohpkm = {1, 2, 3};
    record.presence = pr::resort::PokemonPresenceState::AwayInGame;
    record.away_location.game_id = "emerald";
    record.away_location.save_id = "emerald-main";
    record.away_location.generation = "3";
    record.away_location.export_session_id = "mirror_session_1";

    expect(!record.visibleInNormalBoxes(), "away Pokemon should be excluded from normal box display");
    expect(record.hasOpenHomePayload(), "away Pokemon should keep canonical OpenHome payload");
    expect(record.openhome_id == "0001-00010002-00000003-03",
           "away Pokemon should remain internally addressable by OpenHome ID");

    record.presence = pr::resort::PokemonPresenceState::AvailableInResort;
    expect(record.visibleInNormalBoxes(), "returned Pokemon should become visible again");
    expect(record.openhome_id == "0001-00010002-00000003-03",
           "return should not create a duplicate OpenHome ID");
    expect(record.memories_json == "{}", "Resort metadata should survive presence transitions");
}

void testOpenHomeStorageBridgeWritesOhpkmStoreAndBanksByOpenHomeId() {
    const fs::path root = fs::temp_directory_path() / "pokemon_resort_openhome_bridge";
    fs::remove_all(root);
    pr::resort::openhome::OpenHomeStorageBridge bridge(root);

    pr::resort::openhome::OpenHomePokemonPayload payload;
    payload.openhome_id = "0025-04d2162e-78563412-03";
    payload.openhome_format_version = "OHPKM";
    payload.serialized_identity_or_ohpkm = {0x4f, 0x48, 0x50, 0x4b, 0x4d};

    bridge.upsertOhpkm(payload);
    bridge.placeInHomeBox(payload.openhome_id, 0, 0, 7);

    const auto stored = bridge.loadOhpkmStore();
    expect(stored.size() == 1, "OpenHome bridge should load one OHPKM payload");
    expect(stored[0].openhome_id == payload.openhome_id, "OHPKM store should be keyed by OpenHome ID");
    expect(stored[0].payload.serialized_identity_or_ohpkm == payload.serialized_identity_or_ohpkm,
           "OHPKM bytes should round trip through bridge storage");

    const auto banks = bridge.loadHomeBanks();
    expect(!banks.banks.empty() && !banks.banks[0].boxes.empty(), "OpenHome banks should exist");
    const auto slot = banks.banks[0].boxes[0].identifiers_by_slot.find(7);
    expect(slot != banks.banks[0].boxes[0].identifiers_by_slot.end(), "OpenHome box slot should be occupied");
    expect(slot->second == payload.openhome_id, "OpenHome box slot should store OpenHome ID, not pkrid");

    bridge.placeInHomeBox(payload.openhome_id, 0, 1, 3);
    const auto moved = bridge.loadHomeBanks();
    expect(moved.banks[0].boxes[0].identifiers_by_slot.empty(),
           "moving an OpenHome ID should remove the old box placement");
    expect(moved.banks[0].boxes[1].identifiers_by_slot.at(3) == payload.openhome_id,
           "moving an OpenHome ID should write the new placement");

    expect(bridge.removeOhpkm(payload.openhome_id), "OpenHome bridge should delete OHPKM payload by OpenHome ID");
    expect(bridge.loadOhpkmStore().empty(), "OpenHome bridge payload store should be empty after delete");
    fs::remove_all(root);
}

void testOpenHomePullToHomePlanMatchesUiTrackingFlow() {
    pr::resort::openhome::OpenHomePullToHomeRequest request;
    request.source.save_path = "/tmp/emerald.sav";
    request.source.save_type = "SAV3";
    request.source.box = 0;
    request.source.slot = 4;
    request.destination.bank = 0;
    request.destination.box = 2;
    request.destination.slot = 17;

    const auto steps = pr::resort::openhome::planOpenHomePullToHome(request);
    const std::vector<pr::resort::openhome::OpenHomeMovementStep> expected{
        pr::resort::openhome::OpenHomeMovementStep::LoadSourceSave,
        pr::resort::openhome::OpenHomeMovementStep::SyncTrackedPokemonWithSaveData,
        pr::resort::openhome::OpenHomeMovementStep::LoadTrackedPokemonOrStartTracking,
        pr::resort::openhome::OpenHomeMovementStep::ClearSourceSaveSlot,
        pr::resort::openhome::OpenHomeMovementStep::UpsertOhpkmStore,
        pr::resort::openhome::OpenHomeMovementStep::PlaceOpenHomeIdInHomeBank,
        pr::resort::openhome::OpenHomeMovementStep::WriteOpenHomeBanks,
        pr::resort::openhome::OpenHomeMovementStep::PrepareSaveWriter,
        pr::resort::openhome::OpenHomeMovementStep::WriteSaveFile,
    };

    expect(steps == expected, "OpenHome pull-to-home plan should mirror moveMonToHome/moveBoxToBank UI flow");
    expect(
        std::string(pr::resort::openhome::openHomeMovementStepName(steps[2])) ==
            "load_tracked_pokemon_or_start_tracking",
        "movement step names should be stable for bridge diagnostics");
}

void testOpenHomePushToGamePlanUsesOpenHomeIdAndSaveWriter() {
    pr::resort::openhome::OpenHomePushToGameRequest request;
    request.openhome_id = "0025-04d2162e-78563412-03";
    request.destination.save_path = "/tmp/platinum.sav";
    request.destination.save_type = "SAV4";
    request.destination.box = 1;
    request.destination.slot = 9;

    const auto steps = pr::resort::openhome::planOpenHomePushToGame(request);
    const std::vector<pr::resort::openhome::OpenHomeMovementStep> expected{
        pr::resort::openhome::OpenHomeMovementStep::LoadTargetSave,
        pr::resort::openhome::OpenHomeMovementStep::LoadTrackedPokemonOrStartTracking,
        pr::resort::openhome::OpenHomeMovementStep::ConvertOhpkmForTargetSave,
        pr::resort::openhome::OpenHomeMovementStep::WriteTargetSaveSlot,
        pr::resort::openhome::OpenHomeMovementStep::UpsertOhpkmStore,
        pr::resort::openhome::OpenHomeMovementStep::PrepareSaveWriter,
        pr::resort::openhome::OpenHomeMovementStep::WriteSaveFile,
    };

    expect(steps == expected, "OpenHome push-to-game plan should mirror moveOhpkmToSave UI flow");
    expect(request.openhome_id.find("PKR") == std::string::npos, "OpenHome push should use OpenHome ID, not pkrid");
}

void testOpenHomeMoveBetweenGamesPlanDoesNotPromotePkFilesToCanonical() {
    pr::resort::openhome::OpenHomeMoveBetweenGamesRequest request;
    request.source.save_path = "/tmp/emerald.sav";
    request.source.save_type = "SAV3";
    request.source.box = 0;
    request.source.slot = 1;
    request.destination.save_path = "/tmp/black.sav";
    request.destination.save_type = "SAV5";
    request.destination.box = 3;
    request.destination.slot = 2;

    const auto steps = pr::resort::openhome::planOpenHomeMoveBetweenGames(request);
    expect(
        std::find(
            steps.begin(),
            steps.end(),
            pr::resort::openhome::OpenHomeMovementStep::ConvertOhpkmForTargetSave) != steps.end(),
        "game-to-game moves should project from OHPKM, not use PK files as canonical records");
    expect(
        std::find(
            steps.begin(),
            steps.end(),
            pr::resort::openhome::OpenHomeMovementStep::UpsertOhpkmStore) != steps.end(),
        "game-to-game moves should update the OpenHome OHPKM store");
}

void testManagedExportPresenceMapsToAwayAndKeepsCanonicalPayload() {
    const fs::path path = tempDbPath("openhome_presence_export");
    pr::resort::PokemonResortService service(path);
    auto created = service.importParsedPokemon(makeImported(25, "Pika", kHashA), placeAt(0, 0));
    expect(created.success, "import failed: " + created.error);

    pr::resort::ResortPokemonRecord record;
    record.openhome_payload.openhome_format_version = "OHPKM";
    record.openhome_id = "0025-04d2162e-78563412-03";
    record.openhome_payload.openhome_id = record.openhome_id;
    record.openhome_payload.serialized_identity_or_ohpkm = {0xaa, 0xbb, 0xcc};
    record.memories_json = "{\"resort_memory\":\"met at the dock\"}";

    record.presence = pr::resort::inferPresenceFromPlacement(
        service.getPokemonLocation("default", created.pkrid).has_value(),
        service.getActiveMirrorForPokemon(created.pkrid).has_value());
    expect(record.presence == pr::resort::PokemonPresenceState::AvailableInResort,
           "boxed Pokemon should start available");

    pr::resort::ExportContext context;
    context.target_game = 3;
    context.target_format_name = "pk3";
    auto exported = service.exportPokemon(created.pkrid, context);
    expect(exported.success, "export failed: " + exported.error);

    record.presence = pr::resort::inferPresenceFromPlacement(
        service.getPokemonLocation("default", created.pkrid).has_value(),
        service.getActiveMirrorForPokemon(created.pkrid).has_value());
    record.away_location.game_id = std::to_string(context.target_game);
    record.away_location.generation = "3";
    record.away_location.export_session_id = exported.mirror_session_id;

    expect(record.presence == pr::resort::PokemonPresenceState::AwayInGame,
           "managed export should map to away presence");
    expect(!record.visibleInNormalBoxes(), "away presence should hide normal box rows");
    expect(service.getPokemonById(created.pkrid).has_value(), "export must keep canonical row internally accessible");
    expect(record.hasOpenHomePayload(), "export should not delete OpenHome payload");
    expect(record.memories_json.find("dock") != std::string::npos, "Resort memories should survive move");
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests{
        {"migrations create expected schema", testMigrationsCreateExpectedSchema},
        {"OpenHome payload coexists with Resort metadata", testOpenHomePayloadCoexistsWithResortMetadata},
        {"presence state hides away Pokemon but keeps internal lookup", testPresenceStateHidesAwayPokemonButKeepsInternalLookup},
        {"OpenHome storage bridge writes OHPKM store and banks by OpenHome ID",
         testOpenHomeStorageBridgeWritesOhpkmStoreAndBanksByOpenHomeId},
        {"OpenHome pull-to-home plan matches UI tracking flow", testOpenHomePullToHomePlanMatchesUiTrackingFlow},
        {"OpenHome push-to-game plan uses OpenHome ID and save writer", testOpenHomePushToGamePlanUsesOpenHomeIdAndSaveWriter},
        {"OpenHome move-between-games plan does not promote PK files to canonical",
         testOpenHomeMoveBetweenGamesPlanDoesNotPromotePkFilesToCanonical},
        {"managed export presence maps to away and keeps canonical payload",
         testManagedExportPresenceMapsToAwayAndKeepsCanonicalPayload},
        {"ensureProfile is idempotent", testEnsureProfileIsIdempotent},
        {"listProfileBoxes matches boxes table", testListProfileBoxesMatchesBoxTable},
        {"rename Resort box persists name", testRenameResortBoxPersistsName},
        {"pokemon repository round trip", testPokemonRepositoryRoundTrip},
        {"import validation requires raw payload and hash", testImportValidationRequiresRawPayloadAndHash},
        {"import rolls back on placement failure", testImportRollsBackOnPlacementFailure},
        {"import no match creates new canonical", testImportNoMatchCreatesNewCanonical},
        {"exact match import merges instead of duplicating", testExactMatchImportMergesInsteadOfDuplicating},
        {"high-bit pid and encryption constant still match", testHighBitPidAndEncryptionConstantStillMatch},
        {"merge updates mutable gameplay fields", testMergeUpdatesMutableGameplayFields},
        {"merge preserves warm and suspended when incoming lacks them", testMergePreservesWarmAndSuspendedWhenIncomingLacksThem},
        {"merge unions modeled warm collections", testMergeUnionsModeledWarmCollections},
        {"matched import rolls back canonical update on failure", testMatchedImportRollsBackCanonicalUpdateOnFailure},
        {"Gen1/2 best-effort does not claim exact identity", testGen12BestEffortDoesNotClaimExactIdentity},
        {"mirror session open query close", testMirrorSessionOpenQueryClose},
        {"returning managed import matches mirror before generic identity", testReturningManagedImportMatchesMirrorBeforeGenericIdentity},
        {"cross-gen exact identity import preserves canonical static fields without active mirror",
         testCrossGenExactIdentityImportPreservesCanonicalStaticFieldsWithoutActiveMirror},
        {"PID transport mirror return preserves canonical identity",
         testPidTransportMirrorReturnPreservesCanonicalIdentity},
        {"returning managed import rollback restores mirror state", testReturningManagedImportRollbackRestoresMirrorState},
        {"export projection opens mirror and writes audit", testExportProjectionOpensMirrorAndWritesAudit},
        {"same-game export uses latest raw snapshot payload", testSameGameExportUsesLatestRawSnapshotPayload},
        {"managed export moves Pokemon to off-Pokemon storage", testManagedExportMovesPokemonToOffPokemonStorage},
        {"managed export rejects second active mirror", testManagedExportRejectsSecondActiveMirror},
        {"managed export repairs stale active mirror when Pokemon is boxed", testManagedExportRepairsStaleActiveMirrorWhenPokemonIsBoxed},
        {"recover places off-Pokemon in first available slot", testRecoverPlacesOffPokemonInFirstAvailableSlot},
        {"reset profile wipes all pokemon and placements", testResetProfileWipesAllPokemonAndPlacements},
        {"same-game return closes active mirror via exact identity", testSameGameReturnClosesActiveMirrorViaExactIdentity},
        {"evolved same-game return closes active mirror via exact identity", testEvolvedSameGameReturnClosesActiveMirrorViaExactIdentity},
        {"native Gen1/2 return matches active mirror by original trainer identity", testNativeGen12ReturnMatchesActiveMirrorByOriginalTrainerIdentity},
        {"native Gen1/2 return rejects ambiguous original trainer identity", testNativeGen12ReturnRejectsAmbiguousOriginalTrainerIdentity},
        {"native Gen1/2 return uses DV16 to disambiguate similar active mirrors", testNativeGen12ReturnUsesDv16ToDisambiguateSimilarActiveMirrors},
        {"SHA-256 helper matches known vector", testSha256MatchesKnownVector},
        {"Gen12 DV patch/read and log format", testGen12DvPatchReadAndLogFormat},
        {"Gen12 placeholder DV gets assigned on first Resort import", testGen12PlaceholderDvGetsAssignedOnFirstResortImport},
        {"Gen12 legacy zero-DV export repairs canonical and raw", testGen12LegacyZeroDvExportRepairsCanonicalAndRaw},
        {"prepare resolves snapshot when external save game id differs from import",
         testPrepareFindsSnapshotWhenExternalSaveGameIdDiffersFromImportedSnapshot},
        {"prepare infers Black target format when slot carries older mirror format",
         testPrepareInfersBlackTargetFormatWhenSlotCarriesOlderMirrorFormat},
        {"projection source policy prefers target format then most advanced snapshot",
         testProjectionSourcePolicyPrefersTargetFormatThenMostAdvancedSnapshot},
        {"Gen12 prepare game write patches payload before bridge projection", testGen12PrepareGameWritePatchesPayloadBeforeBridgeProjection},
        {"canonical preserved data survives older projection", testCanonicalPreservedDataSurvivesOlderProjection},
        {"end-to-end export return flow", testEndToEndExportReturnFlow},
        {"backend seed tool creates Pokemon", testBackendSeedToolCreatesPokemon},
        {"reject occupied placement policy", testRejectOccupiedPlacementPolicy},
        {"replace occupied placement policy", testReplaceOccupiedPlacementPolicy},
        {"one Pokemon cannot occupy two slots", testOnePokemonCannotOccupyTwoSlots},
        {"slot view ordering and lightweight data", testSlotViewOrderingAndLightweightData},
        {"snapshot preserves exact raw payload", testSnapshotPreservesExactRawPayload},
        {"bridge import adapter requires import-grade payload", testBridgeImportAdapterRequiresImportGradePayload},
        {"backend bridge import from real save", testBackendBridgeImportFromRealSave},
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
    return failures == 0 ? 0 : 1;
}
