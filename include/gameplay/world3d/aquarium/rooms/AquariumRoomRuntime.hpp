#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/aquarium/AquariumConfig.hpp"
#include "gameplay/world3d/aquarium/rooms/AquariumBuildingLayout.hpp"

namespace pr::gameplay::world3d::aquarium::rooms {
// Initial adapter is deliberately limited to empty, procedural aquarium rooms.
BuildingLayout roomLayoutFromScene(const SceneConfig& scene);
SceneConfig projectRoomLayout(const SceneConfig& source, const RoomLayout& room);
SceneConfig projectBuildingRoom(const SceneConfig& style, const BuildingLayout&, const RoomLayout&);
LayoutProposal proposeWallMove(const BuildingLayout&, const std::string& room_id,
    Wall wall, int coordinate, const std::vector<RoomOccupancy>& occupancy = {});
AquariumConstructionConfig roomConstructionConfig(
    const SceneConfig&, const AquariumConstructionConfig& style, bool regenerate_mask);
bool roomDrawingCellFits(const SceneConfig&, int column, int row);
} // namespace pr::gameplay::world3d::aquarium::rooms
