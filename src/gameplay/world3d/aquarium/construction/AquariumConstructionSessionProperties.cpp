#include "gameplay/world3d/aquarium/construction/AquariumConstructionSession.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace pr::gameplay::world3d::aquarium::construction {
namespace geo = pr::aquarium::geometry;

bool AquariumConstructionSession::ensurePropertyDraft() {
    if (state_ == ConstructionState::DraftReview && draft_) return true;
    const geo::TankDesign* tank = selectedTank();
    if (state_ != ConstructionState::Selected || !tank) return false;
    const geo::GridCell centre = tankCentreCell(*tank);
    draft_ = ConstructionDraft{
        ConstructionDraftOperation::Properties, centre, centre, *tank, *tank,
        AquariumResizeHandle::SouthEast};
    state_ = ConstructionState::DraftReview;
    return true;
}

bool AquariumConstructionSession::beginPropertySelected() {
    return ensurePropertyDraft();
}

std::optional<geo::GridCell> AquariumConstructionSession::nearestCornerVertex() const {
    const geo::TankDesign* tank = selectedTank();
    if (!tank) return std::nullopt;
    const auto corners = geo::footprintCorners(tank->footprint);
    std::optional<geo::GridCell> nearest;
    int nearest_distance = std::numeric_limits<int>::max();
    for (const auto& corner : corners) {
        if (!corner.convex) continue;
        const int world_column = tank->footprint.origin_cell.column + corner.vertex.column;
        const int world_row = tank->footprint.origin_cell.row + corner.vertex.row;
        const int dx = cursor_.column - world_column;
        const int dy = cursor_.row - world_row;
        const int distance = dx * dx + dy * dy;
        if (distance < nearest_distance) {
            nearest_distance = distance;
            nearest = corner.vertex;
        }
    }
    return nearest;
}

bool AquariumConstructionSession::adjustCornerRadius(
    geo::GridCell corner_vertex, int direction) {
    if (direction == 0 || !ensurePropertyDraft() || !draft_) return false;
    geo::TankDesign tank = draftTank();
    const auto corner = std::find_if(tank.corner_radii.begin(), tank.corner_radii.end(),
        [&](const geo::CornerRadiusDesign& item) {
            return item.vertex.column == corner_vertex.column &&
                item.vertex.row == corner_vertex.row;
        });
    const int current = corner == tank.corner_radii.end()
        ? tank.corner_radius_steps : corner->radius_steps;
    const int maximum = geo::fittedCornerRadiusStepsAt(
        tank.footprint, corner_vertex, 64);
    if (maximum <= 0) return false;
    const int next = std::clamp(current + (direction > 0 ? 1 : -1), 0, maximum);
    if (next == current) return false;
    if (corner == tank.corner_radii.end()) {
        tank.corner_radii.push_back({corner_vertex, next});
    } else {
        corner->radius_steps = next;
    }
    std::sort(tank.corner_radii.begin(), tank.corner_radii.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.vertex.row < rhs.vertex.row ||
            (lhs.vertex.row == rhs.vertex.row && lhs.vertex.column < rhs.vertex.column);
    });
    draft_->candidate_tank = std::move(tank);
    refreshDraftValidation();
    return true;
}

bool AquariumConstructionSession::adjustTankProperty(
    AquariumTankProperty property, int direction) {
    if (direction == 0) return false;
    const geo::TankDesign* selected = selectedTank();
    if (!draft_ && !selected) return false;
    const geo::TankDesign current = draft_ ? draftTank() : *selected;
    if ((property == AquariumTankProperty::NotchWidth ||
         property == AquariumTankProperty::NotchDepth) &&
        current.footprint.shape == geo::FootprintShape::Rectangle) {
        return false;
    }
    if (!ensurePropertyDraft() || !draft_) return false;
    geo::TankDesign tank = draftTank();
    const int delta = direction > 0 ? 1 : -1;
    switch (property) {
    case AquariumTankProperty::Shape: {
        int shape = static_cast<int>(tank.footprint.shape);
        tank.footprint.shape = static_cast<geo::FootprintShape>((shape + delta + 3) % 3);
        if (tank.footprint.shape == geo::FootprintShape::Rectangle) {
            tank.footprint.notch_width_cells = 0;
            tank.footprint.notch_depth_cells = 0;
        } else {
            tank.footprint.width_cells = std::max(5, tank.footprint.width_cells);
            tank.footprint.depth_cells = std::max(5, tank.footprint.depth_cells);
            tank.footprint.notch_depth_cells = std::max(1, tank.footprint.depth_cells - 2);
            tank.footprint.notch_width_cells = tank.footprint.shape == geo::FootprintShape::L
                ? std::max(1, tank.footprint.width_cells - 2)
                : std::max(1, tank.footprint.width_cells - 4);
        }
        break;
    }
    case AquariumTankProperty::Height:
        tank.height_steps = std::clamp(
            tank.height_steps + delta,
            geo::kMinimumTankHeightSteps,
            geo::kMaximumTankHeightSteps);
        break;
    case AquariumTankProperty::Depth:
        tank.depth_steps = std::clamp(
            tank.depth_steps + delta,
            geo::kMinimumTankDepthSteps,
            geo::kMaximumTankDepthSteps);
        break;
    case AquariumTankProperty::Roundness: {
        const int maximum = geo::fittedCornerRadiusSteps(tank.footprint, 64);
        tank.corner_radius_steps = std::clamp(tank.corner_radius_steps + delta, 0, maximum);
        break;
    }
    case AquariumTankProperty::Rotation:
        tank.footprint.rotation_quarter_turns =
            (tank.footprint.rotation_quarter_turns + delta + 4) % 4;
        break;
    case AquariumTankProperty::NotchWidth:
        tank.footprint.notch_width_cells = std::clamp(
            tank.footprint.notch_width_cells + delta, 1,
            std::max(1, tank.footprint.width_cells -
                (tank.footprint.shape == geo::FootprintShape::L ? 2 : 4)));
        break;
    case AquariumTankProperty::NotchDepth:
        tank.footprint.notch_depth_cells = std::clamp(
            tank.footprint.notch_depth_cells + delta, 1,
            std::max(1, tank.footprint.depth_cells - 2));
        break;
    }
    draft_->candidate_tank = std::move(tank);
    refreshDraftValidation();
    return true;
}

std::optional<geo::TankDesign> AquariumConstructionSession::previewTank() const {
    if (draft_ && !draft_->paint_original_tanks.empty()) {
        if (!draft_->original_tank) return std::nullopt;
        const auto found = std::find_if(
            draft_->paint_candidate_tanks.begin(), draft_->paint_candidate_tanks.end(),
            [&](const auto& tank) { return tank.id == draft_->original_tank->id; });
        return found == draft_->paint_candidate_tanks.end()
            ? std::optional<geo::TankDesign>{}
            : std::optional<geo::TankDesign>{*found};
    }
    if (draft_ && draft_->operation == ConstructionDraftOperation::Create) {
        return resolveDrawnTank(committed_.tanks, draftTank()).preview_tank;
    }
    return draft_ && !draft_->delete_candidate
        ? std::optional<geo::TankDesign>(draftTank()) : std::nullopt;
}

std::optional<ConstructionDraftOperation> AquariumConstructionSession::draftOperation() const {
    return draft_ ? std::optional<ConstructionDraftOperation>(draft_->operation) : std::nullopt;
}

} // namespace pr::gameplay::world3d::aquarium::construction
