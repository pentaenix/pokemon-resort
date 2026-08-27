#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace pr::gameplay::world3d::interiors {

inline bool shouldRenderDefaultRoom(const SceneConfig& scene) {
    return scene.map_type == "interior" && scene.interior.shell_model_id.empty() &&
        scene.interior.default_room.enabled;
}

inline bool openingCovers(const SceneConfig& scene, std::string_view edge, int along) {
    const bool explicit_opening = std::any_of(
        scene.interior.openings.begin(), scene.interior.openings.end(),
        [&](const InteriorOpeningConfig& opening) {
            return opening.edge == edge && along >= opening.from && along <= opening.to;
        });
    if (explicit_opening) return true;

    // A cardinal-halo door is itself an authored boundary opening. This keeps
    // newly linked rooms usable even when an explicit opening was not added
    // separately in metadata.
    return std::any_of(
        scene.door_triggers.begin(), scene.door_triggers.end(),
        [&](const DoorTriggerConfig& door) {
            if (edge == "north") return door.tile_y < 0 && door.tile_x == along;
            if (edge == "south") return door.tile_y >= scene.grid.height && door.tile_x == along;
            if (edge == "west") return door.tile_x < 0 && door.tile_y == along;
            if (edge == "east") return door.tile_x >= scene.grid.width && door.tile_y == along;
            return false;
        });
}

inline float wallHeightTiles(const SceneConfig& scene, std::string_view edge) {
    const auto& room = scene.interior.default_room;
    return edge == "south" ? room.front_wall_height_tiles : room.wall_height_tiles;
}

// Entry extensions are authored in tile units, but they are rendered and
// traversed as complete cells. Normalizing once prevents a single floor
// texture from being stretched over a fractional or multi-cell strip.
inline int entryExtensionRows(const SceneConfig& scene) {
    if (!shouldRenderDefaultRoom(scene)) return 0;
    return std::max(0, static_cast<int>(std::lround(
        std::max(0.0f, scene.interior.default_room.entry_extension_depth_tiles))));
}

inline bool entryExtensionCoversTile(
    const SceneConfig& scene,
    int tile_x,
    int tile_y) {
    const int rows = entryExtensionRows(scene);
    return rows > 0 &&
        tile_y >= scene.grid.height &&
        tile_y < scene.grid.height + rows &&
        tile_x >= 0 && tile_x < scene.grid.width &&
        openingCovers(scene, "south", tile_x);
}

inline std::pair<int, int> defaultRoomTerrainSampleTile(
    const SceneConfig& scene,
    int tile_x,
    int tile_y) {
    if (!entryExtensionCoversTile(scene, tile_x, tile_y)) {
        return {tile_x, tile_y};
    }
    // The extension continues the last authored floor cell without adding
    // fake terrain rows to OWMAP. Runtime height sampling clamps to that source
    // cell while logical movement remains on the visible extension cell.
    return {tile_x, std::max(0, scene.grid.height - 1)};
}

inline std::pair<float, float> wallOutwardNormal(std::string_view edge) {
    if (edge == "north") return {0.0f, -1.0f};
    if (edge == "south") return {0.0f, 1.0f};
    if (edge == "west") return {-1.0f, 0.0f};
    if (edge == "east") return {1.0f, 0.0f};
    return {0.0f, 0.0f};
}

inline bool boundaryCellBlocked(const SceneConfig& scene, int tile_x, int tile_y) {
    if (!shouldRenderDefaultRoom(scene)) return false;
    if (tile_x < 0 || tile_y < 0 ||
        tile_x >= scene.grid.width || tile_y >= scene.grid.height) {
        return false;
    }
    const int inset = std::clamp(
        scene.interior.default_room.walkable_inset_tiles,
        0,
        std::max(scene.grid.width, scene.grid.height));
    if (inset == 0) return false;

    if (tile_y < inset && !openingCovers(scene, "north", tile_x)) return true;
    if (tile_y >= scene.grid.height - inset && !openingCovers(scene, "south", tile_x)) return true;
    if (tile_x < inset && !openingCovers(scene, "west", tile_y)) return true;
    if (tile_x >= scene.grid.width - inset && !openingCovers(scene, "east", tile_y)) return true;
    return false;
}

} // namespace pr::gameplay::world3d::interiors
