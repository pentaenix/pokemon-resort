#include "core/save/TransferBoxEditsStore.hpp"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

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

fs::path tempDir(const std::string& name) {
    const fs::path base = fs::temp_directory_path() / ("pokemon_resort_" + name);
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base);
    return base;
}

std::string readFileToString(const fs::path& p) {
    std::ifstream in(p);
    if (!in) return {};
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return s;
}

void writeStringToFile(const fs::path& p, const std::string& contents) {
    std::ofstream out(p, std::ios::trunc);
    if (!out) {
        throw TestFailure("Could not write file: " + p.string());
    }
    out << contents;
    out.flush();
    if (!out) {
        throw TestFailure("Failed while writing file: " + p.string());
    }
}

pr::PcSlotSpecies makeSlot(int species_id, const std::string& nickname, int held_item_id) {
    pr::PcSlotSpecies slot;
    slot.present = true;
    slot.species_id = species_id;
    slot.species_name = "Testmon";
    slot.nickname = nickname;
    slot.slug = "testmon";
    slot.held_item_id = held_item_id;
    slot.held_item_name = held_item_id > 0 ? "Potion" : "";
    slot.level = 42;
    slot.is_shiny = true;
    slot.move_count = 1;
    slot.moves[0].move_id = 1;
    slot.moves[0].move_name = "Tackle";
    slot.moves[0].current_pp = 30;
    slot.moves[0].pp_ups = 0;
    return slot;
}

void testOverlayRoundTripSaveLoad() {
    const fs::path save_dir = tempDir("transfer_box_edits_roundtrip");
    pr::TransferBoxEditsOverlay overlay;
    overlay.version = 2;
    overlay.source_path = "/fake/source.sav";
    overlay.game_key = "emerald";
    pr::TransferSaveSelection::PcBox box;
    box.name = "Box 1";
    box.slots.push_back(makeSlot(25, "Pika", 10));
    overlay.pc_boxes.push_back(box);

    std::string error;
    expect(pr::saveTransferBoxEditsOverlayAtomic(save_dir.string(), overlay, &error), "saveTransferBoxEditsOverlayAtomic failed: " + error);

    const fs::path overlay_path = save_dir / "transfer_box_edits.json";
    expect(fs::exists(overlay_path), "overlay file was not created");
    expect(!fs::exists(save_dir / "transfer_box_edits.json.tmp"), "temp file should not remain after save");

    const auto loaded = pr::loadTransferBoxEditsOverlay(save_dir.string(), overlay.source_path, overlay.game_key, &error);
    expect(loaded.has_value(), "loadTransferBoxEditsOverlay did not return overlay: " + error);
    expect(loaded->source_path == overlay.source_path, "loaded source_path mismatch");
    expect(loaded->game_key == overlay.game_key, "loaded game_key mismatch");
    expect(loaded->pc_boxes.size() == 1, "loaded pc_boxes size mismatch");
    expect(loaded->pc_boxes[0].name == "Box 1", "loaded box name mismatch");
    expect(loaded->pc_boxes[0].slots.size() == 1, "loaded box slots size mismatch");
    expect(loaded->pc_boxes[0].slots[0].present, "loaded slot present mismatch");
    expect(loaded->pc_boxes[0].slots[0].nickname == "Pika", "loaded slot nickname mismatch");
    expect(loaded->pc_boxes[0].slots[0].held_item_id == 10, "loaded slot held_item_id mismatch");
}

void testOverlayLoadIgnoresMismatchedIdentity() {
    const fs::path save_dir = tempDir("transfer_box_edits_identity");
    pr::TransferBoxEditsOverlay overlay;
    overlay.version = 2;
    overlay.source_path = "/fake/source_a.sav";
    overlay.game_key = "ruby";
    pr::TransferSaveSelection::PcBox box;
    box.name = "Box A";
    box.slots.push_back(makeSlot(1, "A", 0));
    overlay.pc_boxes.push_back(box);

    std::string error;
    expect(pr::saveTransferBoxEditsOverlayAtomic(save_dir.string(), overlay, &error), "save failed: " + error);

    // Wrong source_path → should not apply.
    const auto wrong_src = pr::loadTransferBoxEditsOverlay(save_dir.string(), "/fake/source_b.sav", "ruby", &error);
    expect(!wrong_src.has_value(), "overlay should be ignored for mismatched source_path");

    // Wrong game_key → should not apply.
    const auto wrong_key = pr::loadTransferBoxEditsOverlay(save_dir.string(), "/fake/source_a.sav", "sapphire", &error);
    expect(!wrong_key.has_value(), "overlay should be ignored for mismatched game_key");
}

