#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include "gameplay/world3d/aquarium/AquariumPokemonMetrics.hpp"
#include "gameplay/world3d/aquarium/rooms/AquariumBuildingLayout.hpp"

namespace pr::gameplay::world3d::aquarium::rooms {

inline constexpr std::string_view kRoomDecorationCategories[]{
    "structures", "furniture", "nature", "equipment"};
std::string roomDecorationCategory(const std::string& asset_id);

enum class RoomDecorationCollisionMode { SolidBounds, CellMask, None };
enum class RoomDecorationSurfaceEffect { None, ShallowWater };

struct RoomDecorationCollisionProfile {
    RoomDecorationCollisionMode mode=RoomDecorationCollisionMode::SolidBounds;
    // Local cells are relative to the decoration's placement cell at zero
    // rotation. They rotate with the visual model around that cell.
    std::vector<Cell> blocked_cells;
};

struct RoomDecorationAsset {
    std::string id;
    std::string category;
    std::filesystem::path path;
    AquariumPokemonMetrics bounds;
    RoomDecorationCollisionProfile collision;
    RoomDecorationSurfaceEffect surface_effect=RoomDecorationSurfaceEffect::None;
    float base_scale=1.0f;
    // Signed playback permits correcting source clips whose exported keyframe
    // order runs opposite to their intended physical motion.
    float animation_playback_rate=1.0f;
    bool measured=false;
};

// The single collision-footprint projection used by placement validation,
// player collision and navigation consumers.
std::vector<Cell> roomDecorationCollisionCells(
    const RoomDecorationAsset& asset,
    Cell placement_cell,
    int yaw_quarter_turns,
    float tile_world_units);

// Placement remains footprint-aware even for walkable props such as floor
// fountains. Collision and editor occupancy are intentionally independent.
std::vector<Cell> roomDecorationPlacementCells(
    const RoomDecorationAsset& asset,
    Cell placement_cell,
    int yaw_quarter_turns,
    float tile_world_units);

class RoomDecorationCatalog {
public:
    void scan(const std::filesystem::path& project_root);
    const std::vector<RoomDecorationAsset>& entries() const { return entries_; }
    std::vector<std::size_t> indices(std::string_view category) const;
    const RoomDecorationAsset* resolve(const std::string& id) const;
    const std::string& error() const { return error_; }

private:
    mutable std::vector<RoomDecorationAsset> entries_;
    mutable std::string error_;
};

} // namespace pr::gameplay::world3d::aquarium::rooms
