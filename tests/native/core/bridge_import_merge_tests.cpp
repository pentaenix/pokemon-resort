#include "core/bridge/BridgeImportMerge.hpp"

#include "core/domain/PcSlotSpecies.hpp"
#include "ui/TransferSaveSelection.hpp"

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

void testParseReadsFirstFormatName() {
    const char* json = R"({
  "bridge_import_schema": 1,
  "success": true,
  "pokemon": [
    { "source_game": 10, "format_name": "pk4", "hot": {} },
    { "source_game": 99, "format_name": "pk6", "hot": {} }
  ]
})";
    std::string fmt;
    std::string err;
    expect(pr::parseBridgeImportFirstPokemonFormatName(json, &fmt, &err), err.c_str());
    expect(fmt == "pk4", "expected first format_name");
}

void testParseReadsFirstSourceGame() {
    const char* json = R"({
  "bridge_import_schema": 1,
  "success": true,
  "pokemon": [
    { "source_game": 44, "format_name": "pk6", "hot": {} },
    { "source_game": 99, "format_name": "pk6", "hot": {} }
  ]
})";
    std::uint16_t sg = 0;
    std::string err;
    expect(pr::parseBridgeImportFirstPokemonSourceGame(json, &sg, &err), err.c_str());
    expect(sg == 44, "expected first source_game");
}

void testMergeAttachesPayloadsByBoxSlot() {
    const char* json = R"({
  "bridge_import_schema": 1,
  "success": true,
  "pokemon": [
    {
      "source_game": 10,
      "format_name": "pk6",
      "source_location": { "area": "box", "box": 0, "slot": 3, "global_index": 3 },
      "raw_payload_base64": "QQ==",
      "raw_hash_sha256": "559aead08264d5795d3909718cdd05abd49572e84fe55590eef31a88a08fdffd",
      "hot": {
        "species_id": 121,
        "nickname": "STARMIE",
        "is_nicknamed": false,
        "level": 45,
        "exp": 123456,
        "hp_current": 80,
        "hp_max": 90,
        "status_flags": 4,
        "pid": 3014515454,
        "encryption_constant": 2733779310
      }
    }
  ]
})";

    std::vector<pr::TransferSaveSelection::PcBox> boxes;
    boxes.resize(2);
    boxes[0].slots.resize(30);
    boxes[1].slots.resize(30);

    std::string err;
    expect(pr::mergeBridgeImportIntoGamePcBoxes(json, boxes, &err), err.c_str());
    expect(boxes[0].slots[3].bridge_box_payload_base64 == "QQ==", "base64 not merged");
    expect(boxes[0].slots[3].bridge_box_payload_hash_sha256 ==
               "559aead08264d5795d3909718cdd05abd49572e84fe55590eef31a88a08fdffd",
           "hash not merged");
    expect(boxes[0].slots[3].species_id == 121, "species not merged");
    expect(boxes[0].slots[3].nickname == "STARMIE", "nickname not merged");
    expect(!boxes[0].slots[3].is_nicknamed, "is_nicknamed not merged");
    expect(boxes[0].slots[3].level == 45, "level not merged");
    expect(boxes[0].slots[3].exp == 123456, "exp not merged");
    expect(boxes[0].slots[3].hp_current == 80, "hp_current not merged");
    expect(boxes[0].slots[3].hp_max == 90, "hp_max not merged");
    expect(boxes[0].slots[3].status_flags == 4, "status_flags not merged");
    expect(boxes[0].slots[3].pid.has_value() && *boxes[0].slots[3].pid == 3014515454u, "pid not merged");
    expect(boxes[0].slots[3].encryption_constant.has_value() &&
               *boxes[0].slots[3].encryption_constant == 2733779310u,
           "ec not merged");
    expect(boxes[0].slots[0].bridge_box_payload_base64.empty(), "wrong slot touched");
}

void testResolveReadsBoxSlotFromHash() {
    const char* json = R"({
  "bridge_import_schema": 1,
  "success": true,
  "pokemon": [
    {
      "source_game": 10,
      "format_name": "pk6",
      "source_location": { "area": "party", "box": 9, "slot": 1 },
      "raw_payload_base64": "QQ==",
      "raw_hash_sha256": "1111111111111111111111111111111111111111111111111111111111111111",
      "hot": {}
    },
    {
      "source_game": 10,
      "format_name": "pk6",
      "source_location": { "area": "box", "box": 2, "slot": 7, "global_index": 17 },
      "raw_payload_base64": "Qg==",
      "raw_hash_sha256": "559aead08264d5795d3909718cdd05abd49572e84fe55590eef31a88a08fdffd",
      "hot": {}
    }
  ]
})";
    int bx = -1;
    int si = -1;
    std::string err;
    expect(
        pr::resolveBridgeImportBoxSlotForRawHash(
            json,
            "559aead08264d5795d3909718cdd05abd49572e84fe55590eef31a88a08fdffd",
            &bx,
            &si,
            &err),
        err.c_str());
    expect(bx == 2, "wrong box index");
    expect(si == 7, "wrong slot index");
}

void testFallbackMirrorUsesHotIdentity() {
    const char* json = R"({
  "bridge_import_schema": 1,
  "success": true,
  "pokemon": [
    {
      "source_game": 35,
      "format_name": "pk1",
      "source_location": { "area": "box", "box": 0, "slot": 0 },
      "raw_payload_base64": "QQ==",
      "raw_hash_sha256": "1111111111111111111111111111111111111111111111111111111111111111",
      "hot": {
        "species_id": 133,
        "nickname": "EEVEE",
        "tid16": 9999,
        "dv16": 43690,
        "ot_name": "ALICE"
      }
    },
    {
      "source_game": 35,
      "format_name": "pk1",
      "source_location": { "area": "box", "box": 0, "slot": 3 },
      "raw_payload_base64": "Qg==",
      "raw_hash_sha256": "2222222222222222222222222222222222222222222222222222222222222222",
      "hot": {
        "species_id": 25,
        "nickname": "PIKACHU",
        "tid16": 54321,
        "dv16": 13107,
        "ot_name": "BOB"
      }
    }
  ]
})";
    pr::PcSlotSpecies mirror;
    mirror.species_id = 25;
    mirror.tid16 = 54321;
    mirror.nickname = "PIKACHU";
    mirror.dv16 = static_cast<std::uint16_t>(13107);
    mirror.ot_name = "BOB";

    int bx = -9;
    int si = -9;
    std::string err;
    expect(pr::resolveBridgeImportBoxSlotFallbackMirror(json, mirror, &bx, &si, &err), err.c_str());
    expect(bx == 0, "wrong fallback box index");
    expect(si == 3, "wrong fallback slot index");
}

} // namespace

int main() {
    try {
        testParseReadsFirstFormatName();
        testParseReadsFirstSourceGame();
        testMergeAttachesPayloadsByBoxSlot();
        testResolveReadsBoxSlotFromHash();
        testFallbackMirrorUsesHotIdentity();
        std::cout << "bridge_import_merge_tests: OK\n";
        return 0;
    } catch (const TestFailure& ex) {
        std::cerr << "bridge_import_merge_tests: FAILED: " << ex.what() << "\n";
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "bridge_import_merge_tests: ERROR: " << ex.what() << "\n";
        return 2;
    }
}