void testOverlayLoadReturnsNulloptOnCorruptJson() {
    const fs::path save_dir = tempDir("transfer_box_edits_corrupt");
    writeStringToFile(save_dir / "transfer_box_edits.json", "{ this is not json ");
    std::string error;
    const auto loaded = pr::loadTransferBoxEditsOverlay(save_dir.string(), "/fake/source.sav", "emerald", &error);
    expect(!loaded.has_value(), "corrupt overlay should not load");
}

void testOverlayBridgePayloadRoundTrip() {
    const fs::path save_dir = tempDir("transfer_box_edits_bridge_payload");
    pr::TransferBoxEditsOverlay overlay;
    overlay.version = 2;
    overlay.source_path = "/fake/source.sav";
    overlay.game_key = "platinum";
    pr::TransferSaveSelection::PcBox box;
    box.name = "Box";
    auto slot = makeSlot(25, "Zap", 0);
    slot.bridge_box_payload_base64 = "QQ==";
    slot.bridge_box_payload_hash_sha256 = "559aead08264d5795d3909718cdd05abd49572e84fe55590eef31a88a08fdffd";
    box.slots.push_back(slot);
    overlay.pc_boxes.push_back(box);

    std::string error;
    expect(pr::saveTransferBoxEditsOverlayAtomic(save_dir.string(), overlay, &error), "save failed: " + error);

    const auto loaded = pr::loadTransferBoxEditsOverlay(save_dir.string(), overlay.source_path, overlay.game_key, &error);
    expect(loaded.has_value(), "load failed: " + error);
    expect(loaded->pc_boxes[0].slots[0].bridge_box_payload_base64 == "QQ==", "bridge base64 mismatch");
    expect(loaded->pc_boxes[0].slots[0].bridge_box_payload_hash_sha256 ==
               "559aead08264d5795d3909718cdd05abd49572e84fe55590eef31a88a08fdffd",
           "bridge hash mismatch");
}

void testOverlayLoadRejectsVersion1Files() {
    const fs::path save_dir = tempDir("transfer_box_edits_v1_reject");
    writeStringToFile(
        save_dir / "transfer_box_edits.json",
        R"({"version":1,"source_path":"/x.sav","game_key":"k","pc_boxes":[{"name":"B","slots":[]}]})");
    std::string error;
    const auto loaded = pr::loadTransferBoxEditsOverlay(save_dir.string(), "/x.sav", "k", &error);
    expect(!loaded.has_value(), "v1 overlay must be ignored (unsafe without PKM payloads)");
}

void testSaveDoesNotTouchSourcePath() {
    const fs::path root = tempDir("transfer_box_edits_no_touch");
    const fs::path save_dir = root / "cache";
    fs::create_directories(save_dir);
    const fs::path source = root / "source_save.bin";
    writeStringToFile(source, "ORIGINAL_SAVE_BYTES");
    const std::string before = readFileToString(source);

    pr::TransferBoxEditsOverlay overlay;
    overlay.version = 2;
    overlay.source_path = source.string();
    overlay.game_key = "firered";
    pr::TransferSaveSelection::PcBox box;
    box.name = "Box";
    box.slots.push_back(makeSlot(7, "Squirt", 0));
    overlay.pc_boxes.push_back(box);

    std::string error;
    expect(pr::saveTransferBoxEditsOverlayAtomic(save_dir.string(), overlay, &error), "save failed: " + error);

    const std::string after = readFileToString(source);
    expect(before == after, "source save bytes changed; overlay save must never mutate source_path");
}

} // namespace

int main() {
    try {
        testOverlayRoundTripSaveLoad();
        testOverlayLoadIgnoresMismatchedIdentity();
        testOverlayLoadReturnsNulloptOnCorruptJson();
        testOverlayBridgePayloadRoundTrip();
        testOverlayLoadRejectsVersion1Files();
        testSaveDoesNotTouchSourcePath();
        std::cout << "transfer_box_edits_store_tests: OK\n";
        return 0;
    } catch (const TestFailure& ex) {
        std::cerr << "transfer_box_edits_store_tests: FAILED: " << ex.what() << "\n";
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "transfer_box_edits_store_tests: ERROR: " << ex.what() << "\n";
        return 2;
    }
}

