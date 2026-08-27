#pragma once

#include "gameplay/world3d/interiors/DefaultRoom.hpp"
#include "gameplay/world3d/terrain/TerrainSurface.hpp"

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

namespace pr::gameplay::world3d::interiors {

struct DefaultRoomFloorPoint {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct DefaultRoomFloorQuad {
    std::array<DefaultRoomFloorPoint, 4> points{};
    int source_tile_x = 0;
    int source_tile_y = 0;
};

struct DefaultRoomWallLine {
    float ax = 0.0f;
    float az = 0.0f;
    float bx = 0.0f;
    float bz = 0.0f;
};

struct DefaultRoomFloorClip {
    float x0 = 0.0f;
    float z0 = 0.0f;
    float x1 = 0.0f;
    float z1 = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
};

inline DefaultRoomFloorClip clipDefaultRoomFloorCell(
    const SceneConfig& scene,
    int tile_x,
    int tile_y,
    float tile_size) {
    DefaultRoomFloorClip clip{
        tile_x * tile_size,
        tile_y * tile_size,
        (tile_x + 1) * tile_size,
        (tile_y + 1) * tile_size};
    if (!shouldRenderDefaultRoom(scene)) return clip;

    const float cut = std::clamp(
        scene.interior.default_room.wall_face_offset_tiles, 0.0f, 1.0f);
    if (tile_x == 0 && !openingCovers(scene, "west", tile_y)) clip.u0 = cut;
    if (tile_x == scene.grid.width - 1 && !openingCovers(scene, "east", tile_y)) {
        clip.u1 = 1.0f - cut;
    }
    if (tile_y == 0 && !openingCovers(scene, "north", tile_x)) clip.v0 = cut;
    if (tile_y == scene.grid.height - 1 && !openingCovers(scene, "south", tile_x)) {
        clip.v1 = 1.0f - cut;
    }
    clip.x0 += clip.u0 * tile_size;
    clip.z0 += clip.v0 * tile_size;
    clip.x1 -= (1.0f - clip.u1) * tile_size;
    clip.z1 -= (1.0f - clip.v1) * tile_size;
    return clip;
}

inline DefaultRoomWallLine placeDefaultRoomWallLine(
    const SceneConfig& scene,
    std::string_view edge,
    float tile_size,
    float ax,
    float az,
    float bx,
    float bz) {
    const float face_offset =
        std::max(0.0f, scene.interior.default_room.wall_face_offset_tiles) * tile_size;
    const auto [normal_x, normal_z] = wallOutwardNormal(edge);
    ax -= normal_x * face_offset;
    az -= normal_z * face_offset;
    bx -= normal_x * face_offset;
    bz -= normal_z * face_offset;

    const float room_east = std::max(1, scene.grid.width) * tile_size;
    const float room_south = std::max(1, scene.grid.height) * tile_size;
    if (edge == "north" || edge == "south") {
        const float corner_cut = std::min(face_offset, room_east * 0.5f);
        ax = std::clamp(ax, corner_cut, room_east - corner_cut);
        bx = std::clamp(bx, corner_cut, room_east - corner_cut);
    } else {
        const float corner_cut = std::min(face_offset, room_south * 0.5f);
        az = std::clamp(az, corner_cut, room_south - corner_cut);
        bz = std::clamp(bz, corner_cut, room_south - corner_cut);
    }

    // The perpendicular walls clip the first and last pieces at their
    // intersection. Nothing is rescaled: shortened pieces retain the original
    // tile coordinate interval and may show only the intersecting half.
    return {ax, az, bx, bz};
}

inline std::vector<DefaultRoomFloorQuad> buildDefaultRoomFloorApron(
    const SceneConfig& scene,
    float tile_size) {
    std::vector<DefaultRoomFloorQuad> quads;
    if (!shouldRenderDefaultRoom(scene)) return quads;

    const int entry_rows = entryExtensionRows(scene);
    if (entry_rows == 0) return quads;

    const int grid_w = std::max(1, scene.grid.width);
    const int grid_h = std::max(1, scene.grid.height);
    const auto corners = [&](int x, int y) {
        std::array<float, 4> result{};
        terrain::fillTileCornerHeights(scene, x, y, result.data());
        return result;
    };
    const auto add = [&](std::array<DefaultRoomFloorPoint, 4> points,
                         int source_x,
                         int source_y) {
        quads.push_back(DefaultRoomFloorQuad{std::move(points), source_x, source_y});
    };

    quads.reserve(static_cast<std::size_t>(grid_w * entry_rows));
    for (int row = 0; row < entry_rows; ++row) {
        const float north_z = (grid_h + row) * tile_size;
        const float south_z = north_z + tile_size;
        for (int x = 0; x < grid_w; ++x) {
            if (!openingCovers(scene, "south", x)) continue;
            const auto source = corners(x, grid_h - 1);
            // Every extension cell is an independent tile-size quad. Its
            // texture therefore repeats at native scale instead of stretching
            // across the entire three-cell doorway.
            add({{{(x + 1) * tile_size, source[2], north_z},
                  {x * tile_size, source[3], north_z},
                  {x * tile_size, source[3], south_z},
                  {(x + 1) * tile_size, source[2], south_z}}},
                x, grid_h + row);
        }
    }
    return quads;
}

} // namespace pr::gameplay::world3d::interiors
