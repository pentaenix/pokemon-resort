#include "mapmaker/app/ProjectWorkspace.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("could not create test project");
    output << text;
}

fs::path temporaryRoot() {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path path = fs::temp_directory_path() /
        ("pokemon_resort_workspace_" + std::to_string(suffix));
    fs::create_directories(path);
    return path;
}

void testNewMapUndoRedoAndDeferredAtomicSave() {
    const fs::path root = temporaryRoot();
    try {
        const fs::path maps = root / "assets/overworld/maps";
        fs::create_directories(maps);
        pr::mapmaker::OwmapDocument::create(4, 4, 16.0f).saveAtomic(maps / "origin.owmap");
        const fs::path project_path = root / "config/gameplay/world3d/map_project.json";
        writeText(project_path, R"json({
  "version": 1,
  "id": "test",
  "name": "Test",
  "maps": [{"id":"origin","name":"Origin","file":"origin.owmap","gridX":0,"gridY":0}],
  "future": {"preserve": true}
})json");
        auto workspace = pr::mapmaker::ProjectWorkspace::open(root, project_path);
        pr::mapmaker::NewMapSpec spec;
        spec.id = "origin_east";
        spec.name = "Origin East";
        spec.type = "interior";
        spec.width = 12;
        spec.height = 9;
        spec.grid_x = 1;
        std::string error;
        expect(workspace.createMap(spec, &error), "create map succeeds");
        expect(workspace.project().findMap(spec.id), "new map enters project graph");
        expect(!fs::exists(maps / "origin_east.owmap"),
            "new source stays in memory until the project is saved");
        expect(workspace.dirty(), "new map dirties workspace");
        expect(workspace.undoProject(), "new map creation is undoable");
        expect(!workspace.project().findMap(spec.id), "undo removes project node");
        expect(!workspace.dirty(), "undo returns project to saved revision");
        expect(workspace.redoProject(), "new map creation is redoable");
        workspace.saveAll();
        expect(fs::is_regular_file(maps / "origin_east.owmap"),
            "save materializes the new OWMAP source");
        expect(!workspace.dirty(), "save marks source and project clean");
        const auto saved = pr::mapmaker::MapProjectDocument::load(project_path);
        expect(saved.findMap(spec.id), "saved project references new source");
        expect(saved.serialize().find("\"future\"") != std::string::npos,
            "project save preserves unknown metadata");
        const auto map = pr::mapmaker::OwmapDocument::load(maps / "origin_east.owmap");
        expect(map.width() == 12 && map.height() == 9, "new map dimensions persist");
        expect(map.metadata().get("type")->asString() == "interior" &&
            map.metadata().get("interior")->get("defaultRoom")->get("enabled")->asBool(),
            "new interiors opt into the resizeable default room foundation");
        const auto* default_room = map.metadata().get("interior")->get("defaultRoom");
        expect(default_room->get("wallHeightTiles")->asNumber() == 4.0 &&
                default_room->get("walkableInsetTiles")->asNumber() == 1.0 &&
                default_room->get("wallFaceOffsetTiles")->asNumber() == 0.5 &&
                default_room->get("entryExtensionDepthTiles")->asNumber() == 0.0 &&
                default_room->get("blackTopCap")->asBool(),
            "new interiors use one in-bounds row for the complete three-cell entry");
        const auto& openings = map.metadata().get("interior")->get("openings")->asArray();
        expect(openings.size() == 1U &&
                openings[0].get("edge")->asString() == "south" &&
                openings[0].get("from")->asNumber() == 5.0 &&
                openings[0].get("to")->asNumber() == 7.0,
            "new interiors keep all three proposed entry paths open through automatic wall collision");
        expect(map.metadata().get("visual") && map.metadata().get("visual")->isObject(),
            "new maps include the visual section required by the game loader");
        const auto& anchors = map.metadata().get("anchors")->asArray();
        expect(anchors.size() == 3U &&
            anchors[0].get("id")->asString() == "entry_left" &&
            anchors[0].get("tile")->asArray()[0].asNumber() == 5.0 &&
            anchors[1].get("id")->asString() == "entry" &&
            anchors[1].get("tile")->asArray()[0].asNumber() == 6.0 &&
            anchors[2].get("id")->asString() == "entry_right" &&
            anchors[2].get("tile")->asArray()[0].asNumber() == 7.0 &&
            anchors[1].get("tile")->asArray()[1].asNumber() == 8.0 &&
            anchors[1].get("facing")->asString() == "north",
            "new interiors propose three south entry nodes with entry in the middle");

        const auto [standalone_x, standalone_y] = workspace.suggestedStandaloneMapPosition();
        expect(standalone_x == 3 && standalone_y == 0,
            "standalone maps receive a clear world-card position");
        pr::mapmaker::NewMapSpec standalone;
        standalone.id = "bonus_area";
        standalone.name = "Bonus Area";
        standalone.width = 20;
        standalone.height = 14;
        standalone.grid_x = standalone_x;
        standalone.grid_y = standalone_y;
        standalone.linked = false;
        expect(workspace.createMap(standalone, &error),
            "standalone exterior creation succeeds");
        const auto* standalone_entry = workspace.project().findMap(standalone.id);
        expect(standalone_entry && !standalone_entry->linked,
            "standalone exterior is not spatially linked");
        workspace.saveAll();
        const auto with_standalone = pr::mapmaker::MapProjectDocument::load(project_path);
        expect(with_standalone.findMap(standalone.id) &&
            !with_standalone.findMap(standalone.id)->linked,
            "standalone status persists in the project");

        expect(workspace.createMapInstance(
            "origin_copy", "Origin Copy", "origin", -2, 1, &error),
            "reused map instance can be created");
        expect(workspace.project().isReusedMap("origin_copy"),
            "reused instance projects shared-source identity");
        expect(workspace.sourceForMap("origin_copy") == workspace.sourceForMap("origin"),
            "reused instance shares one loaded document and history");
    } catch (...) {
        fs::remove_all(root);
        throw;
    }
    fs::remove_all(root);
}

} // namespace

int main() {
    try {
        testNewMapUndoRedoAndDeferredAtomicSave();
        std::cout << "project_workspace_tests: PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "project_workspace_tests: FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
