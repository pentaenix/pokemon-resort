#include "Tunnel.hpp"

#include "CellRegions.hpp"
#include "aquarium_geometry/Kernel.hpp"

#include <algorithm>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace pr::aquarium::geometry {
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

Vec2 localWalkingCellCentre(const FootprintDesign& footprint, GridCell cell) {
    return {
        cellCentreWorld(cell.column) -
            footprintCentreWorld(footprint.origin_cell.column, occupiedWidthCells(footprint)) -
            static_cast<float>(kPlacementOffsetWorldUnits),
        cellCentreWorld(cell.row) -
            footprintCentreWorld(footprint.origin_cell.row, occupiedDepthCells(footprint)) -
            static_cast<float>(kPlacementOffsetWorldUnits),
    };
}

bool interiorNode(const FootprintDesign& footprint, GridCell cell) {
    const std::int32_t left = footprint.origin_cell.column;
    const std::int32_t top = footprint.origin_cell.row;
    const std::int32_t right = left + occupiedWidthCells(footprint);
    const std::int32_t bottom = top + occupiedDepthCells(footprint);
    return cell.column > left && cell.column < right &&
        cell.row > top && cell.row < bottom;
}

std::optional<GridCell> portalOutward(
    const FootprintDesign& footprint, GridCell cell) {
    const std::int32_t left = footprint.origin_cell.column;
    const std::int32_t top = footprint.origin_cell.row;
    const std::int32_t right = left + occupiedWidthCells(footprint);
    const std::int32_t bottom = top + occupiedDepthCells(footprint);
    if (cell.row == top && cell.column > left && cell.column < right) return GridCell{0, -1};
    if (cell.column == right && cell.row > top && cell.row < bottom) return GridCell{1, 0};
    if (cell.row == bottom && cell.column > left && cell.column < right) return GridCell{0, 1};
    if (cell.column == left && cell.row > top && cell.row < bottom) return GridCell{-1, 0};
    return std::nullopt;
}

float cornerRadiusWorld(const TankDesign& tank, GridCell vertex) {
    const auto override = std::find_if(tank.corner_radii.begin(), tank.corner_radii.end(),
        [&](const CornerRadiusDesign& radius) {
            return radius.vertex.column == vertex.column &&
                radius.vertex.row == vertex.row;
        });
    const std::int32_t requested = override == tank.corner_radii.end()
        ? tank.corner_radius_steps : override->radius_steps;
    return static_cast<float>(fittedCornerRadiusStepsAt(
        tank.footprint, vertex, requested) * kRadiusStepWorldUnits);
}

} // namespace

bool tunnelPortalFitsBoundary(const TankDesign& tank, GridCell portal) {
    const auto outward = portalOutward(tank.footprint, portal);
    if (!outward) return false;
    const int left = tank.footprint.origin_cell.column;
    const int top = tank.footprint.origin_cell.row;
    const int width = occupiedWidthCells(tank.footprint);
    const int depth = occupiedDepthCells(tank.footprint);
    GridCell first_corner;
    GridCell second_corner;
    float position = 0.0F;
    float length = 0.0F;
    if (outward->row != 0) {
        first_corner = {0, outward->row < 0 ? 0 : depth};
        second_corner = {width, first_corner.row};
        position = static_cast<float>(portal.column - left) * kWorldUnitsPerCell;
        length = static_cast<float>(width * kWorldUnitsPerCell);
    } else {
        first_corner = {outward->column < 0 ? 0 : width, 0};
        second_corner = {first_corner.column, depth};
        position = static_cast<float>(portal.row - top) * kWorldUnitsPerCell;
        length = static_cast<float>(depth * kWorldUnitsPerCell);
    }
    const float opening_half_width = static_cast<float>(kTunnelOuterHalfWidthWorldUnits);
    constexpr float kTolerance = 0.001F;
    return position + kTolerance >= cornerRadiusWorld(tank, first_corner) + opening_half_width &&
        length - position + kTolerance >=
            cornerRadiusWorld(tank, second_corner) + opening_half_width;
}

