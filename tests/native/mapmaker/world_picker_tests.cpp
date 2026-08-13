#include "mapmaker/interaction/WorldPicker.hpp"

#include "gameplay/world3d/camera/Gen4CameraPreset.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

pr::gameplay::world3d::SceneConfig flatScene() {
    pr::gameplay::world3d::SceneConfig scene;
    scene.grid.enabled = true;
    scene.grid.width = 4;
    scene.grid.height = 3;
    scene.grid.tile_size = 16.0f;
    scene.terrain.heights.assign(3, std::vector<std::uint8_t>(4, 0));
    scene.terrain.specials.assign(3, std::vector<std::uint8_t>(4, 0));
    scene.terrain.collision.assign(3, std::vector<std::uint8_t>(4, 0));
    return scene;
}

void testVerticalRayFindsExactCell() {
    const auto scene = flatScene();
    const pr::mapmaker::WorldRay ray{{24.0f, 100.0f, 40.0f}, {0.0f, -1.0f, 0.0f}};
    const auto hit = pr::mapmaker::pickTerrain(scene, ray);
    expect(hit.has_value(), "vertical ray should hit the terrain");
    expect(hit->tile_x == 1 && hit->tile_y == 2,
        "terrain picking must use the renderer's row-major X/Z tile coordinates");
    expect(std::abs(hit->world.y) < 0.0001f, "flat terrain hit must land at y=0");
}

void testNearestRaisedTileWins() {
    auto scene = flatScene();
    scene.terrain.height_per_floor = 8.0f;
    scene.terrain.heights[1][1] = 2;
    const pr::mapmaker::WorldRay ray{{24.0f, 100.0f, 24.0f}, {0.0f, -1.0f, 0.0f}};
    const auto hit = pr::mapmaker::pickTerrain(scene, ray);
    expect(hit && hit->tile_x == 1 && hit->tile_y == 1, "raised tile should be selected");
    expect(std::abs(hit->world.y - 16.0f) < 0.0001f,
        "picker must use TerrainSurface corner heights, not a flat plane");
}

void testCenterScreenRayMatchesCameraForward() {
    auto preset = pr::gameplay::world3d::camera::loadGen4PresetById("gen4_default");
    pr::gameplay::world3d::camera::Gen4FollowCamera camera(preset);
    camera.setTarget({32.0f, 0.0f, 24.0f});
    const auto ray = pr::mapmaker::screenRay(camera, 200.0f, 125.0f, 400, 250);
    const auto pose = camera.pose();
    expect(std::abs(ray.direction.x - pose.forward.x) < 0.0001f &&
           std::abs(ray.direction.y - pose.forward.y) < 0.0001f &&
           std::abs(ray.direction.z - pose.forward.z) < 0.0001f,
        "center viewport ray must equal the exact game camera forward vector");
}

void testCardinalHaloUsesClampedBoundaryHeight() {
    auto scene = flatScene();
    scene.terrain.height_per_floor = 8.0f;
    scene.terrain.heights[1][0] = 2;
    const pr::mapmaker::WorldRay west{{-8.0f, 100.0f, 24.0f}, {0.0f, -1.0f, 0.0f}};
    const auto hit = pr::mapmaker::pickTerrain(scene, west);
    expect(hit && hit->tile_x == -1 && hit->tile_y == 1,
        "the one-cell west halo must remain pickable for outside-map doors");
    expect(std::abs(hit->world.y - 16.0f) < 0.0001f,
        "halo terrain must extend the nearest boundary height");
}

void testDiagonalHaloCornerIsNotPickable() {
    const auto scene = flatScene();
    const pr::mapmaker::WorldRay corner{{-8.0f, 100.0f, -8.0f}, {0.0f, -1.0f, 0.0f}};
    expect(!pr::mapmaker::pickTerrain(scene, corner),
        "diagonal halo corners are outside the project coordinate contract");
}

void testShallowRayTraversesToNearestTile() {
    auto scene = flatScene();
    scene.terrain.height_per_floor = 8.0f;
    scene.terrain.heights[1][2] = 2;
    const pr::mapmaker::WorldRay ray{{-20.0f, 27.0f, 24.0f}, {1.0f, -0.2f, 0.0f}};
    const auto hit = pr::mapmaker::pickTerrain(scene, ray);
    expect(hit && hit->tile_x == 2 && hit->tile_y == 1,
        "accelerated traversal must retain nearest raised-terrain intersections");
}

} // namespace

int main() {
    try {
        testVerticalRayFindsExactCell();
        testNearestRaisedTileWins();
        testCenterScreenRayMatchesCameraForward();
        testCardinalHaloUsesClampedBoundaryHeight();
        testDiagonalHaloCornerIsNotPickable();
        testShallowRayTraversesToNearestTile();
        std::cout << "world_picker_tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "world_picker_tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
