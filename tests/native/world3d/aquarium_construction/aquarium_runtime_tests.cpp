#include "gameplay/world3d/aquarium/construction/AquariumCollisionOverlay.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumConstructionSession.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumDesignStore.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumPlayerRuntime.hpp"
#include "gameplay/world3d/aquarium/rendering/AquariumResourceGeneration.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace aq = pr::gameplay::world3d::aquarium;
namespace construction = aq::construction;
namespace geo = pr::aquarium::geometry;
namespace fs = std::filesystem;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

construction::AquariumDesignDocument emptyDocument() {
    construction::AquariumDesignDocument document;
    document.design_id = "aqd_runtime_test";
    document.map_id = "aquarium12";
    return document;
}

aq::AquariumConstructionConfig constructionConfig() {
    aq::AquariumConstructionConfig config;
    config.enabled = true;
    for (int row = 10; row <= 15; ++row) {
        for (int column = 10; column <= 21; ++column) {
            config.allowed_cells.push_back({column, row});
        }
    }
    return config;
}

void stateMachinePreservesCommittedDataOnCancel() {
    construction::AquariumConstructionSession unavailable;
    unavailable.configure("outdoor", {}, {}, emptyDocument());
    require(!unavailable.enter({0, 0}),
        "construction entered on a map without explicit construction metadata");

    construction::AquariumConstructionSession session;
    session.configure("aquarium12", constructionConfig(), {}, emptyDocument());
    require(session.enter({9, 9}), "construction did not enter on enabled map");
    require(session.beginRectangle(), "rectangle draft did not begin");
    session.moveCursor(1, 1);
    require(!session.draftValid(), "2x2 draft must remain invalid");
    require(!session.prepareCommit(), "invalid draft prepared a commit");
    require(session.cancel(), "draft cancel was not consumed");
    require(session.committedDesign().revision == 0 && session.committedDesign().tanks.empty(),
        "cancel mutated committed aquarium data");
    session.exit();
    require(session.enter({10, 10}), "construction did not re-enter on an allowed player cell");
    require(session.beginRectangle(), "protected-cell rectangle draft did not begin");
    session.moveCursor(2, 2);
    require(!session.draftValid(),
        "tank placement could trap the player inside its committed collision shell");
}

construction::ConstructionCommitCandidate buildFirstTank(
    construction::AquariumConstructionSession& session) {
    require(session.beginRectangle(), "rectangle draft did not begin");
    session.moveCursor(2, 2);
    require(session.draftValid(), "3x3 draft should be valid");
    auto candidate = session.prepareCommit();
    require(candidate.has_value(), "valid rectangle did not prepare a commit");
    geo::AquariumBuildRequest request;
    request.tank = candidate->document.tanks.back();
    require(geo::buildAquarium(request).validation.valid(),
        "prepared rectangle geometry is invalid");
    return std::move(*candidate);
}

void stateMachineBuildsAndRejectsOverlap() {
    construction::AquariumConstructionSession session;
    session.configure("aquarium12", constructionConfig(), {}, emptyDocument());
    require(session.enter({9, 9}), "construction did not enter");
    auto candidate = buildFirstTank(session);
    require(candidate.document.revision == 1 && candidate.document.tanks.size() == 1,
        "confirmed command did not advance revision exactly once");
    session.publish(std::move(candidate));
    require(session.committedDesign().tanks.front().id == "tank_1",
        "tank stable ID changed during publication");
    session.pointAt({10, 10});
    require(session.beginRectangle(), "overlap draft did not begin");
    session.moveCursor(2, 2);
    require(!session.draftValid() && !session.prepareCommit(),
        "overlapping tank was allowed to commit");
}

void loadedPlacementValidationRejectsBoundsAndOverlap() {
    auto document = emptyDocument();
    geo::TankDesign first;
    first.id = "tank_outside";
    first.footprint.origin_cell = {9, 10};
    first.footprint.width_cells = 3;
    first.footprint.depth_cells = 3;
    document.tanks.push_back(first);
    require(!construction::validateAquariumPlacement(
                document, constructionConfig(), {}).empty(),
        "loaded out-of-bounds tank passed map placement validation");
    construction::AquariumConstructionSession session;
    session.configure("aquarium12", constructionConfig(), {}, document);
    require(!session.available(), "invalid loaded aquarium remained editable");

    document.tanks.front().footprint.origin_cell = {10, 10};
    auto second = document.tanks.front();
    second.id = "tank_overlap";
    second.footprint.origin_cell = {12, 12};
    document.tanks.push_back(second);
    require(!construction::validateAquariumPlacement(
                document, constructionConfig(), {}).empty(),
        "overlapping loaded tanks passed placement validation");
}