namespace detail {

TunnelHalfCellLayout buildTunnelHalfCellLayout(
    const FootprintDesign& footprint,
    const std::vector<ResolvedTunnel>& tunnels) {
    constexpr float kHalfCellWorldUnits =
        static_cast<float>(kWorldUnitsPerCell) * 0.5F;
    constexpr int kTunnelHalfCellWidth =
        kTunnelOuterHalfWidthWorldUnits * 2 /
        static_cast<int>(kHalfCellWorldUnits);
    constexpr int kTunnelHalfCellInset = kTunnelHalfCellWidth / 2;
    const int width = occupiedWidthCells(footprint) * 2;
    const int depth = occupiedDepthCells(footprint) * 2;
    std::set<CellKey> dry_union;
    TunnelHalfCellLayout layout;
    layout.dry_by_tunnel.resize(tunnels.size());
    for (std::size_t tunnel_index = 0; tunnel_index < tunnels.size(); ++tunnel_index) {
        std::set<CellKey> tunnel_dry;
        for (const GridCell node : tunnels[tunnel_index].cells) {
            const int left = 2 * (node.column - footprint.origin_cell.column) -
                kTunnelHalfCellInset;
            const int top = 2 * (node.row - footprint.origin_cell.row) -
                kTunnelHalfCellInset;
            for (int row = top; row < top + kTunnelHalfCellWidth; ++row) {
                for (int column = left; column < left + kTunnelHalfCellWidth; ++column) {
                    if (column < 0 || row < 0 || column >= width || row >= depth) continue;
                    tunnel_dry.emplace(column, row);
                    dry_union.emplace(column, row);
                }
            }
        }
        for (const auto [column, row] : tunnel_dry) {
            layout.dry_by_tunnel[tunnel_index].push_back({column, row});
        }
    }
    for (int row = 0; row < depth; ++row) {
        for (int column = 0; column < width; ++column) {
            if (dry_union.count({column, row})) {
                layout.dry.push_back({column, row});
            } else {
                layout.water.push_back({column, row});
            }
        }
    }
    layout.dry_count = dry_union.size();
    return layout;
}

std::vector<Vec2> tunnelHalfCellBoundary(
    const std::vector<GridCell>& cells,
    const FootprintDesign& footprint) {
    constexpr float kHalfCellWorldUnits =
        static_cast<float>(kWorldUnitsPerCell) * 0.5F;
    return cellRegionBoundaryLocalWorld(
        cells, kHalfCellWorldUnits,
        static_cast<float>(occupiedWidthCells(footprint)) *
            static_cast<float>(kWorldUnitsPerCell) * 0.5F,
        static_cast<float>(occupiedDepthCells(footprint)) *
            static_cast<float>(kWorldUnitsPerCell) * 0.5F);
}

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
    if (tank.height_steps < 6) {
        error(diagnostics, "tunnel_tank_too_short", "/tank/heightSteps",
            "Tunnels require a tank at least three vertical levels tall");
        return diagnostics;
    }
    std::set<std::string> ids;
    std::vector<std::set<CellKey>> route_cell_sets(tank.tunnels.size());
    for (std::size_t index = 0; index < tank.tunnels.size(); ++index) {
        for (const CellPoint point : tank.tunnels[index].centreline_cells) {
            route_cell_sets[index].emplace(point.column, point.row);
        }
    }
    const auto usedByOtherRoute = [&](std::size_t own_index, GridCell cell) {
        for (std::size_t other = 0; other < route_cell_sets.size(); ++other) {
            if (other != own_index && route_cell_sets[other].count(
                    {cell.column, cell.row})) return true;
        }
        return false;
    };
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
            const bool endpoint = cell_index == 0 ||
                cell_index + 1U == tunnel.centreline_cells.size();
            const bool portal = portalOutward(tank.footprint, cell).has_value();
            const bool junction = endpoint && interiorNode(tank.footprint, cell) &&
                usedByOtherRoute(index, cell);
            if (!(endpoint ? portal || junction : interiorNode(tank.footprint, cell))) {
                error(diagnostics, "tunnel_outside_footprint", path + "/centrelineCells",
                    "Tunnel endpoints must be wall portals or existing interior junctions, and intermediate points must remain inside the tank");
                valid = false;
            }
            if (!route_cells.emplace(cell.column, cell.row).second) {
                error(diagnostics, "tunnel_self_intersection", path + "/centrelineCells",
                    "A tunnel route cannot revisit a cell");
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
        std::optional<GridCell> entry_outward;
        std::optional<GridCell> exit_outward;
        if (cells.size() >= 2U) {
            const GridCell entry_inward = delta(cells[0], cells[1]);
            const GridCell exit_outward_candidate = delta(cells[cells.size() - 2U], cells.back());
            const auto expected_entry = portalOutward(tank.footprint, cells.front());
            const auto expected_exit = portalOutward(tank.footprint, cells.back());
            const GridCell entry_candidate{-entry_inward.column, -entry_inward.row};
            const bool entry_junction = interiorNode(tank.footprint, cells.front()) &&
                usedByOtherRoute(index, cells.front());
            const bool exit_junction = interiorNode(tank.footprint, cells.back()) &&
                usedByOtherRoute(index, cells.back());
            if ((expected_entry && !same(entry_candidate, *expected_entry)) ||
                (expected_exit && !same(exit_outward_candidate, *expected_exit)) ||
                (!expected_entry && !entry_junction) ||
                (!expected_exit && !exit_junction) ||
                (!expected_entry && !expected_exit)) {
                error(diagnostics, "invalid_tunnel_portal", path + "/centrelineCells",
                    "A route needs at least one correctly approached wall portal; its other end may join an existing tunnel");
                valid = false;
            }
            if (expected_entry) entry_outward = entry_candidate;
            if (expected_exit) exit_outward = exit_outward_candidate;
            if ((expected_entry && !tunnelPortalFitsBoundary(tank, cells.front())) ||
                (expected_exit && !tunnelPortalFitsBoundary(tank, cells.back()))) {
                error(diagnostics, "tunnel_portal_intersects_rounding",
                    path + "/centrelineCells",
                    "Reduce the nearby corner roundness or move the tunnel portal");
                valid = false;
            }
        }
        if (!valid) continue;
        if (!resolved) continue;
        ResolvedTunnel item;
        item.design = &tunnel;
        item.cells = cells;
        item.entry_outward = entry_outward;
        item.exit_outward = exit_outward;
        item.route_local_world.push_back(
            localWalkingCellCentre(tank.footprint, cells.front()));
        GridCell prior_direction = delta(cells[0], cells[1]);
        for (std::size_t cell_index = 2; cell_index < cells.size(); ++cell_index) {
            const GridCell direction = delta(cells[cell_index - 1U], cells[cell_index]);
            if (!same(direction, prior_direction)) {
                item.route_local_world.push_back(
                    localWalkingCellCentre(tank.footprint, cells[cell_index - 1U]));
            }
            prior_direction = direction;
        }
        item.route_local_world.push_back(
            localWalkingCellCentre(tank.footprint, cells.back()));
        resolved->push_back(std::move(item));
    }

