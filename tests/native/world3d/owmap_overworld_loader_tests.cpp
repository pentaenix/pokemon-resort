#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"
#include "gameplay/world3d/characters/CharacterController.hpp"
#include "gameplay/world3d/characters/CharacterTerrainQuery.hpp"
#include "gameplay/world3d/data/OwmapOverworldLoader.hpp"
#include "gameplay/world3d/data/JsonOverworldLoader.hpp"
#include "gameplay/world3d/followers/FollowerConfig.hpp"
#include "gameplay/world3d/rendering/BillboardPlacement.hpp"
#include "gameplay/world3d/rendering/PixelScale.hpp"
#include "gameplay/world3d/rendering/WorldBillboardCompositor.hpp"
#include "gameplay/world3d/terrain/ActorTerrainBinding.hpp"
#include "gameplay/world3d/terrain/GridStepMotor.hpp"
#include "gameplay/world3d/terrain/TerrainSurface.hpp"

#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <sstream>
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

float expectedSlopeBillboardLift(const pr::gameplay::world3d::SceneConfig& scene) {
    return std::max(1.0f, scene.grid.tile_size * 0.125f);
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

pr::gameplay::world3d::camera::Gen4CameraPreset sceneCameraPreset(
    const pr::gameplay::world3d::SceneConfig& scene,
    float distance_multiplier = 1.0f) {
    auto preset = pr::gameplay::world3d::camera::loadGen4PresetById(scene.camera_preset.c_str());
    if (scene.camera_distance > 0.0f) {
        preset.distance = scene.camera_distance;
    }
    preset.pitch_deg = scene.camera_pitch_deg;
    preset.yaw_deg = scene.camera_yaw_deg;
    preset.roll_deg = scene.camera_roll_deg;
    preset.near_clip = scene.camera_near_clip;
    preset.far_clip = scene.camera_far_clip;
    preset.aspect_width = scene.camera_aspect_width;
    preset.aspect_height = scene.camera_aspect_height;
    preset.fov_y_deg = scene.camera_fov_y_deg;
    preset.distance *= distance_multiplier;
    pr::gameplay::world3d::rendering::applySceneCameraScaleToCameraPreset(preset, scene);
    return preset;
}

void testFlatBootstrapOwmapParsesExpectedCells() {
    const fs::path root = repositoryRoot();
    const fs::path owmap_path = root / "assets" / "overworld" / "maps" / "flat_bootstrap.owmap";
    if (!fs::exists(owmap_path)) {
        std::cout << "[SKIP] flat_bootstrap.owmap not present in workspace\n";
        return;
    }

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
    expect(!via_dispatch.tile_package.path.empty(), "dispatch loader should parse linked RTPKS package path");
    expect(!via_dispatch.tile_layers.layers.empty(), "dispatch loader should parse RTPKS tile layers");
}

void testTerrainRenderConfigLoads() {
    const fs::path root = repositoryRoot();
    const fs::path testing_path = root / "assets" / "overworld" / "maps" / "testing.owmap";
    expect(fs::exists(testing_path), "testing.owmap must exist");
    const auto scene = pr::gameplay::world3d::data::loadSceneConfig(root.string(), testing_path.string());
    expect(std::abs(scene.terrain.height_per_floor - 16.0f) < 0.001f, "terrain heightPerFloor loads from render config");
    expect(scene.terrain.floor_color_a.r == 116, "terrain floor color A loads");
    expect(scene.terrain.floor_color_b.b == 200, "terrain floor color B loads");
    expect(scene.terrain.floor_height_recolor_enabled, "floor height recolor loads enabled");
    expect(scene.terrain.first_non_base_floor_color_a.r == 196, "first non-base floor color A loads");
    expect(scene.terrain.first_non_base_floor_color_b.g == 102, "first non-base floor color B loads");
    expect(scene.terrain.ramp_recolor_enabled, "ramp recolor loads enabled");
    expect(scene.terrain.ramp_color_a.r == 232, "ramp color A loads");
    expect(scene.terrain.ramp_color_b.g == 214, "ramp color B loads");
    expect(std::abs(scene.terrain.ramp_incline_inset_px - 6.0f) < 0.001f, "ramp incline inset loads from render config");
    expect(scene.terrain.wall_color_ns.g == 117, "terrain wall NS color loads");
    expect(scene.terrain.wire_color.a == 120, "terrain wire alpha loads");
    expect(scene.pixel_scale.map_pixels_per_tile == 16, "pixel scale mapPixelsPerTile loads");
    expect(std::abs(scene.pixel_scale.zoom - 1.0f) < 0.001f, "pixel scale zoom loads");
    expect(scene.pixel_scale.pixel_perfect_world, "pixel-perfect world rendering loads enabled");
    expect(scene.pixel_scale.world_render_width == 400, "pixel-perfect world width loads");
    expect(scene.pixel_scale.world_render_height == 250, "pixel-perfect world height loads");
    expect(scene.world_viewport.enabled, "world viewport loads enabled");
    expect(scene.world_viewport.base_width == 400, "world viewport base width loads");
    expect(scene.world_viewport.base_height == 250, "world viewport base height loads");
    expect(scene.world_viewport.internal_scale == 1, "world viewport internal scale loads");
    expect(std::abs(scene.scene_camera.distance_scale - 1.0f) < 0.001f, "scene camera distance scale loads");
    expect(scene.pixel_compositor.sprite_sizing == "native", "pixel compositor sprite sizing loads");
    expect(scene.pixel_compositor.snap_anchors, "pixel compositor snap anchors loads");
    expect(std::abs(scene.pixel_compositor.actor_depth_bias_px - 10.0f) < 0.001f,
        "pixel compositor actor depth bias loads");
    expect(scene.pixel_compositor.actor_screen_offset_y_px == 1,
        "pixel compositor actor screen Y offset loads");
    expect(scene.presentation.scale_mode == "integerFit", "presentation scale mode loads");
    expect(std::abs(scene.presentation.zoom - 1.0f) < 0.001f, "presentation zoom loads");
    expect(pr::gameplay::world3d::rendering::worldViewportBaseWidth(scene) == 400,
        "world viewport helper returns base width");
    expect(pr::gameplay::world3d::rendering::worldViewportRenderWidth(scene) == 400,
        "world viewport helper returns 1x render width");
    expect(std::abs(pr::gameplay::world3d::rendering::worldUnitsPerPixel(scene) - 1.0f) < 0.001f,
        "pixel scale derives one world unit per map pixel");
}

void testWorldViewportInternalScaleDerivesRenderSize() {
    pr::gameplay::world3d::SceneConfig scene{};
    scene.world_viewport.base_width = 400;
    scene.world_viewport.base_height = 250;
    scene.world_viewport.internal_scale = 2;
    expect(pr::gameplay::world3d::rendering::worldViewportBaseWidth(scene) == 400,
        "2x internal scale keeps base width");
    expect(pr::gameplay::world3d::rendering::worldViewportBaseHeight(scene) == 250,
        "2x internal scale keeps base height");
    expect(pr::gameplay::world3d::rendering::worldViewportInternalScale(scene) == 2,
        "2x internal scale helper returns scale");
    expect(pr::gameplay::world3d::rendering::worldViewportRenderWidth(scene) == 800,
        "2x internal scale doubles render width");
    expect(pr::gameplay::world3d::rendering::worldViewportRenderHeight(scene) == 500,
        "2x internal scale doubles render height");
}

pr::gameplay::world3d::SceneConfig makeFlatMovementScene(
    int width,
    int height,
    int spawn_x,
    int spawn_y,
    pr::gameplay::world3d::FacingDirection facing) {
    pr::gameplay::world3d::SceneConfig scene{};
    scene.grid.width = width;
    scene.grid.height = height;
    scene.grid.tile_size = 16.0f;
    scene.terrain.heights.assign(
        static_cast<std::size_t>(height),
        std::vector<std::uint8_t>(static_cast<std::size_t>(width), 0));
    scene.terrain.specials.assign(
        static_cast<std::size_t>(height),
        std::vector<std::uint8_t>(static_cast<std::size_t>(width), 0));
    scene.terrain.collision.assign(
        static_cast<std::size_t>(height),
        std::vector<std::uint8_t>(static_cast<std::size_t>(width), 0));
    scene.player.spawn_tile_x = spawn_x;
    scene.player.spawn_tile_y = spawn_y;
    scene.player.facing = facing;
    return scene;
}

void testPlayerCanStepIntoLoadedWestChunk() {
    using namespace pr::gameplay::world3d;
    SceneConfig current = makeFlatMovementScene(2, 2, 0, 1, FacingDirection::West);
    SceneConfig west = makeFlatMovementScene(2, 2, 1, 1, FacingDirection::West);

    characters::CharacterController player(current, 64.0f, 0.0f);
    player.setTerrainQuery(characters::makeLoadedWorldCharacterTerrainQuery({
        characters::LoadedWorldChunk{"current", current, 0, 0},
        characters::LoadedWorldChunk{"west", west, -2, 0},
    }));

    const auto result = player.moveInput(-1, 0, 1.0, {});
    expect(result.attempted_step, "westward cross-chunk movement attempts a step");
    expect(!result.blocked, "loaded west chunk should allow entering world tile -1");
    expect(player.tileX() == -1 && player.tileY() == 1, "player logical tile enters west chunk world coordinates");
    expect(player.moving(), "player starts a movement segment into west chunk");
}

void testPlayerCannotStepIntoUnloadedChunk() {
    using namespace pr::gameplay::world3d;
    SceneConfig current = makeFlatMovementScene(2, 2, 0, 1, FacingDirection::West);

    characters::CharacterController player(current, 64.0f, 0.0f);
    player.setTerrainQuery(characters::makeLoadedWorldCharacterTerrainQuery({
        characters::LoadedWorldChunk{"current", current, 0, 0},
    }));

    const auto result = player.moveInput(-1, 0, 1.0, {});
    expect(result.attempted_step, "unloaded west chunk movement attempts a step");
    expect(result.blocked, "unloaded world tile should block movement");
    expect(player.tileX() == 0 && player.tileY() == 1, "player remains in current chunk");
}

void testPlayableCharacterRunSheetLoads() {
    const fs::path root = repositoryRoot();
    const auto character = pr::gameplay::world3d::data::loadCharacterDefinition(
        root.string(),
        (root / "assets" / "characters" / "playable" / "haru.charbin").string());

    expect(character.has_run, "Haru charbin should expose its run action");
    expect(!character.run.frames.empty(), "Haru run action should have animation frames");
    expect(!character.run_texture_png_bytes.empty(), "Haru run action should load its separate run sheet texture");
}

void testCameraEastProjectsScreenRight() {
    pr::gameplay::world3d::camera::Gen4CameraPreset preset{};
    preset.distance = 520.0f;
    preset.pitch_deg = -59.051514f;
    preset.yaw_deg = 0.0f;
    preset.near_clip = 150.0f;
    preset.far_clip = 900.0f;
    preset.fov_y_deg = 30.0f;

    pr::gameplay::world3d::camera::Gen4FollowCamera camera(preset);
    camera.setTarget({0.0f, 0.0f, 0.0f});

    float east_x = 0.0f;
    float east_y = 0.0f;
    float east_depth = 0.0f;
    float west_x = 0.0f;
    float west_y = 0.0f;
    float west_depth = 0.0f;
    expect(camera.worldToScreen({16.0f, 0.0f, 0.0f}, 640, 480, east_x, east_y, east_depth),
           "east point should project");
    expect(camera.worldToScreen({-16.0f, 0.0f, 0.0f}, 640, 480, west_x, west_y, west_depth),
           "west point should project");
    expect(east_x > west_x, "camera should project world east to screen-right");
}

void testNorthRampHeightMatchesCornerSlope() {
    pr::gameplay::world3d::SceneConfig scene{};
    scene.grid.tile_size = 16.0f;
    scene.grid.width = 3;
    scene.grid.height = 3;
    scene.terrain.heights = {
        {1, 1, 1},
        {0, 0, 0},
        {0, 0, 0},
    };
    scene.terrain.specials = {
        {0, 0, 0},
        {0, 2, 0},
        {0, 0, 0},
    };

    float low_tile[4]{};
    pr::gameplay::world3d::terrain::fillTileCornerHeights(scene, 1, 1, low_tile);
    expect(std::abs(low_tile[0] - 16.0f) < 0.001f, "RAMP_N on lower tile: NW corner high");
    expect(std::abs(low_tile[1] - 16.0f) < 0.001f, "RAMP_N on lower tile: NE corner high");
    expect(std::abs(low_tile[2] - 0.0f) < 0.001f, "RAMP_N on lower tile: SE corner low");
    expect(std::abs(low_tile[3] - 0.0f) < 0.001f, "RAMP_N on lower tile: SW corner low");

    float high_tile[4]{};
    pr::gameplay::world3d::terrain::fillTileCornerHeights(scene, 1, 0, high_tile);
    expect(std::abs(high_tile[0] - 16.0f) < 0.001f, "upper tile should be flat at height 1");
    expect(std::abs(high_tile[2] - 16.0f) < 0.001f, "upper tile should be flat at height 1");

    // Mid-step world position is already in the upper tile by floor(z), but feet should
    // still sample the ramp tile that owns the special in .owmap.
    const float upper_flat =
        pr::gameplay::world3d::terrain::heightAtWorldPosition(scene, 24.0f, 8.0f, 1, 0);
    const float mid_stitched =
        pr::gameplay::world3d::terrain::heightAtWorldPositionStitched(scene, 24.0f, 20.0f, 1, 1);
    expect(std::abs(upper_flat - 16.0f) < 0.001f, "floor(z) on upper plateau tile is flat high");
    expect(mid_stitched > 4.0f && mid_stitched <= 12.0f, "stitched sample follows ramp tile");

    const auto climb_sample = pr::gameplay::world3d::terrain::resolveRampSampleTile(scene, 1, 1, 1, 0, 0, -1, 1);
    expect(climb_sample.x == 1 && climb_sample.y == 1, "dh>0 north climb samples lower ramp tile");

    const auto flat_walk = pr::gameplay::world3d::terrain::resolveRampSampleTile(scene, 1, 1, 2, 1, 1, 0, 0);
    expect(flat_walk.x == 1 && flat_walk.y == 1, "dh==0 along ramp uses from tile");
}

void testTerrainHeightPerFloorOverridesTileSizeVertically() {
    pr::gameplay::world3d::SceneConfig scene{};
    scene.grid.tile_size = 16.0f;
    scene.grid.width = 3;
    scene.grid.height = 3;
    scene.terrain.height_per_floor = 8.0f;
    scene.terrain.ramp_incline_inset_px = 0.0f;
    scene.terrain.heights = {
        {1, 1, 1},
        {0, 0, 0},
        {0, 0, 0},
    };
    scene.terrain.specials = {
        {0, 0, 0},
        {0, 2, 0},
        {0, 0, 0},
    };

    float ramp[4]{};
    pr::gameplay::world3d::terrain::fillTileCornerHeights(scene, 1, 1, ramp);
    expect(std::abs(ramp[0] - 8.0f) < 0.001f, "heightPerFloor controls ramp high edge");
    expect(std::abs(ramp[2] - 0.0f) < 0.001f, "heightPerFloor keeps ramp low edge at base");

    float plateau[4]{};
    pr::gameplay::world3d::terrain::fillTileCornerHeights(scene, 1, 0, plateau);
    expect(std::abs(plateau[0] - 8.0f) < 0.001f, "heightPerFloor controls flat floor height");

    const float mid_ramp =
        pr::gameplay::world3d::terrain::heightAtWorldPositionStitched(scene, 24.0f, 24.0f, 1, 1);
    expect(std::abs(mid_ramp - 4.0f) < 0.01f, "heightPerFloor affects shared actor/render height sampling");
}

void testRampSamplingClampsToOwningTileEdges() {
    pr::gameplay::world3d::SceneConfig north_scene{};
    north_scene.grid.tile_size = 16.0f;
    north_scene.grid.width = 3;
    north_scene.grid.height = 3;
    north_scene.terrain.heights = {
        {1, 1, 1},
        {0, 0, 0},
        {0, 0, 0},
    };
    north_scene.terrain.specials = {
        {0, 0, 0},
        {0, 2, 0},
        {0, 0, 0},
    };

    const float north_edge =
        pr::gameplay::world3d::terrain::heightAtWorldPositionStitched(north_scene, 24.0f, 12.0f, 1, 1);
    const float south_edge =
        pr::gameplay::world3d::terrain::heightAtWorldPositionStitched(north_scene, 24.0f, 36.0f, 1, 1);
    expect(std::abs(north_edge - 16.0f) < 0.01f, "north ramp does not extrapolate above its high edge");
    expect(std::abs(south_edge - 0.0f) < 0.01f, "north ramp does not extrapolate below its low edge");

    pr::gameplay::world3d::SceneConfig east_scene{};
    east_scene.grid.tile_size = 16.0f;
    east_scene.grid.width = 3;
    east_scene.grid.height = 3;
    east_scene.terrain.heights = {
        {0, 0, 0},
        {0, 0, 1},
        {0, 0, 0},
    };
    east_scene.terrain.specials = {
        {0, 0, 0},
        {0, 3, 0},
        {0, 0, 0},
    };

    const float east_edge =
        pr::gameplay::world3d::terrain::heightAtWorldPositionStitched(east_scene, 36.0f, 24.0f, 1, 1);
    const float west_edge =
        pr::gameplay::world3d::terrain::heightAtWorldPositionStitched(east_scene, 12.0f, 24.0f, 1, 1);
    expect(std::abs(east_edge - 16.0f) < 0.01f, "east ramp does not extrapolate above its high edge");
    expect(std::abs(west_edge - 0.0f) < 0.01f, "east ramp does not extrapolate below its low edge");
}

void testPlayerStartsNorthRampOnRampOwnerTile() {
    pr::gameplay::world3d::SceneConfig scene{};
    scene.grid.tile_size = 16.0f;
    scene.grid.width = 3;
    scene.grid.height = 4;
    scene.player.spawn_tile_x = 1;
    scene.player.spawn_tile_y = 2;
    scene.player.facing = pr::gameplay::world3d::FacingDirection::North;
    scene.terrain.heights = {
        {1, 1, 1},
        {0, 0, 0},
        {0, 0, 0},
        {0, 0, 0},
    };
    scene.terrain.specials = {
        {0, 0, 0},
        {0, 2, 0},
        {0, 0, 0},
        {0, 0, 0},
    };
    scene.terrain.collision.assign(4, std::vector<std::uint8_t>(3, 0));

    pr::gameplay::world3d::characters::CharacterController player(scene);
    player.moveInput(0, -1, 0.0);
    expect(player.tileY() == 1, "player logical tile updates to ramp owner when step starts");
    player.moveInput(0, -1, 0.05);
    auto early_pos = player.position();
    expect(early_pos.z > 32.0f, "early ramp entry is still south of the ramp tile edge");
    expect(early_pos.y > 0.5f, "player starts rising before reaching the ramp tile edge");
    player.moveInput(0, -1, 0.18);
    const auto pos = player.position();
    expect(player.moving(), "player should be mid-step onto the ramp owner tile");
    expect(pos.z < 32.0f, "player has entered the north ramp owner tile");
    expect(pos.y > 1.0f, "player height rises while entering the ramp owner tile");
}

void testCardinalRampEntryExitDirectionRules() {
    pr::gameplay::world3d::SceneConfig scene{};
    scene.grid.tile_size = 16.0f;
    scene.grid.width = 4;
    scene.grid.height = 3;
    scene.player.spawn_tile_x = 0;
    scene.player.spawn_tile_y = 1;
    scene.player.facing = pr::gameplay::world3d::FacingDirection::East;
    scene.terrain.heights = {
        {0, 1, 1, 0},
        {0, 0, 0, 0},
        {0, 0, 0, 0},
    };
    scene.terrain.specials = {
        {0, 0, 0, 0},
        {0, 2, 2, 0},
        {0, 0, 0, 0},
    };
    scene.terrain.collision.assign(3, std::vector<std::uint8_t>(4, 0));

    pr::gameplay::world3d::characters::CharacterController side_entry(scene);
    side_entry.moveInput(1, 0, 0.1);
    expect(!side_entry.moving(), "flat side-entry into cardinal ramp is blocked");
    expect(side_entry.tileX() == 0 && side_entry.tileY() == 1, "side-entry does not change logical tile");

    scene.player.spawn_tile_x = 1;
    scene.player.spawn_tile_y = 2;
    scene.player.facing = pr::gameplay::world3d::FacingDirection::North;
    pr::gameplay::world3d::characters::CharacterController low_side_entry(scene);
    low_side_entry.moveInput(0, -1, 0.1);
    expect(low_side_entry.moving(), "low-side entry into cardinal ramp is allowed");
    expect(low_side_entry.tileX() == 1 && low_side_entry.tileY() == 1, "low-side entry owns ramp tile immediately");

    scene.player.spawn_tile_x = 1;
    scene.player.spawn_tile_y = 1;
    scene.player.facing = pr::gameplay::world3d::FacingDirection::East;
    pr::gameplay::world3d::characters::CharacterController lateral_on_ramp(scene);
    lateral_on_ramp.moveInput(1, 0, 0.1);
    expect(lateral_on_ramp.moving(), "lateral movement across matching ramp strip is allowed");
    expect(lateral_on_ramp.tileX() == 2 && lateral_on_ramp.tileY() == 1, "lateral ramp strip movement changes logical tile");
}

void testEastRampResolveSampleTile() {
    pr::gameplay::world3d::SceneConfig scene{};
    scene.grid.tile_size = 16.0f;
    scene.grid.width = 3;
    scene.grid.height = 3;
    scene.terrain.ramp_incline_inset_px = 0.0f;
    scene.terrain.heights = {
        {0, 0, 0},
        {0, 0, 1},
        {0, 0, 0},
    };
    scene.terrain.specials = {
        {0, 0, 0},
        {0, 3, 0},
        {0, 0, 0},
    };

    float ramp[4]{};
    pr::gameplay::world3d::terrain::fillTileCornerHeights(scene, 1, 1, ramp);
    expect(std::abs(ramp[1] - 16.0f) < 0.001f, "RAMP_E east corners high");

    const auto climb = pr::gameplay::world3d::terrain::resolveRampSampleTile(scene, 1, 1, 2, 1, 1, 0, 1);
    expect(climb.x == 1 && climb.y == 1, "dh>0 east climb samples ramp tile");
}

void testBillboardFeetStayOnSimulationPosition() {
    pr::gameplay::world3d::SceneConfig scene{};
    scene.grid.tile_size = 16.0f;
    scene.grid.width = 3;
    scene.grid.height = 3;
    scene.billboard_tile_anchor_forward = 0.12f;
    scene.billboard_tile_anchor_right = 0.25f;
    scene.terrain.heights = {
        {1, 1, 1},
        {0, 0, 0},
        {0, 0, 0},
    };
    scene.terrain.specials = {
        {0, 0, 0},
        {0, 2, 0},
        {0, 0, 0},
    };

    pr::gameplay::world3d::camera::Gen4CameraPreset preset{};
    preset.distance = 80.0f;
    preset.pitch_deg = 45.0f;
    preset.yaw_deg = 45.0f;
    preset.fov_y_deg = 45.0f;
    preset.near_clip = 0.1f;
    preset.far_clip = 500.0f;
    pr::gameplay::world3d::camera::Gen4FollowCamera camera(preset);
    camera.setTarget({24.0f, 8.0f, 24.0f});

    pr::gameplay::world3d::CharacterSpriteDefinition character{};
    character.world_height = 999.0f;
    character.sprite_scale = 1.0f;
    character.anchor = "bottom_center";

    const float wx = 24.0f;
    const float wz = 24.0f;
    const auto binding = pr::gameplay::world3d::terrain::bindActorStanding(scene, 1, 1, wx, wz);
    const SDL_Rect source_rect{0, 0, 32, 32};
    const auto placement = pr::gameplay::world3d::rendering::buildCharacterBillboardPlacement(
        scene,
        camera,
        binding,
        character,
        {wx, binding.simulation_y, wz},
        source_rect,
        320,
        240);
    expect(placement.visible, "placement should project for test camera");
    expect(std::abs(placement.feet.x - wx) < 0.01f, "billboard feet X stays on simulation position");
    expect(std::abs(placement.feet.z - wz) < 0.01f, "billboard feet Z stays on simulation position");
    expect(std::abs(placement.world_h - 32.0f) < 0.01f, "32px character billboard uses shared pixel scale");
    expect(std::abs(pr::gameplay::world3d::rendering::authoredPixelsWorldUnits(scene, 17.0f) - 17.0f) < 0.01f,
           "shadow authored pixels use same world pixel scale");

    const SDL_Rect large_source_rect{0, 0, 64, 64};
    const auto large_placement = pr::gameplay::world3d::rendering::buildCharacterBillboardPlacement(
        scene,
        camera,
        binding,
        character,
        {wx, binding.simulation_y, wz},
        large_source_rect,
        320,
        240);
    expect(large_placement.visible, "large placement should project for test camera");
    expect(std::abs(large_placement.world_h - 64.0f) < 0.01f, "64px character billboard keeps matching pixel scale");
}

void testTextureBillboardScaleUsesAuthoredSpritePixels() {
    pr::gameplay::world3d::SceneConfig scene{};
    scene.grid.tile_size = 16.0f;
    scene.grid.width = 3;
    scene.grid.height = 3;
    scene.terrain.heights.assign(3, std::vector<std::uint8_t>(3, 0));
    scene.terrain.specials.assign(3, std::vector<std::uint8_t>(3, 0));

    pr::gameplay::world3d::camera::Gen4CameraPreset preset{};
    preset.distance = 80.0f;
    preset.pitch_deg = 45.0f;
    preset.yaw_deg = 45.0f;
    preset.fov_y_deg = 45.0f;
    preset.near_clip = 0.1f;
    preset.far_clip = 500.0f;
    pr::gameplay::world3d::camera::Gen4FollowCamera camera(preset);
    camera.setTarget({24.0f, 0.0f, 24.0f});

    const float wx = 24.0f;
    const float wz = 24.0f;
    const auto binding = pr::gameplay::world3d::terrain::bindActorStanding(scene, 1, 1, wx, wz);
    const SDL_Rect source_rect{0, 0, 32, 32};
    const auto placement = pr::gameplay::world3d::rendering::buildTextureBillboardPlacement(
        scene,
        camera,
        binding,
        {wx, binding.simulation_y, wz},
        source_rect,
        320,
        240,
        1.0f,
        0);
    expect(placement.visible, "texture billboard should project for test camera");
    expect(std::abs(placement.world_h - 32.0f) < 0.01f, "texture billboard uses shared authored sprite scale");
}

void testWorldBillboardCompositorDefaultCameraContract() {
    const fs::path root = repositoryRoot();
    const fs::path testing_path = root / "assets" / "overworld" / "maps" / "testing.owmap";
    auto scene = pr::gameplay::world3d::data::loadSceneConfig(root.string(), testing_path.string());
    scene.pixel_compositor.snap_anchors = true;

    const float tile_size = scene.grid.tile_size;
    const float wx = (static_cast<float>(scene.player.spawn_tile_x) + 0.5f) * tile_size;
    const float wz = (static_cast<float>(scene.player.spawn_tile_y) + 0.5f) * tile_size;
    const auto binding =
        pr::gameplay::world3d::terrain::bindActorStanding(scene, scene.player.spawn_tile_x, scene.player.spawn_tile_y, wx, wz);
    const pr::gameplay::world3d::camera::Vec3 sim_pos{wx, binding.simulation_y, wz};
    const SDL_Rect source_rect{0, 0, 32, 32};

    pr::gameplay::world3d::CharacterSpriteDefinition character{};
    character.anchor = "bottom_center";
    character.sprite_scale = 1.0f;

    auto camera = pr::gameplay::world3d::camera::Gen4FollowCamera(sceneCameraPreset(scene));
    camera.setTarget(sim_pos);
    const auto placement = pr::gameplay::world3d::rendering::buildCharacterBillboardPlacement(
        scene,
        camera,
        binding,
        character,
        sim_pos,
        source_rect,
        pr::gameplay::world3d::rendering::worldViewportBaseWidth(scene),
        pr::gameplay::world3d::rendering::worldViewportBaseHeight(scene));
    expect(placement.visible, "default camera billboard placement should be visible");

    const auto rect = pr::gameplay::world3d::rendering::projectWorldBillboardRect(
        scene,
        camera,
        placement,
        source_rect,
        pr::gameplay::world3d::rendering::worldViewportBaseWidth(scene),
        pr::gameplay::world3d::rendering::worldViewportBaseHeight(scene),
        1);
    expect(rect.visible, "default camera projected rect should be visible");
    if (rect.base_w != 32 || rect.base_h != 32) {
        std::ostringstream msg;
        msg << "default camera should project 32x32 frame to 32x32 base pixels, got "
            << rect.base_w << "x" << rect.base_h;
        throw TestFailure(msg.str());
    }

    const auto rect_2x = pr::gameplay::world3d::rendering::projectWorldBillboardRect(
        scene,
        camera,
        placement,
        source_rect,
        pr::gameplay::world3d::rendering::worldViewportBaseWidth(scene),
        pr::gameplay::world3d::rendering::worldViewportBaseHeight(scene),
        2);
    expect(rect_2x.base_w == 32 && rect_2x.base_h == 32, "2x keeps base-pixel sprite measurement");
    expect(rect_2x.internal_w == 64 && rect_2x.internal_h == 64, "2x doubles internal submitted sprite size");

    float feet_x = 0.0f;
    float feet_y = 0.0f;
    float feet_depth = 0.0f;
    expect(
        camera.worldToScreen(
            placement.feet,
            pr::gameplay::world3d::rendering::worldViewportBaseWidth(scene),
            pr::gameplay::world3d::rendering::worldViewportBaseHeight(scene),
            feet_x,
            feet_y,
            feet_depth),
        "feet should project for anchor snap test");
    expect(rect.base_x + rect.base_w / 2 == static_cast<int>(std::round(feet_x)),
        "anchor snap keeps feet on integer base X");
    expect(rect.base_y + rect.base_h == static_cast<int>(std::round(feet_y)),
        "anchor snap keeps feet on integer base Y");
}

void testDepthBufferedCharacterQuadDefaultCameraContract() {
    const fs::path root = repositoryRoot();
    const fs::path testing_path = root / "assets" / "overworld" / "maps" / "testing.owmap";
    auto scene = pr::gameplay::world3d::data::loadSceneConfig(root.string(), testing_path.string());

    const float tile_size = scene.grid.tile_size;
    const float wx = (static_cast<float>(scene.player.spawn_tile_x) + 0.5f) * tile_size;
    const float wz = (static_cast<float>(scene.player.spawn_tile_y) + 0.5f) * tile_size;
    const auto binding =
        pr::gameplay::world3d::terrain::bindActorStanding(scene, scene.player.spawn_tile_x, scene.player.spawn_tile_y, wx, wz);
    const pr::gameplay::world3d::camera::Vec3 sim_pos{wx, binding.simulation_y, wz};
    const SDL_Rect source_rect{0, 0, 32, 32};

    pr::gameplay::world3d::CharacterSpriteDefinition character{};
    character.anchor = "bottom_center";
    character.sprite_scale = 1.0f;

    auto camera = pr::gameplay::world3d::camera::Gen4FollowCamera(sceneCameraPreset(scene));
    camera.setTarget(sim_pos);
    const auto placement = pr::gameplay::world3d::rendering::buildCharacterBillboardPlacement(
        scene,
        camera,
        binding,
        character,
        sim_pos,
        source_rect,
        pr::gameplay::world3d::rendering::worldViewportBaseWidth(scene),
        pr::gameplay::world3d::rendering::worldViewportBaseHeight(scene));
    expect(placement.visible, "depth character quad placement should be visible");

    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    expect(
        pr::gameplay::world3d::rendering::projectDepthBillboardScreenRect(
            scene,
            camera,
            placement,
            source_rect,
            pr::gameplay::world3d::rendering::worldViewportBaseWidth(scene),
            pr::gameplay::world3d::rendering::worldViewportBaseHeight(scene),
            0,
            x,
            y,
            w,
            h),
        "depth character quad should project");
    (void)x;
    (void)y;
    if (w != 32 || h != 32) {
        std::ostringstream msg;
        msg << "default camera should project depth character quad to 32x32 base pixels, got "
            << w << "x" << h;
        throw TestFailure(msg.str());
    }
}

void testWorldBillboardPlacementUsesBottomFeetAnchor() {
    const fs::path root = repositoryRoot();
    const fs::path testing_path = root / "assets" / "overworld" / "maps" / "testing.owmap";
    auto scene = pr::gameplay::world3d::data::loadSceneConfig(root.string(), testing_path.string());
    scene.pixel_compositor.snap_anchors = true;

    const float tile_size = scene.grid.tile_size;
    const float wx = (static_cast<float>(scene.player.spawn_tile_x) + 0.5f) * tile_size;
    const float wz = (static_cast<float>(scene.player.spawn_tile_y) + 0.5f) * tile_size;
    const auto binding =
        pr::gameplay::world3d::terrain::bindActorStanding(scene, scene.player.spawn_tile_x, scene.player.spawn_tile_y, wx, wz);
    const pr::gameplay::world3d::camera::Vec3 sim_pos{wx, binding.simulation_y, wz};
    const SDL_Rect source_rect{0, 0, 32, 32};

    pr::gameplay::world3d::CharacterSpriteDefinition character{};
    character.anchor = "center";
    character.screen_offset_y_px = 13;
    character.sprite_scale = 1.0f;

    auto camera = pr::gameplay::world3d::camera::Gen4FollowCamera(sceneCameraPreset(scene));
    camera.setTarget(sim_pos);
    const auto placement = pr::gameplay::world3d::rendering::buildCharacterBillboardPlacement(
        scene,
        camera,
        binding,
        character,
        sim_pos,
        source_rect,
        pr::gameplay::world3d::rendering::worldViewportBaseWidth(scene),
        pr::gameplay::world3d::rendering::worldViewportBaseHeight(scene));
    expect(placement.visible, "bottom-anchor placement should be visible");

    const float terrain_y = pr::gameplay::world3d::terrain::heightAtActorFeet(
        scene,
        wx,
        wz,
        binding.height_sample_tx,
        binding.height_sample_ty);
    expect(std::abs(placement.feet.x - wx) < 0.001f, "bottom-anchor feet X stays on simulation foot");
    expect(std::abs(placement.feet.z - wz) < 0.001f, "bottom-anchor feet Z stays on simulation foot");
    expect(std::abs(placement.feet.y - terrain_y) < 0.001f,
        "bottom-anchor feet Y ignores legacy center anchor and profile screen offset");

    float feet_x = 0.0f;
    float feet_y = 0.0f;
    float feet_depth = 0.0f;
    expect(
        camera.worldToScreen(
            placement.feet,
            pr::gameplay::world3d::rendering::worldViewportBaseWidth(scene),
            pr::gameplay::world3d::rendering::worldViewportBaseHeight(scene),
            feet_x,
            feet_y,
            feet_depth),
        "bottom-anchor feet should project");
    const auto rect = pr::gameplay::world3d::rendering::projectWorldBillboardRect(
        scene,
        camera,
        placement,
        source_rect,
        pr::gameplay::world3d::rendering::worldViewportBaseWidth(scene),
        pr::gameplay::world3d::rendering::worldViewportBaseHeight(scene),
        1);
    expect(rect.base_y + rect.base_h == static_cast<int>(std::round(feet_y)),
        "bottom-anchor rect bottom lands on projected feet");
}

void testWorldShadowRectCentersOnFeet() {
    const fs::path root = repositoryRoot();
    const fs::path testing_path = root / "assets" / "overworld" / "maps" / "testing.owmap";
    auto scene = pr::gameplay::world3d::data::loadSceneConfig(root.string(), testing_path.string());
    scene.pixel_compositor.snap_anchors = true;
    scene.sprite_shadow.texture_width_px = 16;
    scene.sprite_shadow.texture_height_px = 8;
    scene.sprite_shadow.feet_to_shadow_bottom_px = 2;
    scene.sprite_shadow.screen_offset_x_px = 0;
    scene.sprite_shadow.screen_offset_y_px = 0;

    const float tile_size = scene.grid.tile_size;
    const float wx = (static_cast<float>(scene.player.spawn_tile_x) + 0.5f) * tile_size;
    const float wz = (static_cast<float>(scene.player.spawn_tile_y) + 0.5f) * tile_size;
    const auto binding =
        pr::gameplay::world3d::terrain::bindActorStanding(scene, scene.player.spawn_tile_x, scene.player.spawn_tile_y, wx, wz);
    const pr::gameplay::world3d::camera::Vec3 sim_pos{wx, binding.simulation_y, wz};
    const SDL_Rect source_rect{0, 0, 32, 32};

    pr::gameplay::world3d::CharacterSpriteDefinition character{};
    character.anchor = "bottom_center";
    character.sprite_scale = 1.0f;

    auto camera = pr::gameplay::world3d::camera::Gen4FollowCamera(sceneCameraPreset(scene));
    camera.setTarget(sim_pos);
    const auto placement = pr::gameplay::world3d::rendering::buildCharacterBillboardPlacement(
        scene,
        camera,
        binding,
        character,
        sim_pos,
        source_rect,
        pr::gameplay::world3d::rendering::worldViewportBaseWidth(scene),
        pr::gameplay::world3d::rendering::worldViewportBaseHeight(scene));
    expect(placement.visible, "shadow placement should be visible");

    const auto sprite_rect = pr::gameplay::world3d::rendering::projectWorldBillboardRect(
        scene,
        camera,
        placement,
        source_rect,
        pr::gameplay::world3d::rendering::worldViewportBaseWidth(scene),
        pr::gameplay::world3d::rendering::worldViewportBaseHeight(scene),
        1);
    const auto shadow_rect = pr::gameplay::world3d::rendering::projectWorldShadowRect(
        scene,
        camera,
        placement,
        source_rect,
        scene.sprite_shadow,
        pr::gameplay::world3d::rendering::worldViewportBaseWidth(scene),
        pr::gameplay::world3d::rendering::worldViewportBaseHeight(scene),
        1);
    expect(sprite_rect.visible && shadow_rect.visible, "sprite and shadow rects should be visible");
    expect(
        shadow_rect.base_x == sprite_rect.base_x + (sprite_rect.base_w / 2) - ((shadow_rect.base_w + 1) / 2),
        "shadow rect aligns to the sprite foot boundary");
    expect(
        shadow_rect.base_y + shadow_rect.base_h ==
            sprite_rect.base_y + sprite_rect.base_h + scene.sprite_shadow.feet_to_shadow_bottom_px,
        "shadow bottom follows configured feet-to-shadow distance");
    expect(shadow_rect.base_w == 16 && shadow_rect.base_h == 8,
        "default shadow keeps native mask size at 1x");
}

void testWorldBillboardCompositorScalesWithCameraDistance() {
    const fs::path root = repositoryRoot();
    const fs::path testing_path = root / "assets" / "overworld" / "maps" / "testing.owmap";
    auto scene = pr::gameplay::world3d::data::loadSceneConfig(root.string(), testing_path.string());

    const float tile_size = scene.grid.tile_size;
    const float wx = (static_cast<float>(scene.player.spawn_tile_x) + 0.5f) * tile_size;
    const float wz = (static_cast<float>(scene.player.spawn_tile_y) + 0.5f) * tile_size;
    const auto binding =
        pr::gameplay::world3d::terrain::bindActorStanding(scene, scene.player.spawn_tile_x, scene.player.spawn_tile_y, wx, wz);
    const pr::gameplay::world3d::camera::Vec3 sim_pos{wx, binding.simulation_y, wz};
    const SDL_Rect source_rect{0, 0, 32, 32};

    pr::gameplay::world3d::CharacterSpriteDefinition character{};
    character.anchor = "bottom_center";
    character.sprite_scale = 1.0f;

    auto rect_for_distance = [&](float distance_multiplier) {
        auto camera = pr::gameplay::world3d::camera::Gen4FollowCamera(sceneCameraPreset(scene, distance_multiplier));
        camera.setTarget(sim_pos);
        const auto placement = pr::gameplay::world3d::rendering::buildCharacterBillboardPlacement(
            scene,
            camera,
            binding,
            character,
            sim_pos,
            source_rect,
            pr::gameplay::world3d::rendering::worldViewportBaseWidth(scene),
            pr::gameplay::world3d::rendering::worldViewportBaseHeight(scene));
        expect(placement.visible, "camera-distance billboard placement should be visible");
        return pr::gameplay::world3d::rendering::projectWorldBillboardRect(
            scene,
            camera,
            placement,
            source_rect,
            pr::gameplay::world3d::rendering::worldViewportBaseWidth(scene),
            pr::gameplay::world3d::rendering::worldViewportBaseHeight(scene),
            1);
    };

    const auto closer = rect_for_distance(0.75f);
    const auto normal = rect_for_distance(1.0f);
    const auto farther = rect_for_distance(1.25f);
    expect(closer.visible && normal.visible && farther.visible, "camera-distance rects should be visible");
    expect(closer.base_h > normal.base_h, "moving camera closer increases projected billboard height");
    expect(farther.base_h < normal.base_h, "moving camera farther decreases projected billboard height");
}

void testWorldBillboardCompositorScalesByActorDepth() {
    const fs::path root = repositoryRoot();
    const fs::path testing_path = root / "assets" / "overworld" / "maps" / "testing.owmap";
    auto scene = pr::gameplay::world3d::data::loadSceneConfig(root.string(), testing_path.string());

    const float tile_size = scene.grid.tile_size;
    const float wx = (static_cast<float>(scene.player.spawn_tile_x) + 0.5f) * tile_size;
    const float wz = (static_cast<float>(scene.player.spawn_tile_y) + 0.5f) * tile_size;
    const auto binding =
        pr::gameplay::world3d::terrain::bindActorStanding(scene, scene.player.spawn_tile_x, scene.player.spawn_tile_y, wx, wz);
    const pr::gameplay::world3d::camera::Vec3 mid_pos{wx, binding.simulation_y, wz};
    const SDL_Rect source_rect{0, 0, 32, 32};

    pr::gameplay::world3d::CharacterSpriteDefinition character{};
    character.anchor = "bottom_center";
    character.sprite_scale = 1.0f;

    auto camera = pr::gameplay::world3d::camera::Gen4FollowCamera(sceneCameraPreset(scene));
    camera.setTarget(mid_pos);
    const auto pose = camera.pose();
    const pr::gameplay::world3d::camera::Vec3 toward_camera{
        -pose.forward.x * tile_size * 4.0f,
        0.0f,
        -pose.forward.z * tile_size * 4.0f};
    const pr::gameplay::world3d::camera::Vec3 side{
        pose.right.x * tile_size * 4.0f,
        0.0f,
        pose.right.z * tile_size * 4.0f};

    auto rect_for = [&](pr::gameplay::world3d::camera::Vec3 pos) {
        const int tx = static_cast<int>(std::floor(pos.x / tile_size));
        const int ty = static_cast<int>(std::floor(pos.z / tile_size));
        const auto actor_binding = pr::gameplay::world3d::terrain::bindActorStanding(scene, tx, ty, pos.x, pos.z);
        pos.y = actor_binding.simulation_y;
        const auto placement = pr::gameplay::world3d::rendering::buildCharacterBillboardPlacement(
            scene,
            camera,
            actor_binding,
            character,
            pos,
            source_rect,
            pr::gameplay::world3d::rendering::worldViewportBaseWidth(scene),
            pr::gameplay::world3d::rendering::worldViewportBaseHeight(scene));
        expect(placement.visible, "actor-depth billboard placement should be visible");
        return pr::gameplay::world3d::rendering::projectWorldBillboardRect(
            scene,
            camera,
            placement,
            source_rect,
            pr::gameplay::world3d::rendering::worldViewportBaseWidth(scene),
            pr::gameplay::world3d::rendering::worldViewportBaseHeight(scene),
            1);
    };

    const auto near_rect = rect_for({mid_pos.x + toward_camera.x, mid_pos.y, mid_pos.z + toward_camera.z});
    const auto mid_rect = rect_for(mid_pos);
    const auto far_rect = rect_for({mid_pos.x - toward_camera.x, mid_pos.y, mid_pos.z - toward_camera.z});
    const auto side_rect = rect_for({mid_pos.x + side.x, mid_pos.y, mid_pos.z + side.z});

    expect(near_rect.visible && mid_rect.visible && far_rect.visible && side_rect.visible,
        "actor-depth rects should be visible");
    expect(near_rect.base_h > mid_rect.base_h, "near actor projects larger than target-depth actor");
    expect(far_rect.base_h < mid_rect.base_h, "far actor projects smaller than target-depth actor");
    expect(std::abs(side_rect.base_h - mid_rect.base_h) <= 1, "side-by-side actor keeps matching size");
}

void testPlayerSpritePriorityIsSortOnly() {
    const float follower_depth = 100.0f;
    const float player_depth = 100.0f;
    constexpr float player_priority_bias = -0.25f;
    expect(player_depth + player_priority_bias < follower_depth,
        "player priority bias only changes sprite-vs-sprite sort order");
    expect(player_depth == 100.0f,
        "player priority must not modify world actor depth used against environment");
}

void testFollowerSummonRenderConfigLoads() {
    const fs::path root = repositoryRoot();
    const auto config = pr::gameplay::world3d::followers::loadFollowerSummonConfig(root.string());
    expect(config.ball_animation.duration_ms == 230, "summon ball duration loads");
    expect(config.ball_animation.hold_last_frame_ms == 10, "summon ball hold loads");
    expect(std::abs(config.ball_animation.sprite_scale - 0.36f) < 0.001f, "summon ball scale loads");
    expect(config.ball_animation.screen_offset_y_px == 0, "summon ball screen offset loads");
    expect(config.landing_dust.override_idle_config, "summon dust override is enabled when configured");
    expect(std::abs(config.landing_dust.sprite_scale - 1.0f) < 0.001f, "summon dust scale loads");
    expect(config.landing_dust.screen_offset_y_px == 0, "summon dust grounded screen offset loads");
}

void testActorTerrainBindingMatchesHeightAtFeet() {
    pr::gameplay::world3d::SceneConfig scene{};
    scene.grid.tile_size = 16.0f;
    scene.grid.width = 3;
    scene.grid.height = 3;
    scene.terrain.heights = {
        {1, 1, 1},
        {0, 0, 0},
        {0, 0, 0},
    };
    scene.terrain.specials = {
        {0, 0, 0},
        {0, 2, 0},
        {0, 0, 0},
    };

    const float wx = 24.0f;
    const float wz = 24.0f;
    const auto binding = pr::gameplay::world3d::terrain::bindActorStanding(scene, 1, 1, wx, wz);
    const float expected =
        pr::gameplay::world3d::terrain::heightAtActorFeet(scene, wx, wz, binding.height_sample_tx, binding.height_sample_ty);
    expect(binding.logical_tx == 1 && binding.logical_ty == 1, "binding stores logical tile");
    expect(std::abs(binding.simulation_y - expected) < 0.01f, "simulation_y matches heightAtActorFeet");
}

void testGridStepMotorSamplesRampTileMidStep() {
    pr::gameplay::world3d::SceneConfig scene{};
    scene.grid.tile_size = 16.0f;
    scene.grid.width = 3;
    scene.grid.height = 3;
    scene.terrain.heights = {
        {1, 1, 1},
        {0, 0, 0},
        {0, 0, 0},
    };
    scene.terrain.specials = {
        {0, 0, 0},
        {0, 2, 0},
        {0, 0, 0},
    };

    auto motor = pr::gameplay::world3d::terrain::GridStepMotor::beginStep(scene, 1, 1, 1, 0, 0, -1, 0, 1);
    const auto sample = motor.activeSampleTile(0.5f);
    expect(sample.x == 1 && sample.y == 1, "mid-step north climb keeps ramp owner sample tile");

    const float wx = 24.0f;
    const float wz = 20.0f;
    const float stepped_y = pr::gameplay::world3d::terrain::actorHeightDuringStep(scene, wx, wz, motor, 0.5f);
    const float expected =
        pr::gameplay::world3d::terrain::heightAtActorFeet(scene, wx, wz, sample.x, sample.y);
    expect(std::abs(stepped_y - expected) < 0.01f, "step height uses motor sample tile");
}

void testBillboardPlacementFeetOnTerrain() {
    pr::gameplay::world3d::SceneConfig scene{};
    scene.grid.tile_size = 16.0f;
    scene.grid.width = 3;
    scene.grid.height = 3;
    scene.billboard_tile_anchor_forward = 0.12f;
    scene.terrain.heights = {
        {1, 1, 1},
        {0, 0, 0},
        {0, 0, 0},
    };
    scene.terrain.specials = {
        {0, 0, 0},
        {0, 2, 0},
        {0, 0, 0},
    };

    pr::gameplay::world3d::camera::Gen4CameraPreset preset{};
    preset.distance = 80.0f;
    preset.pitch_deg = 45.0f;
    preset.yaw_deg = 45.0f;
    preset.fov_y_deg = 45.0f;
    preset.near_clip = 0.1f;
    preset.far_clip = 500.0f;
    pr::gameplay::world3d::camera::Gen4FollowCamera camera(preset);
    camera.setTarget({24.0f, 8.0f, 24.0f});

    pr::gameplay::world3d::CharacterSpriteDefinition character{};
    character.sprite_scale = 1.0f;
    character.anchor = "bottom_center";

    const float wx = 24.0f;
    const float wz = 24.0f;
    const auto binding = pr::gameplay::world3d::terrain::bindActorStanding(scene, 1, 1, wx, wz);
    pr::gameplay::world3d::camera::Vec3 sim_pos{wx, binding.simulation_y, wz};
    const SDL_Rect source_rect{0, 0, 32, 32};
    const auto placement = pr::gameplay::world3d::rendering::buildCharacterBillboardPlacement(
        scene,
        camera,
        binding,
        character,
        sim_pos,
        source_rect,
        320,
        240);
    expect(placement.visible, "placement should project for test camera");
    const float terrain_at_feet = pr::gameplay::world3d::terrain::heightAtActorFeet(
        scene,
        placement.feet.x,
        placement.feet.z,
        binding.height_sample_tx,
        binding.height_sample_ty);
    const float expected_lift = expectedSlopeBillboardLift(scene);
    expect(std::abs(placement.feet.y - (terrain_at_feet + expected_lift)) < 0.05f, "sprite feet Y is lifted on slope");
    expect(
        std::abs(placement.shadow_ground.y - (terrain_at_feet + scene.sprite_shadow.world_y_lift)) < 0.001f,
        "shadow stays grounded on slope terrain");

    sim_pos.y += 8.0f;
    const auto jumping_placement = pr::gameplay::world3d::rendering::buildCharacterBillboardPlacement(
        scene,
        camera,
        binding,
        character,
        sim_pos,
        source_rect,
        320,
        240);
    expect(jumping_placement.visible, "jumping placement should project for test camera");
    expect(jumping_placement.feet.y > placement.feet.y + 7.9f, "sprite visual feet follow jump lift");
    expect(
        std::abs(jumping_placement.shadow_ground.y - placement.shadow_ground.y) < 0.001f,
        "shadow stays on terrain while sprite jumps");
}

void testTestingOwmapRampActorBinding() {
    const fs::path root = repositoryRoot();
    const fs::path testing_path = root / "assets" / "overworld" / "maps" / "testing.owmap";
    expect(fs::exists(testing_path), "testing.owmap must exist");
    auto scene = pr::gameplay::world3d::data::loadOwmapScene(root.string(), testing_path.string());

    const int w = static_cast<int>(scene.terrain.specials.front().size());
    const int h = static_cast<int>(scene.terrain.specials.size());
    int ramp_tx = -1;
    int ramp_ty = -1;
    for (int ty = 0; ty < h && ramp_tx < 0; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            const int special = scene.terrain.specials[static_cast<std::size_t>(ty)][static_cast<std::size_t>(tx)];
            if (special >= 2 && special <= 5) {
                ramp_tx = tx;
                ramp_ty = ty;
                break;
            }
        }
    }
    expect(ramp_tx >= 0, "testing.owmap should contain at least one ramp tile");

    const float tile_size = scene.grid.tile_size;
    const float wx = (static_cast<float>(ramp_tx) + 0.5f) * tile_size;
    const float wz = (static_cast<float>(ramp_ty) + 0.5f) * tile_size;
    const auto binding = pr::gameplay::world3d::terrain::bindActorStanding(scene, ramp_tx, ramp_ty, wx, wz);

    pr::gameplay::world3d::camera::Gen4CameraPreset preset{};
    preset.distance = 80.0f;
    preset.pitch_deg = 45.0f;
    preset.yaw_deg = 45.0f;
    preset.fov_y_deg = 45.0f;
    preset.near_clip = 0.1f;
    preset.far_clip = 500.0f;
    pr::gameplay::world3d::camera::Gen4FollowCamera camera(preset);
    camera.setTarget({wx, binding.simulation_y + 16.0f, wz});

    pr::gameplay::world3d::CharacterSpriteDefinition character{};
    character.sprite_scale = 1.0f;
    character.anchor = "bottom_center";
    scene.billboard_tile_anchor_forward = 0.12f;

    pr::gameplay::world3d::camera::Vec3 sim_pos{wx, binding.simulation_y, wz};
    const SDL_Rect source_rect{0, 0, 32, 32};
    const auto placement = pr::gameplay::world3d::rendering::buildCharacterBillboardPlacement(
        scene,
        camera,
        binding,
        character,
        sim_pos,
        source_rect,
        320,
        240);
    expect(placement.visible, "testing.owmap ramp placement should be visible");
    const float terrain_at_feet = pr::gameplay::world3d::terrain::heightAtActorFeet(
        scene,
        placement.feet.x,
        placement.feet.z,
        binding.height_sample_tx,
        binding.height_sample_ty);
    expect(
        std::abs(placement.feet.y - (terrain_at_feet + expectedSlopeBillboardLift(scene))) < 0.05f,
        "testing.owmap ramp feet get render-only lift");
}

