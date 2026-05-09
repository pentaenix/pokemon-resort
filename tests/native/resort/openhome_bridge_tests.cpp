#include "resort/openhome/OpenHomeMovementBridge.hpp"
#include "resort/openhome/OpenHomeStorageBridge.hpp"

#include <algorithm>
#include <filesystem>
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

void testStorageBridgeWritesOhpkmStoreAndBanksByOpenHomeId() {
    const fs::path root = fs::temp_directory_path() / "pokemon_resort_openhome_bridge_only";
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

    const auto banks = bridge.loadHomeBanks();
    expect(banks.banks[0].id.size() == 36, "OpenHome bank id should be UUID-shaped for OpenHome compatibility");
    expect(banks.banks[0].boxes[0].id.size() == 36, "OpenHome box id should be UUID-shaped for OpenHome compatibility");
    expect(banks.banks[0].boxes[0].identifiers_by_slot.at(7) == payload.openhome_id,
           "OpenHome box slot should store OpenHome ID");

    bridge.placeInHomeBox(payload.openhome_id, 0, 1, 3);
    const auto moved = bridge.loadHomeBanks();
    expect(moved.banks[0].boxes[0].identifiers_by_slot.empty(),
           "moving an OpenHome ID should remove the old placement");
    expect(moved.banks[0].boxes[1].identifiers_by_slot.at(3) == payload.openhome_id,
           "moving an OpenHome ID should write the new placement");
    fs::remove_all(root);
}

void testPullToHomePlanMatchesUiTrackingFlow() {
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

    expect(steps == expected, "OpenHome pull-to-home plan should mirror OpenHome UI flow");
}

void testPushToGamePlanUsesOpenHomeIdAndSaveWriter() {
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

    expect(steps == expected, "OpenHome push-to-game plan should mirror OpenHome UI flow");
    expect(request.openhome_id.find("PKR") == std::string::npos, "OpenHome push should use OpenHome ID");
}

void testMoveBetweenGamesPlanProjectsFromOhpkm() {
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
    expect(std::find(
               steps.begin(),
               steps.end(),
               pr::resort::openhome::OpenHomeMovementStep::ConvertOhpkmForTargetSave) != steps.end(),
           "game-to-game moves should project from OHPKM");
    expect(std::find(
               steps.begin(),
               steps.end(),
               pr::resort::openhome::OpenHomeMovementStep::UpsertOhpkmStore) != steps.end(),
           "game-to-game moves should update the OpenHome OHPKM store");
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests{
        {"storage bridge writes OHPKM store and banks by OpenHome ID", testStorageBridgeWritesOhpkmStoreAndBanksByOpenHomeId},
        {"pull-to-home plan matches UI tracking flow", testPullToHomePlanMatchesUiTrackingFlow},
        {"push-to-game plan uses OpenHome ID and save writer", testPushToGamePlanUsesOpenHomeIdAndSaveWriter},
        {"move-between-games plan projects from OHPKM", testMoveBetweenGamesPlanProjectsFromOhpkm},
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