    for (std::size_t left = 0; left < route_cell_sets.size(); ++left) {
        for (std::size_t right = left + 1U; right < route_cell_sets.size(); ++right) {
            std::vector<CellKey> shared;
            std::set_intersection(route_cell_sets[left].begin(), route_cell_sets[left].end(),
                route_cell_sets[right].begin(), route_cell_sets[right].end(),
                std::back_inserter(shared));
            const std::string path = "/tank/tunnels/" + std::to_string(right) +
                "/centrelineCells";
            if (shared.size() > 1U) {
                error(diagnostics, "tunnel_overlap", path,
                    "Connected tunnel routes may share one junction cell, not an overlapping segment");
                continue;
            }
            if (shared.size() == 1U) {
                const GridCell junction{shared.front().first, shared.front().second};
                if (!interiorNode(tank.footprint, junction)) {
                    error(diagnostics, "tunnel_shared_portal", path,
                        "Tunnel routes must connect at an interior junction, not reuse a portal");
                }
            }
            bool separated = true;
            for (const auto& [left_column, left_row] : route_cell_sets[left]) {
                for (const auto& [right_column, right_row] : route_cell_sets[right]) {
                    if (std::max(std::abs(left_column - right_column),
                            std::abs(left_row - right_row)) <= 2) {
                        if (shared.size() == 1U) {
                            const auto [junction_column, junction_row] = shared.front();
                            const bool inside_junction =
                                std::max(std::abs(left_column - junction_column),
                                    std::abs(left_row - junction_row)) <= 2 &&
                                std::max(std::abs(right_column - junction_column),
                                    std::abs(right_row - junction_row)) <= 2;
                            if (inside_junction) continue;
                        }
                        separated = false;
                        break;
                    }
                }
                if (!separated) break;
            }
            if (!separated) {
                error(diagnostics, "tunnel_separation_too_small", path,
                    "Separate tunnels need at least one clear walking-grid tile between their glass edges");
            }
        }
    }
    return diagnostics;
}

} // namespace detail
} // namespace pr::aquarium::geometry
