#pragma once

#include "gameplay/world3d/aquarium/construction/AquariumDesign.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::aquarium::construction {

enum class AquariumResizeHandle {
    NorthWest,
    North,
    NorthEast,
    East,
    SouthEast,
    South,
    SouthWest,
    West,
};

std::vector<pr::aquarium::geometry::GridCell> tankFootprintCells(
    const pr::aquarium::geometry::TankDesign& tank);

const pr::aquarium::geometry::TankDesign* playerTankAtCell(
    const AquariumDesignDocument& document,
    pr::aquarium::geometry::GridCell cell);

std::optional<std::size_t> playerTankIndex(
    const AquariumDesignDocument& document,
    const std::string& tank_id);

pr::aquarium::geometry::GridCell tankCentreCell(
    const pr::aquarium::geometry::TankDesign& tank);

pr::aquarium::geometry::GridCell resizeHandleCell(
    const pr::aquarium::geometry::TankDesign& tank,
    AquariumResizeHandle handle);

AquariumResizeHandle nearestResizeHandle(
    const pr::aquarium::geometry::TankDesign& tank,
    pr::aquarium::geometry::GridCell cell);

pr::aquarium::geometry::TankDesign moveTankByCells(
    const pr::aquarium::geometry::TankDesign& tank,
    int column_delta,
    int row_delta);

pr::aquarium::geometry::TankDesign resizeTankToCell(
    const pr::aquarium::geometry::TankDesign& tank,
    AquariumResizeHandle handle,
    pr::aquarium::geometry::GridCell cell);

} // namespace pr::gameplay::world3d::aquarium::construction
