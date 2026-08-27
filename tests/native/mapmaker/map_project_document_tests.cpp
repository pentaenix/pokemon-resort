#include "mapmaker/project/MapProjectDocument.hpp"
#include "mapmaker/project/ProjectDiscovery.hpp"
#include "mapmaker/selection/SelectionModel.hpp"
#include "mapmaker/session/EditorSession.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void expect(bool condition, const std::string& message) {
    if (!condition) throw TestFailure(message);
}

fs::path temporaryRoot(const std::string& label) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path path = fs::temp_directory_path() /
        ("pokemon_resort_mapmaker_" + label + "_" + std::to_string(suffix));
    fs::create_directories(path);
    return path;
}

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output) throw TestFailure("Could not create " + path.string());
    output << text;
}

const std::string& projectJson() {
    static const std::string json = R"json({
  "version": 1,
  "id": "resort",
  "name": "Resort",
  "maps": [
    {"id":"water_a","name":"Water A","file":"shared/../water.owmap","gridX":0,"gridY":0,"linked":true},
    {"id":"water_b","name":"Water B","file":"./water.owmap","sourceMapId":"water_a","gridX":8,"gridY":0,"linked":false},
    {"id":"house","name":"House","file":"house.owmap","gridX":0,"gridY":0,"linked":false}
  ],
  "tilePackages": [{"id":"gen5","file":"maptiles.rtpks","name":"Gen 5"}],
  "defaultTilePackageId": "gen5",
  "editor": {"activeMapId":"house","viewMode":"3d","zoom":1.5},
  "futureFeature": {"mustSurvive":true,"nested":[1,2,3]}
}
)json";
    return json;
}

void testProjectionPreservesRawJsonAndUnknownFields() {
    const pr::mapmaker::MapProjectDocument project =
        pr::mapmaker::MapProjectDocument::parse(projectJson(), "project.json");
    expect(project.rawJson() == projectJson(), "raw project text must remain byte-for-byte intact");
    expect(project.id() == "resort" && project.name() == "Resort", "project identity projects");
    expect(project.maps().size() == 3U, "all map entries project");
    expect(project.tilePackages().size() == 1U, "tile packages project");
    expect(project.defaultTilePackageId() == "gen5", "default package projects");
    expect(project.editor().active_map_id == "house" && project.editor().view_mode == "3d" &&
        project.editor().zoom == 1.5, "editor preferences project");
    expect(project.sourcePath() == fs::path("project.json"), "source path is retained");
}

void testReusedEntriesShareOneNormalizedSource() {
    const pr::mapmaker::MapProjectDocument project =
        pr::mapmaker::MapProjectDocument::parse(projectJson());
    expect(project.sourceKeyFor("water_a") == "water.owmap", "dot segments normalize");
    expect(project.sourceKeyFor("water_b") == "water.owmap", "leading dot segment normalizes");
    const auto entries = project.entriesSharingSource("water_b");
    expect(entries.size() == 2U, "reused instances resolve to one source group");
    expect(entries[0]->id == "water_a" && entries[1]->id == "water_b",
        "source group preserves project order");
    expect(project.isReusedMap("water_a") && project.isReusedMap("water_b"),
        "both root and instance report shared use");
    expect(!project.isReusedMap("house"), "unique file is not reported as reused");
    expect(project.sourceGroups().size() == 2U, "three entries form two source groups");
}

void testEditorSessionUsesPreferenceAndRetainsSharedIdentity() {
    pr::mapmaker::EditorSession session(pr::mapmaker::MapProjectDocument::parse(projectJson()));
    expect(session.activeMapId() == "house", "session starts at valid project preference");
    expect(!session.activateMap("missing"), "unknown map cannot become active");
    expect(session.activateMap("water_b"), "known reused map can become active");
    const auto source = session.activeSource();
    expect(source.has_value() && source->entry_ids.size() == 2U,
        "active reused entry exposes the shared source group");

    session.selection().select({pr::mapmaker::SelectionKind::Model, "water_b", "tree"});
    session.selection().select(
        {pr::mapmaker::SelectionKind::Model, "house", "table"},
        pr::mapmaker::SelectionMode::Add);
    expect(session.activateMap("house"), "switching active maps succeeds");
    expect(session.selection().items().size() == 1U &&
        session.selection().items().front().object_id == "table",
        "map switch drops stale selections but keeps selections in the new map");
}

