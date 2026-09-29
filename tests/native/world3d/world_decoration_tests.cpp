#include "gameplay/world3d/decorations/WorldDecoration.hpp"

#include <filesystem>
#include <iostream>

namespace {
int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
}

int main() {
    namespace decor = pr::gameplay::world3d::decorations;
    namespace fs = std::filesystem;

    decor::Catalog catalog;
    std::string error;
    expect(catalog.load(PR_SOURCE_DIR, error), "committed outdoor decoration catalog should load");
    expect(catalog.entries().size() == 6, "initial outdoor catalog should expose six palm variants");
    expect(catalog.indices("nature").size() == 6, "initial palm assets should live in the nature tab");

    pr::gameplay::world3d::SceneConfig scene;
    scene.grid.enabled = true;
    scene.grid.width = 4;
    scene.grid.height = 4;
    scene.grid.tile_size = 16.0f;
    scene.terrain.collision.assign(4, std::vector<std::uint8_t>(4, 0));
    scene.terrain.collision[1][1] = 1;
    scene.water_terrain.actual_water_cells.assign(4, std::vector<std::uint8_t>(4, 0));
    scene.water_terrain.actual_water_cells[2][2] = 1;

    decor::Editor editor;
    editor.open("0", scene, {}, catalog, 0, 0);
    expect(editor.place(), "editor should place a palm on free land");
    expect(!editor.place(), "editor should reject overlapping placements");
    editor.moveCursor(1, 1);
    expect(!editor.place(), "editor should reject authored collision cells");
    editor.moveCursor(1, 1);
    expect(!editor.place(), "editor should reject water cells");
    editor.moveCursor(1, 0);
    editor.rotate();
    expect(editor.place(), "editor should place a rotated palm on another free cell");
    expect(editor.placements().back().yaw_quarter_turns == 1,
        "rotation should be stored in quarter turns");

    decor::Document document;
    document.revision = 7;
    document.placements = editor.placements();
    const auto parsed = decor::parseDocument(decor::serializeDocument(document), error);
    expect(parsed && parsed->revision == 7 && parsed->placements.size() == 2,
        "world decoration documents should round-trip canonically");

    const fs::path directory = fs::temp_directory_path() / "pokemon-resort-world-decoration-tests";
    fs::remove_all(directory);
    decor::Store store(directory / "decorations.json");
    expect(store.save(document, error), "world decoration store should save transactionally");
    const auto loaded = store.load(error);
    expect(loaded && decor::serializeDocument(*loaded) == decor::serializeDocument(document),
        "world decoration store should read back the committed document");
    fs::remove_all(directory);

    if (failures == 0) std::cout << "world decoration tests passed\n";
    return failures == 0 ? 0 : 1;
}
