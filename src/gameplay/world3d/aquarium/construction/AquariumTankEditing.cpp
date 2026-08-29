#include "gameplay/world3d/aquarium/construction/AquariumTankEditing.hpp"

#include <algorithm>

namespace pr::gameplay::world3d::aquarium::construction {
namespace geo = pr::aquarium::geometry;

std::vector<geo::GridCell> tankFootprintCells(const geo::TankDesign& tank) {
    std::vector<geo::GridCell> cells;
    const int width = std::max(0, tank.footprint.width_cells);
    const int depth = std::max(0, tank.footprint.depth_cells);
    cells.reserve(static_cast<std::size_t>(width * depth));
    for (int row = tank.footprint.origin_cell.row;
         row < tank.footprint.origin_cell.row + depth; ++row) {
        for (int column = tank.footprint.origin_cell.column;
             column < tank.footprint.origin_cell.column + width; ++column) {
            cells.push_back({column, row});
        }
    }
    return cells;
}

const geo::TankDesign* playerTankAtCell(
    const AquariumDesignDocument& document, geo::GridCell cell) {
    const auto found = std::find_if(document.tanks.rbegin(), document.tanks.rend(),
        [&](const auto& tank) {
            const auto& footprint = tank.footprint;
            return cell.column >= footprint.origin_cell.column &&
                cell.column < footprint.origin_cell.column + footprint.width_cells &&
                cell.row >= footprint.origin_cell.row &&
                cell.row < footprint.origin_cell.row + footprint.depth_cells;
        });
    return found == document.tanks.rend() ? nullptr : &*found;
}

std::optional<std::size_t> playerTankIndex(
    const AquariumDesignDocument& document, const std::string& tank_id) {
    const auto found = std::find_if(document.tanks.begin(), document.tanks.end(),
        [&](const auto& tank) { return tank.id == tank_id; });
    if (found == document.tanks.end()) return std::nullopt;
    return static_cast<std::size_t>(std::distance(document.tanks.begin(), found));
}

geo::GridCell tankCentreCell(const geo::TankDesign& tank) {
    return {
        tank.footprint.origin_cell.column + (tank.footprint.width_cells - 1) / 2,
        tank.footprint.origin_cell.row + (tank.footprint.depth_cells - 1) / 2,
    };
}

geo::TankDesign moveTankByCells(const geo::TankDesign& tank, int column_delta, int row_delta) {
    geo::TankDesign moved = tank;
    moved.footprint.origin_cell.column += column_delta;
    moved.footprint.origin_cell.row += row_delta;
    return moved;
}

geo::TankDesign resizeTankToCell(
    const geo::TankDesign& tank, AquariumResizeHandle handle, geo::GridCell cell) {
    geo::TankDesign resized = tank;
    const int original_left = tank.footprint.origin_cell.column;
    const int original_top = tank.footprint.origin_cell.row;
    const int original_right = original_left + tank.footprint.width_cells;
    const int original_bottom = original_top + tank.footprint.depth_cells;
    int left = original_left;
    int top = original_top;
    int right = original_right;
    int bottom = original_bottom;

    const bool west = handle == AquariumResizeHandle::NorthWest ||
        handle == AquariumResizeHandle::West || handle == AquariumResizeHandle::SouthWest;
    const bool east = handle == AquariumResizeHandle::NorthEast ||
        handle == AquariumResizeHandle::East || handle == AquariumResizeHandle::SouthEast;
    const bool north = handle == AquariumResizeHandle::NorthWest ||
        handle == AquariumResizeHandle::North || handle == AquariumResizeHandle::NorthEast;
    const bool south = handle == AquariumResizeHandle::SouthWest ||
        handle == AquariumResizeHandle::South || handle == AquariumResizeHandle::SouthEast;
    if (west) left = std::min(cell.column, original_right - 1);
    if (east) right = std::max(cell.column + 1, original_left + 1);
    if (north) top = std::min(cell.row, original_bottom - 1);
    if (south) bottom = std::max(cell.row + 1, original_top + 1);

    resized.footprint.origin_cell = {left, top};
    resized.footprint.width_cells = right - left;
    resized.footprint.depth_cells = bottom - top;
    return resized;
}

} // namespace pr::gameplay::world3d::aquarium::construction