void testDiscoveryUsesRuntimeOrder() {
    const fs::path workspace = temporaryRoot("discovery");
    const fs::path resort = workspace / "pokemon-resort";
    const auto candidates = pr::mapmaker::mapProjectDiscoveryCandidates(resort);
    expect(candidates.size() == 3U, "discovery exposes all migration candidates");

    writeText(candidates[2], "{}");
    auto result = pr::mapmaker::discoverMapProject(resort);
    expect(result.path == candidates[2] && result.usedFallback(), "legacy web project is final fallback");

    writeText(candidates[1], "{}");
    result = pr::mapmaker::discoverMapProject(resort);
    expect(result.path == candidates[1] && result.usedFallback(), "asset-local project beats web fallback");

    writeText(candidates[0], "{}");
    result = pr::mapmaker::discoverMapProject(resort);
    expect(result.path == candidates[0] && !result.usedFallback(), "canonical config wins discovery");
    fs::remove_all(workspace);
}

void testLoadingKeepsExactFileBytes() {
    const fs::path root = temporaryRoot("load");
    const fs::path path = root / "map_project.json";
    writeText(path, projectJson());
    const auto project = pr::mapmaker::MapProjectDocument::load(path);
    expect(project.rawJson() == projectJson(), "loading does not normalize whitespace or unknown JSON");
    expect(project.sourcePath() == path, "loading records the exact source path");
    fs::remove_all(root);
}

void testWorldMutationsPreserveUnknownProjectData() {
    auto project = pr::mapmaker::MapProjectDocument::parse(projectJson());
    expect(project.moveMap("house", -4, 7), "map position changes");
    const auto* house = project.findMap("house");
    expect(house && house->grid_x == -4 && house->grid_y == 7,
        "world position projection updates immediately");
    pr::mapmaker::MapProjectEntry addition;
    addition.id = "dock_east";
    addition.name = "East Dock";
    addition.file = "dock_east.owmap";
    addition.grid_x = 9;
    addition.grid_y = 3;
    expect(project.addMap(addition), "new map is added");
    expect(!project.addMap(addition), "duplicate map IDs are rejected");
    const std::string saved = project.serialize();
    expect(saved.find("\"futureFeature\"") != std::string::npos &&
        saved.find("\"mustSurvive\": true") != std::string::npos,
        "unknown project metadata survives mutations");
    const auto reparsed = pr::mapmaker::MapProjectDocument::parse(saved);
    expect(reparsed.findMap("dock_east") != nullptr && reparsed.maps().size() == 4U,
        "mutated project round-trips");
}

void testAtomicProjectSaveCreatesRecoverableBackup() {
    const fs::path root = temporaryRoot("project_save");
    const fs::path path = root / "map_project.json";
    writeText(path, projectJson());
    auto project = pr::mapmaker::MapProjectDocument::load(path);
    expect(project.moveMap("house", 12, -8), "save test mutation applies");
    project.saveAtomic();
    expect(fs::is_regular_file(path.string() + ".bak"), "atomic save preserves a backup");
    const auto loaded = pr::mapmaker::MapProjectDocument::load(path);
    const auto* house = loaded.findMap("house");
    expect(house && house->grid_x == 12 && house->grid_y == -8,
        "atomic save writes a parseable project");
    const auto backup = pr::mapmaker::MapProjectDocument::load(path.string() + ".bak");
    const auto* old_house = backup.findMap("house");
    expect(old_house && old_house->grid_x == 0 && old_house->grid_y == 0,
        "backup retains the previous project");
    fs::remove_all(root);
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests{
        {"projection preserves raw JSON and unknown fields", testProjectionPreservesRawJsonAndUnknownFields},
        {"reused entries share one normalized source", testReusedEntriesShareOneNormalizedSource},
        {"editor session uses preference and shared identity", testEditorSessionUsesPreferenceAndRetainsSharedIdentity},
        {"discovery uses runtime order", testDiscoveryUsesRuntimeOrder},
        {"loading keeps exact file bytes", testLoadingKeepsExactFileBytes},
        {"world mutations preserve unknown data", testWorldMutationsPreserveUnknownProjectData},
        {"atomic project save creates backup", testAtomicProjectSaveCreatesRecoverableBackup},
    };
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
