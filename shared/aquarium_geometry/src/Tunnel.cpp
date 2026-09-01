#include "Tunnel.hpp"

#include "aquarium_geometry/Kernel.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <utility>

namespace pr::aquarium::geometry::detail {
namespace {

using CellKey = std::pair<std::int32_t, std::int32_t>;

bool same(GridCell lhs, GridCell rhs) {
    return lhs.column == rhs.column && lhs.row == rhs.row;
}

GridCell delta(GridCell from, GridCell to) {
    return {to.column - from.column, to.row - from.row};
}

void error(std::vector<ValidationDiagnostic>& out, std::string code,
           std::string path, std::string message) {
    out.push_back({DiagnosticSeverity::Error, std::move(code),
        std::move(path), std::move(message)});
}

Vec2 localCellCentre(const FootprintDesign& footprint, GridCell cell) {
    return {
        cellCentreWorld(cell.column) -
            footprintCentreWorld(footprint.origin_cell.column, occupiedWidthCells(footprint)),
        cellCentreWorld(cell.row) -
            footprintCentreWorld(footprint.origin_cell.row, occupiedDepthCells(footprint)),
    };
}

} // namespace

std::vector<ValidationDiagnostic> validateAndResolveTunnels(
    const TankDesign& tank, std::vector<ResolvedTunnel>* resolved) {
    std::vector<ValidationDiagnostic> diagnostics;
    if (resolved) resolved->clear();
    if (tank.tunnels.empty()) return diagnostics;
    if (tank.footprint.shape != FootprintShape::Rectangle ||
        !tank.footprint.subtracted_cells.empty() ||
        tank.footprint.rotation_quarter_turns != 0) {
        error(diagnostics, "tunnel_footprint_not_supported", "/tank/tunnels",
            "This tunnel checkpoint requires an unrotated rectangular tank");
        return diagnostics;
    }
    if (tank.corner_radius_steps != 0 ||
        std::any_of(tank.corner_radii.begin(), tank.corner_radii.end(),
            [](const auto& radius) { return radius.radius_steps != 0; })) {
        error(diagnostics, "tunnel_requires_square_corners", "/tank/tunnels",
            "This tunnel checkpoint requires square tank corners");
        return diagnostics;
    }
    const auto footprint_cells = footprintCells(tank.footprint);
    std::set<CellKey> occupied;
    for (const GridCell cell : footprint_cells) occupied.emplace(cell.column, cell.row);
    std::set<std::string> ids;
    std::set<CellKey> used_cells;
    for (std::size_t index = 0; index < tank.tunnels.size(); ++index) {
        const TunnelDesign& tunnel = tank.tunnels[index];
        const std::string path = "/tank/tunnels/" + std::to_string(index);
        bool valid = true;
        if (tunnel.id.empty() || !ids.insert(tunnel.id).second) {
            error(diagnostics, "invalid_tunnel_id", path + "/id",
                "Tunnel IDs must be stable, non-empty, and unique");
            valid = false;
        }
        if (tunnel.centreline_cells.size() < 3U) {
            error(diagnostics, "tunnel_route_too_short", path + "/centrelineCells",
                "A tunnel route needs two portals and at least one interior cell");
            valid = false;
        }
        std::vector<GridCell> cells;
        cells.reserve(tunnel.centreline_cells.size());
        std::set<CellKey> route_cells;
        int turns = 0;
        GridCell previous_direction{};
        for (std::size_t cell_index = 0; cell_index < tunnel.centreline_cells.size(); ++cell_index) {
            const auto point = tunnel.centreline_cells[cell_index];
            const GridCell cell{point.column, point.row};
            cells.push_back(cell);
            if (!occupied.count({cell.column, cell.row})) {
                error(diagnostics, "tunnel_outside_footprint", path + "/centrelineCells",
                    "Every tunnel cell must remain inside the tank footprint");
                valid = false;
            }
            if (!route_cells.emplace(cell.column, cell.row).second) {
                error(diagnostics, "tunnel_self_intersection", path + "/centrelineCells",
                    "A tunnel route cannot revisit a cell");
                valid = false;
            }
            if (used_cells.count({cell.column, cell.row})) {
                error(diagnostics, "tunnel_intersection", path + "/centrelineCells",
                    "Tunnel routes cannot cross or overlap");
                valid = false;
            }
            if (cell_index == 0) continue;
            const GridCell direction = delta(cells[cell_index - 1U], cell);
            if (std::abs(direction.column) + std::abs(direction.row) != 1) {
                error(diagnostics, "tunnel_route_not_contiguous", path + "/centrelineCells",
                    "Tunnel cells must form an ordered orthogonal route");
                valid = false;
            }
            if (cell_index > 1U && !same(direction, previous_direction)) ++turns;
            previous_direction = direction;
        }
        const int expected_turns = tunnel.route == TunnelRoute::Straight ? 0 : 1;
        if (turns != expected_turns) {
            error(diagnostics, "tunnel_route_shape_mismatch", path + "/route",
                "The authored route must be straight or contain exactly one elbow");
            valid = false;
        }
        GridCell entry_outward{};
        GridCell exit_outward{};
        if (cells.size() >= 2U) {
            const GridCell entry_inward = delta(cells[0], cells[1]);
            const GridCell exit_outward_candidate = delta(cells[cells.size() - 2U], cells.back());
            entry_outward = {-entry_inward.column, -entry_inward.row};
            exit_outward = exit_outward_candidate;
            const GridCell entry_exterior{cells.front().column + entry_outward.column,
                cells.front().row + entry_outward.row};
            const GridCell exit_exterior{cells.back().column + exit_outward.column,
                cells.back().row + exit_outward.row};
            if (occupied.count({entry_exterior.column, entry_exterior.row}) ||
                occupied.count({exit_exterior.column, exit_exterior.row})) {
                error(diagnostics, "invalid_tunnel_portal", path + "/centrelineCells",
                    "Both tunnel endpoints must face outward through the tank boundary");
                valid = false;
            }
            for (std::size_t cell_index = 1; cell_index + 1U < cells.size(); ++cell_index) {
                const GridCell cell = cells[cell_index];
                constexpr GridCell neighbours[]{{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
                if (std::any_of(std::begin(neighbours), std::end(neighbours), [&](GridCell d) {
                        return !occupied.count({cell.column + d.column, cell.row + d.row});
                    })) {
                    error(diagnostics, "tunnel_interior_touches_boundary",
                        path + "/centrelineCells",
                        "Only tunnel portal cells may touch the tank boundary");
                    valid = false;
                    break;
                }
            }
        }
        if (!valid) continue;
        used_cells.insert(route_cells.begin(), route_cells.end());
        if (!resolved) continue;
        ResolvedTunnel item;
        item.design = &tunnel;
        item.cells = cells;
        item.entry_outward = entry_outward;
        item.exit_outward = exit_outward;
        const Vec2 first = localCellCentre(tank.footprint, cells.front());
        item.route_local_world.push_back({
            first.x + entry_outward.column * kWorldUnitsPerCell * 0.5F,
            first.y + entry_outward.row * kWorldUnitsPerCell * 0.5F});
        GridCell prior_direction = delta(cells[0], cells[1]);
        for (std::size_t cell_index = 2; cell_index < cells.size(); ++cell_index) {
            const GridCell direction = delta(cells[cell_index - 1U], cells[cell_index]);
            if (!same(direction, prior_direction)) {
                item.route_local_world.push_back(
                    localCellCentre(tank.footprint, cells[cell_index - 1U]));
            }
            prior_direction = direction;
        }
        const Vec2 last = localCellCentre(tank.footprint, cells.back());
        item.route_local_world.push_back({
            last.x + exit_outward.column * kWorldUnitsPerCell * 0.5F,
            last.y + exit_outward.row * kWorldUnitsPerCell * 0.5F});
        resolved->push_back(std::move(item));
    }
    return diagnostics;
}

} // namespace pr::aquarium::geometry::detail
