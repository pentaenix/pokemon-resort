#include "core/domain/PcSlotSpecies.hpp"
#include "resort/integration/BridgeImportAdapter.hpp"

#include <exception>
#include <iostream>
#include <string>

namespace {

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

/// "QQ==" decodes to one byte 0x41; hash matches `bridge_import_merge_tests`.
constexpr const char* kOneByte41Hash =
    "559aead08264d5795d3909718cdd05abd49572e84fe55590eef31a88a08fdffd";

void testBuildsFromMergedPcSlot() {
    pr::PcSlotSpecies slot{};
    slot.present = true;
    slot.slug = "pikachu";
    slot.species_name = "Pikachu";
    slot.species_id = 25;
    slot.format = "pk6";
    slot.nickname = "Pika";
    slot.is_nicknamed = true;
    slot.form_key = "$";
    slot.level = 20;
    slot.exp = 8000;
    slot.hp_current = 40;
    slot.hp_max = 55;
    slot.status_flags = 2;
    slot.ot_name = "ASH";
    slot.tid16 = 12345;
    slot.sid16 = 678;
    slot.pid = 0x12345678u;
    slot.encryption_constant = 0x87654321u;
    slot.home_tracker = "HOMETRACKER";
    slot.lineage_root_species = 25;
    slot.ball_id = 4;
    slot.nature = "Jolly";
    slot.ability_id = 9;
    slot.ability_name = "Static";
    slot.primary_type = "Electric";
    slot.tera_type = "Electric";
    slot.mark_icon = "lunch-time";
    slot.pokerus_status = "infected";
    slot.is_alpha = true;
    slot.is_gigantamax = true;
    slot.markings = 3;
    slot.source_game_id = 64;
    slot.source_game_key = "pokemon_heartgold";
    slot.source_save_trainer_name = "Ethan";
    slot.source_save_play_time = "12:34";
    slot.source_save_badges = "8";
    slot.bridge_box_payload_base64 = "QQ==";
    slot.bridge_box_payload_hash_sha256 = kOneByte41Hash;

    const auto got = pr::resort::importedPokemonFromGamePcSlot(slot, 10);
    expect(got.has_value(), "expected optional ImportedPokemon");
    expect(got->source_game == 10, "source_game");
    expect(got->format_name == "pk6", "format_name");
    expect(got->raw_bytes.size() == 1 && got->raw_bytes[0] == 0x41, "raw bytes");
    expect(got->warm_json.find("\"source_game_key\":\"pokemon_heartgold\"") != std::string::npos,
           "warm_json should preserve concrete source game key");
    expect(got->warm_json.find("\"source_game_id\":64") != std::string::npos,
           "warm_json should preserve bridge source game id");
    expect(got->warm_json.find("\"species_name\":\"Pikachu\"") != std::string::npos,
           "warm_json should preserve species name for Resort slot info banners");
    expect(got->warm_json.find("\"nature\":\"Jolly\"") != std::string::npos,
           "warm_json should preserve nature for Resort slot info banners");
    expect(got->warm_json.find("\"ability_name\":\"Static\"") != std::string::npos,
           "warm_json should preserve ability name for Resort slot info banners");
    expect(got->warm_json.find("\"primary_type\":\"Electric\"") != std::string::npos,
           "warm_json should preserve type names for Resort slot info banners");
    expect(got->warm_json.find("\"pokerus_status\":\"infected\"") != std::string::npos,
           "warm_json should preserve Pokerus status for Resort slot info banners");
    expect(got->warm_json.find("\"is_alpha\":true") != std::string::npos,
           "warm_json should preserve alpha status for Resort slot info banners");
    expect(got->warm_json.find("\"is_gigantamax\":true") != std::string::npos,
           "warm_json should preserve Gigantamax status for Resort slot info banners");
    expect(got->warm_json.find("\"markings\":3") != std::string::npos,
           "warm_json should preserve markings for Resort slot info banners");
    expect(got->warm_json.find("\"source_context\"") != std::string::npos,
           "warm_json should preserve first-transfer source context");
    expect(got->warm_json.find("\"game_key\":\"pokemon_heartgold\"") != std::string::npos,
           "source context should preserve concrete source game key");
    expect(got->warm_json.find("\"trainer_name\":\"Ethan\"") != std::string::npos,
           "source context should preserve source save trainer");
    expect(got->warm_json.find("\"play_time\":\"12:34\"") != std::string::npos,
           "source context should preserve source save play time");
    expect(got->warm_json.find("\"badges\":\"8\"") != std::string::npos,
           "source context should preserve source save badge count");
    expect(got->hot.species_id == 25, "species");
    expect(got->hot.nickname == "Pika", "nickname");
    expect(got->hot.is_nicknamed, "nickname flag");
    expect(got->hot.exp == 8000, "exp");
    expect(got->hot.hp_current == 40 && got->hot.hp_max == 55, "hp");
    expect(got->hot.status_flags == 2, "status");
    expect(got->hot.ot_name == "ASH", "ot");
    expect(got->hot.pid.has_value() && *got->hot.pid == 0x12345678u, "pid");
    expect(got->hot.encryption_constant.has_value() && *got->hot.encryption_constant == 0x87654321u, "ec");
    expect(got->hot.home_tracker.has_value() && *got->hot.home_tracker == "HOMETRACKER", "home tracker");
    expect(got->identity.pid.has_value() && *got->identity.pid == 0x12345678u, "identity pid");
    expect(got->identity.encryption_constant.has_value() && *got->identity.encryption_constant == 0x87654321u, "identity ec");
}

void testRejectsMissingPayload() {
    pr::PcSlotSpecies slot{};
    slot.present = true;
    slot.species_id = 25;
    slot.format = "pk6";
    const auto got = pr::resort::importedPokemonFromGamePcSlot(slot, 10);
    expect(!got.has_value(), "expected failure without bridge payload");
}

void testBuildPreservesNonNicknamedDefaultSpeciesText() {
    pr::PcSlotSpecies slot{};
    slot.present = true;
    slot.slug = "pikachu";
    slot.species_name = "Pikachu";
    slot.species_id = 25;
    slot.format = "pk3";
    slot.nickname = "PIKACHU";
    slot.is_nicknamed = false;
    slot.level = 20;
    slot.ot_name = "ASH";
    slot.bridge_box_payload_base64 = "QQ==";
    slot.bridge_box_payload_hash_sha256 = kOneByte41Hash;

    const auto got = pr::resort::importedPokemonFromGamePcSlot(slot, 3);
    expect(got.has_value(), "expected optional ImportedPokemon");
    expect(got->hot.nickname == "PIKACHU", "projection display bytes should remain available as incoming text");
    expect(!got->hot.is_nicknamed, "Gen 3 default species bytes must not become canonical nickname state");
}

} // namespace

int main() {
    try {
        testBuildsFromMergedPcSlot();
        testRejectsMissingPayload();
        testBuildPreservesNonNicknamedDefaultSpeciesText();
        std::cout << "pcslot_imported_pokemon_builder_tests: OK\n";
        return 0;
    } catch (const TestFailure& ex) {
        std::cerr << "pcslot_imported_pokemon_builder_tests: FAILED: " << ex.what() << "\n";
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "pcslot_imported_pokemon_builder_tests: ERROR: " << ex.what() << "\n";
        return 2;
    }
}