void storeRoundTripsAndRecoversBackup() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() /
        ("pokemon_resort_aquarium_store_" + std::to_string(nonce));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    construction::AquariumDesignStore store(root / "aquarium12.aquarium.json");
    auto document = emptyDocument();
    std::string error;
    require(store.saveTransactionally(document, &error), "initial transactional save failed");
    document.revision = 1;
    require(store.saveTransactionally(document, &error), "second transactional save failed");
    const auto loaded = store.load();
    require(loaded.document && loaded.document->revision == 1,
        "transactional save did not round trip latest revision");
    {
        std::ofstream corrupt(store.primaryPath(), std::ios::trunc);
        corrupt << "{not-json";
    }
    const auto recovered = store.load();
    require(recovered.status == construction::AquariumStoreLoadStatus::RecoveredBackup &&
            recovered.document && recovered.document->revision == 0,
        "invalid primary did not recover the validated backup");
}

void storePreservesNewerDocumentsAndFailedWrites() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() /
        ("pokemon_resort_aquarium_fault_" + std::to_string(nonce));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    fs::create_directories(root);
    auto document = emptyDocument();
    construction::AquariumDesignStore store(root / "aquarium12.aquarium.json");
    std::string error;
    require(store.saveTransactionally(document, &error), "fault fixture save failed");
    std::string newer = construction::serializeAquariumDesignCanonical(document);
    const std::string old_version = "\"schemaVersion\": 1";
    const auto version_position = newer.find(old_version);
    require(version_position != std::string::npos, "fault fixture schema version missing");
    newer.replace(version_position, old_version.size(), "\"schemaVersion\": 2");
    {
        std::ofstream primary(store.primaryPath(), std::ios::trunc);
        primary << newer;
    }
    require(store.load().status == construction::AquariumStoreLoadStatus::NewerVersion,
        "newer primary was overwritten by an older backup");

    const fs::path blocker = root / "not-a-directory";
    { std::ofstream file(blocker); file << "block"; }
    construction::AquariumDesignStore blocked_store(blocker / "aquarium.json");
    require(!blocked_store.saveTransactionally(document, &error) && !error.empty(),
        "save fault injection unexpectedly committed through a non-directory path");
    require(store.load().status == construction::AquariumStoreLoadStatus::NewerVersion,
        "failed unrelated save damaged the recoverable newer document");
}

class FlatQuery final : public pr::gameplay::world3d::characters::CharacterTerrainQuery {
public:
    float tileSize() const override { return 16.0f; }
    bool containsTile(int, int) const override { return true; }
    bool tileBlocked(int x, int y) const override { return x == 1 && y == 1; }
    bool tileIsActualWater(int, int) const override { return false; }
    int tileBaseHeightUnits(int, int) const override { return 0; }
    int tileSpecial(int, int) const override { return 0; }
    float tileWorldHeight(int, int) const override { return 0.0f; }
    bool canTraverseTerrainEdge(int, int, int, int, int, int) const override { return true; }
    pr::gameplay::world3d::terrain::ActorTerrainBinding bindActorStanding(
        int, int, float, float) const override { return {}; }
    pr::gameplay::world3d::terrain::GridStepMotor beginStep(
        int, int, int, int, int, int, int, int) const override { return {}; }
    float actorHeightDuringStep(
        float, float, const pr::gameplay::world3d::terrain::GridStepMotor&, float) const override {
        return 0.0f;
    }
};

void collisionOverlayCombinesStaticAndDynamicCells() {
    auto overlay = construction::AquariumCollisionOverlay(std::make_shared<FlatQuery>());
    overlay.setBlockedCells({{4, 5}, {4, 5}});
    require(overlay.tileBlocked(1, 1), "overlay discarded static collision");
    require(overlay.tileBlocked(4, 5), "overlay did not expose generated collision");
    require(!overlay.tileBlocked(3, 5), "overlay blocked an unrelated cell");
    require(!overlay.canTraverseTerrainEdge(3, 5, 4, 5, 1, 0),
        "overlay allowed traversal into generated collision");
}

