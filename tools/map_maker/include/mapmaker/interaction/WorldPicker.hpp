#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"

#include <optional>

namespace pr::mapmaker {

struct WorldRay {
    gameplay::world3d::camera::Vec3 origin{};
    gameplay::world3d::camera::Vec3 direction{};
};

struct TerrainPick {
    int tile_x = -1;
    int tile_y = -1;
    gameplay::world3d::camera::Vec3 world{};
    float distance = 0.0f;
};

// Builds a world-space ray from a point in an SDL-style viewport (top-left is 0,0).
WorldRay screenRay(
    const gameplay::world3d::camera::Gen4FollowCamera& camera,
    float viewport_x,
    float viewport_y,
    int viewport_width,
    int viewport_height);

// Intersects the same two terrain triangles submitted by OverworldBgfxRenderer.
// This remains pure and allocation-free so pointer-hover picking is inexpensive.
std::optional<TerrainPick> pickTerrain(
    const gameplay::world3d::SceneConfig& scene,
    const WorldRay& ray);

std::optional<TerrainPick> pickTerrainFromScreen(
    const gameplay::world3d::SceneConfig& scene,
    const gameplay::world3d::camera::Gen4FollowCamera& camera,
    float viewport_x,
    float viewport_y,
    int viewport_width,
    int viewport_height);

} // namespace pr::mapmaker
