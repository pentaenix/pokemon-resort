#include "CellRegions.hpp"

#include "aquarium_geometry/Kernel.hpp"

#include <algorithm>
#include <map>
#include <queue>
#include <set>
#include <utility>

namespace pr::aquarium::geometry::detail {
namespace {

using CellKey = std::pair<std::int32_t, std::int32_t>;

bool sameCell(GridCell lhs, GridCell rhs) {
    return lhs.column == rhs.column && lhs.row == rhs.row;
}

} // namespace

std::vector<std::vector<GridCell>> connectedCellRegions(
    const std::vector<GridCell>& cells) {
    std::set<CellKey> remaining;
    for (const GridCell cell : cells) remaining.emplace(cell.column, cell.row);
    std::vector<std::vector<GridCell>> regions;
    constexpr CellKey neighbours[]{{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    while (!remaining.empty()) {
        std::queue<CellKey> pending;
        pending.push(*remaining.begin());
        remaining.erase(remaining.begin());
        std::vector<GridCell> region;
        while (!pending.empty()) {
            const CellKey current = pending.front();
            pending.pop();
            region.push_back({current.first, current.second});
            for (const CellKey delta : neighbours) {
                const CellKey next{current.first + delta.first, current.second + delta.second};
                const auto found = remaining.find(next);
                if (found == remaining.end()) continue;
                pending.push(next);
                remaining.erase(found);
            }
        }
        std::sort(region.begin(), region.end(), [](GridCell lhs, GridCell rhs) {
            return lhs.row < rhs.row || (lhs.row == rhs.row && lhs.column < rhs.column);
        });
        regions.push_back(std::move(region));
    }
    return regions;
}

std::vector<Vec2> cellRegionBoundaryLocalWorld(
    const std::vector<GridCell>& cells,
    const FootprintDesign& tank_footprint) {
    std::set<CellKey> occupied;
    for (const GridCell cell : cells) occupied.emplace(cell.column, cell.row);
    std::map<CellKey, CellKey> edges;
    const auto add = [&](CellKey start, CellKey end) { edges.emplace(start, end); };
    for (const auto [column, row] : occupied) {
        if (!occupied.count({column, row - 1})) add({column, row}, {column + 1, row});
        if (!occupied.count({column + 1, row})) add({column + 1, row}, {column + 1, row + 1});
        if (!occupied.count({column, row + 1})) add({column + 1, row + 1}, {column, row + 1});
        if (!occupied.count({column - 1, row})) add({column, row + 1}, {column, row});
    }
    if (edges.empty()) return {};
    const CellKey start = edges.begin()->first;
    std::vector<CellKey> raw{start};
    CellKey current = start;
    do {
        const auto found = edges.find(current);
        if (found == edges.end()) return {};
        current = found->second;
        if (current != start) raw.push_back(current);
    } while (current != start && raw.size() <= edges.size());
    std::vector<CellKey> simplified;
    for (std::size_t index = 0; index < raw.size(); ++index) {
        const CellKey previous = raw[(index + raw.size() - 1U) % raw.size()];
        const CellKey point = raw[index];
        const CellKey next = raw[(index + 1U) % raw.size()];
        if ((previous.first == point.first && point.first == next.first) ||
            (previous.second == point.second && point.second == next.second)) continue;
        simplified.push_back(point);
    }
    const float center_x = footprintCentreWorld(
        tank_footprint.origin_cell.column, occupiedWidthCells(tank_footprint));
    const float center_z = footprintCentreWorld(
        tank_footprint.origin_cell.row, occupiedDepthCells(tank_footprint));
    std::vector<Vec2> boundary;
    boundary.reserve(simplified.size());
    for (const CellKey point : simplified) {
        boundary.push_back({
            static_cast<float>(point.first * kWorldUnitsPerCell) - center_x,
            static_cast<float>(point.second * kWorldUnitsPerCell) - center_z,
        });
    }
    return boundary;
}

} // namespace pr::aquarium::geometry::detail