class TestPopulationPolicy final : public construction::AquariumPopulationPolicy {
public:
    std::vector<aq::AquariumPokemonActor> populationFor(
        const construction::PlayerTankRuntime& tank,
        const construction::AquariumPopulationContext&,
        std::vector<std::string>*) const override {
        aq::AquariumPokemonActor actor;
        actor.id = tank.design.id + ":test";
        actor.species = "test-species";
        return {actor};
    }
};

void populationPolicyIsReplaceableAndNavigationIsDerived() {
    auto document = emptyDocument();
    geo::TankDesign tank;
    tank.id = "tank_policy";
    tank.footprint.origin_cell = {10, 10};
    tank.footprint.width_cells = 3;
    tank.footprint.depth_cells = 3;
    document.tanks.push_back(tank);
    pr::gameplay::world3d::SceneConfig scene;
    scene.grid.width = 24;
    scene.grid.height = 18;
    scene.grid.tile_size = 16.0f;
    scene.terrain.heights.assign(18, std::vector<std::uint8_t>(24, 0));
    aq::AquariumMapConfig map;
    map.map_id = "aquarium12";
    TestPopulationPolicy policy;
    const auto runtime = construction::buildPlayerAquariumRuntime(
        document, scene, map, fs::path{}, policy);
    require(runtime.tanks.size() == 1 && runtime.actors.size() == 1,
        "replaceable population policy was not applied once per tank");
    require(runtime.tanks.front().build.navigation.layers.size() == 1,
        "committed rectangle did not derive a navigation volume");
    require(runtime.collision_cells.size() == 8,
        "committed 3x3 rectangle did not derive perimeter collision");
}

void resourceGenerationRejectsCandidatesWithoutTouchingActiveResources() {
    struct FakeResource { int id = 0; };
    aq::rendering::AquariumResourceGeneration<FakeResource> owner;
    std::vector<int> destroyed;
    const auto destroy = [&](FakeResource& resource) { destroyed.push_back(resource.id); };
    require(owner.publish({{1}, {2}}, true, destroy), "initial fake GPU generation did not publish");
    require(owner.generation() == 1 && owner.active().size() == 2,
        "initial fake GPU generation metadata is wrong");
    require(!owner.publish({{3}, {4}}, false, destroy),
        "invalid fake GPU generation unexpectedly published");
    require(owner.generation() == 1 && owner.active()[0].id == 1 &&
            destroyed.size() == 2 && destroyed[0] == 3 && destroyed[1] == 4,
        "candidate rejection changed active resources or leaked candidate resources");
    require(owner.stage({{5}}, true, destroy), "replacement fake GPU generation did not stage");
    require(owner.generation() == 1 && owner.active().size() == 2,
        "staging changed the active fake GPU generation before publication");
    owner.discardStaged(destroy);
    require(owner.generation() == 1 && owner.active().size() == 2 && destroyed.back() == 5,
        "discarding a staged generation changed active resources or leaked the candidate");
    require(owner.stage({{6}}, true, destroy) && owner.publishStaged(destroy),
        "replacement fake GPU generation failed");
    require(owner.generation() == 2 && owner.active().size() == 1 && owner.active()[0].id == 6,
        "replacement did not publish atomically");
    require(destroyed.size() == 5 && destroyed[3] == 1 && destroyed[4] == 2,
        "replacement did not retire the previous resource generation");
}

} // namespace

int main() {
    try {
        stateMachinePreservesCommittedDataOnCancel();
        stateMachineBuildsAndRejectsOverlap();
        loadedPlacementValidationRejectsBoundsAndOverlap();
        storeRoundTripsAndRecoversBackup();
        storePreservesNewerDocumentsAndFailedWrites();
        collisionOverlayCombinesStaticAndDynamicCells();
        populationPolicyIsReplaceableAndNavigationIsDerived();
        resourceGenerationRejectsCandidatesWithoutTouchingActiveResources();
        std::cout << "aquarium_runtime_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "aquarium_runtime_tests: " << error.what() << '\n';
        return 1;
    }
}
