#include "gameplay/world3d/aquarium/construction/AquariumTankEditing.hpp"

#include "aquarium_geometry/Kernel.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <set>

namespace pr::gameplay::world3d::aquarium::construction {
namespace geo = pr::aquarium::geometry;

std::vector<geo::GridCell> tankFootprintCells(const geo::TankDesign& tank) {
    return geo::footprintCells(tank.footprint);
}

geo::TankDesign tankWithFootprintCells(
    const geo::TankDesign& source, const std::vector<geo::GridCell>& cells) {
    if (cells.empty()) return source;
    geo::TankDesign result = source;
    int min_column = cells.front().column;
    int min_row = cells.front().row;
    int max_column = min_column;
    int max_row = min_row;
    std::set<std::pair<int, int>> occupied;
    for (const auto cell : cells) {
        min_column = std::min(min_column, cell.column);
        min_row = std::min(min_row, cell.row);
        max_column = std::max(max_column, cell.column);
        max_row = std::max(max_row, cell.row);
        occupied.emplace(cell.column, cell.row);
    }
    result.footprint.shape = geo::FootprintShape::Rectangle;
    result.footprint.origin_cell = {min_column, min_row};
    result.footprint.width_cells = max_column - min_column + 1;
    result.footprint.depth_cells = max_row - min_row + 1;
    result.footprint.rotation_quarter_turns = 0;
    result.footprint.notch_width_cells = 0;
    result.footprint.notch_depth_cells = 0;
    result.footprint.subtracted_cells.clear();
    result.corner_radii.clear();
    for (int row = min_row; row <= max_row; ++row) {
        for (int column = min_column; column <= max_column; ++column) {
            if (!occupied.count({column, row})) {
                result.footprint.subtracted_cells.push_back(
                    {column - min_column, row - min_row});
            }
        }
    }
    return result;
}

DrawnTankResolution resolveDrawnTank(
    const std::vector<geo::TankDesign>& existing, const geo::TankDesign& drawn) {
    using CellKey = std::pair<int, int>;
    std::set<CellKey> merged_cells;
    for (const auto cell : tankFootprintCells(drawn)) {
        merged_cells.emplace(cell.column, cell.row);
    }
    std::vector<std::size_t> touched;
    std::vector<bool> included(existing.size(), false);
    constexpr CellKey neighbours[]{{0, 0}, {0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    bool found_connected_tank = true;
    while (found_connected_tank) {
        found_connected_tank = false;
        for (std::size_t index = 0; index < existing.size(); ++index) {
            if (included[index]) continue;
            const auto existing_cells = tankFootprintCells(existing[index]);
            const bool touches = std::any_of(
                existing_cells.begin(), existing_cells.end(), [&](geo::GridCell cell) {
                    return std::any_of(std::begin(neighbours), std::end(neighbours),
                        [&](CellKey delta) {
                            return merged_cells.count(
                                {cell.column + delta.first, cell.row + delta.second}) != 0;
                        });
                });
            if (!touches) continue;
            included[index] = true;
            touched.push_back(index);
            for (const auto cell : existing_cells) {
                merged_cells.emplace(cell.column, cell.row);
            }
            found_connected_tank = true;
        }
    }

    DrawnTankResolution resolution;
    resolution.tanks = existing;
    resolution.preview_tank = drawn;
    if (touched.empty()) {
        resolution.tanks.push_back(drawn);
        return resolution;
    }

    resolution.extends_existing = true;
    const std::size_t primary_index = touched.front();
    resolution.primary_tank_id = existing[primary_index].id;
    for (const std::size_t index : touched) {
        resolution.affected_tank_ids.push_back(existing[index].id);
    }
    std::vector<geo::GridCell> merged_list;
    merged_list.reserve(merged_cells.size());
    for (const auto [column, row] : merged_cells) {
        merged_list.push_back({column, row});
    }
    resolution.preview_tank = tankWithFootprintCells(
        existing[primary_index], merged_list);
    for (const std::size_t index : touched) {
        if (index == primary_index) continue;
        resolution.preview_tank.tunnels.insert(
            resolution.preview_tank.tunnels.end(), existing[index].tunnels.begin(),
            existing[index].tunnels.end());
    }
    resolution.tanks[primary_index] = resolution.preview_tank;
    std::sort(touched.begin(), touched.end());
    for (auto found = touched.rbegin(); found != touched.rend(); ++found) {
        if (*found != primary_index) {
            resolution.tanks.erase(
                resolution.tanks.begin() + static_cast<std::ptrdiff_t>(*found));
        }
    }
    return resolution;
}

const geo::TankDesign* playerTankAtCell(
    const AquariumDesignDocument& document, geo::GridCell cell) {
    const auto found = std::find_if(document.tanks.rbegin(), document.tanks.rend(),
        [&](const auto& tank) {
            const auto cells = tankFootprintCells(tank);
            return std::any_of(cells.begin(), cells.end(), [&](geo::GridCell occupied) {
                return occupied.column == cell.column && occupied.row == cell.row;
            });
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
        tank.footprint.origin_cell.column + (geo::occupiedWidthCells(tank.footprint) - 1) / 2,
        tank.footprint.origin_cell.row + (geo::occupiedDepthCells(tank.footprint) - 1) / 2,
    };
}

geo::GridCell resizeHandleCell(const geo::TankDesign& tank, AquariumResizeHandle handle) {
    const int left = tank.footprint.origin_cell.column;
    const int top = tank.footprint.origin_cell.row;
    const int width = geo::occupiedWidthCells(tank.footprint);
    const int depth = geo::occupiedDepthCells(tank.footprint);
    const int right = left + width - 1;
    const int bottom = top + depth - 1;
    const int centre_column = left + (width - 1) / 2;
    const int centre_row = top + (depth - 1) / 2;
    switch (handle) {
    case AquariumResizeHandle::NorthWest: return {left, top};
    case AquariumResizeHandle::North: return {centre_column, top};
    case AquariumResizeHandle::NorthEast: return {right, top};
    case AquariumResizeHandle::East: return {right, centre_row};
    case AquariumResizeHandle::SouthEast: return {right, bottom};
    case AquariumResizeHandle::South: return {centre_column, bottom};
    case AquariumResizeHandle::SouthWest: return {left, bottom};
    case AquariumResizeHandle::West: return {left, centre_row};
    }
    return {right, bottom};
}

AquariumResizeHandle nearestResizeHandle(const geo::TankDesign& tank, geo::GridCell cell) {
    constexpr std::array<AquariumResizeHandle, 8> handles{
        AquariumResizeHandle::SouthEast, AquariumResizeHandle::NorthWest,
        AquariumResizeHandle::North, AquariumResizeHandle::NorthEast,
        AquariumResizeHandle::East, AquariumResizeHandle::South,
        AquariumResizeHandle::SouthWest, AquariumResizeHandle::West,
    };
    AquariumResizeHandle nearest = handles.front();
    int nearest_distance = std::numeric_limits<int>::max();
    for (const AquariumResizeHandle handle : handles) {
        const geo::GridCell handle_cell = resizeHandleCell(tank, handle);
        const int dx = cell.column - handle_cell.column;
        const int dy = cell.row - handle_cell.row;
        const int distance = dx * dx + dy * dy;
        if (distance < nearest_distance) {
            nearest = handle;
            nearest_distance = distance;
        }
    }
    return nearest;
}

geo::TankDesign moveTankByCells(const geo::TankDesign& tank, int column_delta, int row_delta) {
    geo::TankDesign moved = tank;
    moved.footprint.origin_cell.column += column_delta;
    moved.footprint.origin_cell.row += row_delta;
    for (auto& tunnel : moved.tunnels) {
        for (auto& point : tunnel.centreline_cells) {
            point.column += column_delta;
            point.row += row_delta;
        }
    }
    return moved;
}

geo::TankDesign resizeTankToCell(
    const geo::TankDesign& tank, AquariumResizeHandle handle, geo::GridCell cell) {
    geo::TankDesign resized = tank;
    const int original_left = tank.footprint.origin_cell.column;
    const int original_top = tank.footprint.origin_cell.row;
    const int original_right = original_left + geo::occupiedWidthCells(tank.footprint);
    const int original_bottom = original_top + geo::occupiedDepthCells(tank.footprint);
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
    const int occupied_width = right - left;
    const int occupied_depth = bottom - top;
    if ((resized.footprint.rotation_quarter_turns & 1) != 0) {
        resized.footprint.width_cells = occupied_depth;
        resized.footprint.depth_cells = occupied_width;
    } else {
        resized.footprint.width_cells = occupied_width;
        resized.footprint.depth_cells = occupied_depth;
    }
    if (!tank.footprint.subtracted_cells.empty() &&
        tank.footprint.rotation_quarter_turns == 0) {
        resized.footprint.subtracted_cells.clear();
        for (const geo::GridCell cut : tank.footprint.subtracted_cells) {
            const int world_column = original_left + cut.column;
            const int world_row = original_top + cut.row;
            const geo::GridCell shifted{world_column - left, world_row - top};
            if (shifted.column >= 0 && shifted.row >= 0 &&
                shifted.column < resized.footprint.width_cells &&
                shifted.row < resized.footprint.depth_cells) {
                resized.footprint.subtracted_cells.push_back(shifted);
            }
        }
    }
    if (left != original_left || top != original_top ||
        right != original_right || bottom != original_bottom) {
        resized.corner_radii.clear();
    }
    return resized;
}

} // namespace pr::gameplay::world3d::aquarium::construction
