#include "gameplay/world3d/data/OwmapOverworldLoader.hpp"
#include "gameplay/world3d/data/JsonOverworldLoader.hpp"

#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
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
    if (!condition) {
        throw TestFailure(message);
    }
}

fs::path repositoryRoot() {
    fs::path current = fs::current_path();
    while (!current.empty()) {
        if (fs::exists(current / "config" / "app.json") &&
            fs::exists(current / "assets" / "overworld" / "maps")) {
            return current;
        }
        current = current.parent_path();
    }
    throw TestFailure("Could not locate repository root from " + fs::current_path().string());
}

void testFlatBootstrapOwmapParsesExpectedCells() {
    const fs::path root = repositoryRoot();
    const fs::path owmap_path = root / "assets" / "overworld" / "maps" / "flat_bootstrap.owmap";
    expect(fs::exists(owmap_path), "flat_bootstrap.owmap must exist");

    const auto from_owmap = pr::gameplay::world3d::data::loadOwmapScene(root.string(), owmap_path.string());
    expect(from_owmap.grid.width == 16, "flat_bootstrap.owmap width should be 16");
    expect(from_owmap.grid.height == 16, "flat_bootstrap.owmap height should be 16");
    expect(std::abs(from_owmap.grid.tile_size - 16.0f) < 0.0001f, "flat_bootstrap.owmap tile_size should be 16");
    expect(from_owmap.terrain.heights[0][0] == 5, "expected height[0][0] == 5");
    expect(from_owmap.terrain.specials[0][0] == 0, "expected special[0][0] == 0");
    expect(from_owmap.terrain.collision[0][0] == 0, "expected collision[0][0] == 0");
}

void testOwmapMagicSniffAndDispatch() {
    const fs::path root = repositoryRoot();
    const fs::path testing_path = root / "assets" / "overworld" / "maps" / "testing.owmap";
    expect(fs::exists(testing_path), "testing.owmap must exist");
    expect(pr::gameplay::world3d::data::isOwmapFile(testing_path.string()), "isOwmapFile should detect testing.owmap");
    const auto via_dispatch = pr::gameplay::world3d::data::loadSceneConfig(root.string(), testing_path.string());
    expect(via_dispatch.grid.width > 0, "dispatch loader should parse owmap grid width");
    expect(via_dispatch.grid.height > 0, "dispatch loader should parse owmap grid height");
}

} // namespace

int main() {
    try {
        testFlatBootstrapOwmapParsesExpectedCells();
        std::cout << "[PASS] flat_bootstrap owmap parses expected cells\n";
        testOwmapMagicSniffAndDispatch();
        std::cout << "[PASS] owmap sniff and dispatch\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        std::cerr << "[FAIL] " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}