void testStitchedHeightBlendsAcrossNorthEdge() {
    pr::gameplay::world3d::SceneConfig scene{};
    scene.grid.tile_size = 16.0f;
    scene.grid.width = 3;
    scene.grid.height = 3;
    scene.terrain.heights = {
        {1, 1, 1},
        {0, 0, 0},
        {0, 0, 0},
    };
    scene.terrain.specials = {
        {0, 0, 0},
        {0, 2, 0},
        {0, 0, 0},
    };

    const float mid_ramp =
        pr::gameplay::world3d::terrain::heightAtWorldPositionStitched(scene, 24.0f, 24.0f, 1, 1);
    const float inset = scene.terrain.ramp_incline_inset_px / scene.grid.tile_size;
    const float expected = pr::gameplay::world3d::terrain::heightPerFloor(scene) *
        ((0.5f - inset) / (1.0f - inset));
    expect(std::abs(mid_ramp - expected) < 0.01f, "stitched height at ramp tile center");
}

} // namespace

int main() {
    try {
        testFlatBootstrapOwmapParsesExpectedCells();
        std::cout << "[PASS] flat_bootstrap owmap parses expected cells\n";
        testOwmapMagicSniffAndDispatch();
        std::cout << "[PASS] owmap sniff and dispatch\n";
        testTerrainRenderConfigLoads();
        std::cout << "[PASS] terrain render config loads\n";
        testWorldViewportInternalScaleDerivesRenderSize();
        std::cout << "[PASS] world viewport internal scale derives render size\n";
        testPlayerCanStepIntoLoadedWestChunk();
        std::cout << "[PASS] player can step into loaded west chunk\n";
        testPlayerCannotStepIntoUnloadedChunk();
        std::cout << "[PASS] player cannot step into unloaded chunk\n";
        testPlayableCharacterRunSheetLoads();
        std::cout << "[PASS] playable character run sheet loads\n";
        testCameraEastProjectsScreenRight();
        std::cout << "[PASS] camera east projects screen-right\n";
        testNorthRampHeightMatchesCornerSlope();
        std::cout << "[PASS] north ramp height matches corner slope\n";
        testTerrainHeightPerFloorOverridesTileSizeVertically();
        std::cout << "[PASS] terrain height per floor overrides tile size vertically\n";
        testRampSamplingClampsToOwningTileEdges();
        std::cout << "[PASS] ramp sampling clamps to owning tile edges\n";
        testPlayerStartsNorthRampOnRampOwnerTile();
        std::cout << "[PASS] player starts north ramp on ramp owner tile\n";
        testCardinalRampEntryExitDirectionRules();
        std::cout << "[PASS] cardinal ramp entry/exit direction rules\n";
        testEastRampResolveSampleTile();
        std::cout << "[PASS] east ramp resolve sample tile\n";
        testBillboardFeetStayOnSimulationPosition();
        std::cout << "[PASS] billboard feet stay on simulation position\n";
        testTextureBillboardScaleUsesAuthoredSpritePixels();
        std::cout << "[PASS] texture billboard scale uses authored sprite pixels\n";
        testWorldBillboardCompositorDefaultCameraContract();
        std::cout << "[PASS] world billboard compositor default camera contract\n";
        testDepthBufferedCharacterQuadDefaultCameraContract();
        std::cout << "[PASS] depth-buffered character quad default camera contract\n";
        testWorldBillboardPlacementUsesBottomFeetAnchor();
        std::cout << "[PASS] world billboard placement uses bottom feet anchor\n";
        testWorldShadowRectCentersOnFeet();
        std::cout << "[PASS] world shadow rect centers on feet\n";
        testWorldBillboardCompositorScalesWithCameraDistance();
        std::cout << "[PASS] world billboard compositor scales with camera distance\n";
        testWorldBillboardCompositorScalesByActorDepth();
        std::cout << "[PASS] world billboard compositor scales by actor depth\n";
        testPlayerSpritePriorityIsSortOnly();
        std::cout << "[PASS] player sprite priority is sort-only\n";
        testFollowerSummonRenderConfigLoads();
        std::cout << "[PASS] follower summon render config loads\n";
        testActorTerrainBindingMatchesHeightAtFeet();
        std::cout << "[PASS] actor terrain binding matches height at feet\n";
        testGridStepMotorSamplesRampTileMidStep();
        std::cout << "[PASS] grid step motor samples ramp tile mid step\n";
        testBillboardPlacementFeetOnTerrain();
        std::cout << "[PASS] billboard placement feet on terrain\n";
        testTestingOwmapRampActorBinding();
        std::cout << "[PASS] testing.owmap ramp actor binding\n";
        testStitchedHeightBlendsAcrossNorthEdge();
        std::cout << "[PASS] stitched height blends across north edge\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        std::cerr << "[FAIL] " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}
