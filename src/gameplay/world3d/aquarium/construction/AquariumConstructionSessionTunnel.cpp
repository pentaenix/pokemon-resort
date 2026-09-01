#include "gameplay/world3d/aquarium/construction/AquariumConstructionSession.hpp"

#include <algorithm>
#include <set>

namespace pr::gameplay::world3d::aquarium::construction {
namespace geo = pr::aquarium::geometry;
namespace {

bool same(geo::GridCell lhs, geo::GridCell rhs) {
    return lhs.column == rhs.column && lhs.row == rhs.row;
}

bool occupied(const geo::TankDesign& tank, geo::GridCell cell) {
    const auto cells = tankFootprintCells(tank);
    return std::any_of(cells.begin(), cells.end(),
        [&](geo::GridCell item) { return same(item, cell); });
}

int turnCount(const std::vector<geo::CellPoint>& route) {
    int turns = 0;
    for (std::size_t index = 2; index < route.size(); ++index) {
        const int before_column = route[index - 1U].column - route[index - 2U].column;
        const int before_row = route[index - 1U].row - route[index - 2U].row;
        const int after_column = route[index].column - route[index - 1U].column;
        const int after_row = route[index].row - route[index - 1U].row;
        if (before_column != after_column || before_row != after_row) ++turns;
    }
    return turns;
}

} // namespace

std::vector<geo::GridCell> AquariumConstructionSession::tunnelPortalCells() const {
    const geo::TankDesign* tank = selectedTank();
    if (!tank) return {};
    if (tank->footprint.shape != geo::FootprintShape::Rectangle ||
        !tank->footprint.subtracted_cells.empty() ||
        tank->footprint.rotation_quarter_turns != 0 ||
        tank->corner_radius_steps != 0 ||
        std::any_of(tank->corner_radii.begin(), tank->corner_radii.end(),
            [](const auto& radius) { return radius.radius_steps != 0; })) return {};
    const auto cells = tankFootprintCells(*tank);
    std::vector<geo::GridCell> portals;
    constexpr geo::GridCell neighbours[]{{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    for (const geo::GridCell cell : cells) {
        const bool boundary = std::any_of(std::begin(neighbours), std::end(neighbours),
            [&](geo::GridCell direction) {
                const geo::GridCell exterior{
                    cell.column + direction.column, cell.row + direction.row};
                return !occupied(*tank, exterior) && cellAllowed(exterior) &&
                    !occupiedByCommitted(exterior, tank->id);
            });
        if (boundary) portals.push_back(cell);
    }
    return portals;
}

bool AquariumConstructionSession::isTunnelPortalCell(geo::GridCell cell) const {
    const auto portals = tunnelPortalCells();
    return std::any_of(portals.begin(), portals.end(),
        [&](geo::GridCell portal) { return same(cell, portal); });
}

std::vector<geo::GridCell> AquariumConstructionSession::tunnelRouteCells() const {
    std::vector<geo::GridCell> cells;
    if (!draft_ || draft_->operation != ConstructionDraftOperation::Tunnel) return cells;
    cells.reserve(draft_->tunnel_route.size());
    for (const auto point : draft_->tunnel_route) {
        cells.push_back({point.column, point.row});
    }
    return cells;
}

bool AquariumConstructionSession::beginTunnelSelected() {
    const geo::TankDesign* tank = selectedTank();
    if (state_ != ConstructionState::Selected || !tank || !isTunnelPortalCell(cursor_)) {
        validation_message_ = "Choose a glowing tank-edge portal";
        return false;
    }
    if (tank->footprint.shape != geo::FootprintShape::Rectangle ||
        !tank->footprint.subtracted_cells.empty() ||
        tank->footprint.rotation_quarter_turns != 0 ||
        tank->corner_radius_steps != 0 ||
        std::any_of(tank->corner_radii.begin(), tank->corner_radii.end(),
            [](const auto& radius) { return radius.radius_steps != 0; })) {
        validation_message_ = "Tunnels currently need a square-corner rectangle";
        return false;
    }
    draft_ = ConstructionDraft{};
    draft_->operation = ConstructionDraftOperation::Tunnel;
    draft_->anchor = cursor_;
    draft_->cursor = cursor_;
    draft_->original_tank = *tank;
    draft_->candidate_tank = *tank;
    draft_->tunnel_route.push_back({cursor_.column, cursor_.row});
    state_ = ConstructionState::TunnelRoute;
    validation_message_ = "Move through the tank to another glowing portal";
    return true;
}

void AquariumConstructionSession::syncTunnelCandidate() {
    if (!draft_ || !draft_->original_tank ||
        draft_->operation != ConstructionDraftOperation::Tunnel) return;
    geo::TankDesign candidate = *draft_->original_tank;
    geo::TunnelDesign tunnel;
    const std::string base_id = candidate.id + "_tunnel_" +
        std::to_string(committed_.revision + 1U);
    tunnel.id = base_id;
    int suffix = 2;
    while (std::any_of(candidate.tunnels.begin(), candidate.tunnels.end(),
            [&](const auto& existing) { return existing.id == tunnel.id; })) {
        tunnel.id = base_id + "_" + std::to_string(suffix++);
    }
    tunnel.route = turnCount(draft_->tunnel_route) == 0
        ? geo::TunnelRoute::Straight : geo::TunnelRoute::OneElbow;
    tunnel.centreline_cells = draft_->tunnel_route;
    candidate.tunnels.push_back(std::move(tunnel));
    draft_->candidate_tank = std::move(candidate);
}

bool AquariumConstructionSession::extendTunnelRouteTo(geo::GridCell target) {
    if (state_ != ConstructionState::TunnelRoute || !draft_ ||
        !draft_->original_tank || draft_->tunnel_route.empty()) return false;
    const geo::TankDesign& tank = *draft_->original_tank;
    auto step = [&](geo::GridCell next) {
        auto& route = draft_->tunnel_route;
        if (!occupied(tank, next)) return false;
        if (route.size() >= 2U &&
            route[route.size() - 2U].column == next.column &&
            route[route.size() - 2U].row == next.row) {
            route.pop_back();
            return true;
        }
        if (std::any_of(route.begin(), route.end(), [&](const auto point) {
                return point.column == next.column && point.row == next.row;
            })) return false;
        const geo::GridCell current{route.back().column, route.back().row};
        if (std::abs(next.column - current.column) +
            std::abs(next.row - current.row) != 1) return false;
        route.push_back({next.column, next.row});
        if (turnCount(route) > 1) {
            route.pop_back();
            return false;
        }
        for (const auto& existing : tank.tunnels) {
            if (std::any_of(existing.centreline_cells.begin(),
                    existing.centreline_cells.end(), [&](const auto point) {
                        return point.column == next.column && point.row == next.row;
                    })) {
                route.pop_back();
                return false;
            }
        }
        return true;
    };
    geo::GridCell current{draft_->tunnel_route.back().column,
        draft_->tunnel_route.back().row};
    bool changed = false;
    while (current.column != target.column) {
        current.column += current.column < target.column ? 1 : -1;
        if (!step(current)) break;
        changed = true;
    }
    while (current.row != target.row) {
        current.row += current.row < target.row ? 1 : -1;
        if (!step(current)) break;
        changed = true;
    }
    if (!changed) return false;
    cursor_ = {draft_->tunnel_route.back().column, draft_->tunnel_route.back().row};
    draft_->cursor = cursor_;
    syncTunnelCandidate();
    refreshDraftValidation();
    return true;
}

} // namespace pr::gameplay::world3d::aquarium::construction
