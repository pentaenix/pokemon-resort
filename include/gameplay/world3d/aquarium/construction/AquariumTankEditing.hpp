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

pr::aquarium::geometry::TankDesign tankWithFootprintCells(
    const pr::aquarium::geometry::TankDesign& source,
    const std::vector<pr::aquarium::geometry::GridCell>& cells);

struct DrawnTankResolution {
    bool extends_existing = false;
    std::string primary_tank_id;
    std::vector<std::string> affected_tank_ids;
    std::vector<pr::aquarium::geometry::TankDesign> tanks;
    pr::aquarium::geometry::TankDesign preview_tank;
};

DrawnTankResolution resolveDrawnTank(
    const std::vector<pr::aquarium::geometry::TankDesign>& existing,
    const pr::aquarium::geometry::TankDesign& drawn);

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
