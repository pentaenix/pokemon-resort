#include "gameplay/world3d/aquarium/construction/AquariumConstructionSession.hpp"

#include <algorithm>
#include <utility>

namespace pr::gameplay::world3d::aquarium::construction {
namespace geo = pr::aquarium::geometry;

namespace {

bool sameCell(geo::GridCell lhs, geo::GridCell rhs) {
    return lhs.column == rhs.column && lhs.row == rhs.row;
}

} // namespace

bool AquariumConstructionSession::beginSubtractSelected() {
    const geo::TankDesign* selected = selectedTank();
    if (state_ != ConstructionState::Selected || !selected) return false;
    geo::TankDesign editable = *selected;
    if (editable.footprint.shape != geo::FootprintShape::Rectangle ||
        editable.footprint.rotation_quarter_turns != 0) {
        const auto occupied = geo::footprintCells(editable.footprint);
        const int width = geo::occupiedWidthCells(editable.footprint);
        const int depth = geo::occupiedDepthCells(editable.footprint);
        editable.footprint.shape = geo::FootprintShape::Rectangle;
        editable.footprint.width_cells = width;
        editable.footprint.depth_cells = depth;
        editable.footprint.rotation_quarter_turns = 0;
        editable.footprint.notch_width_cells = 0;
        editable.footprint.notch_depth_cells = 0;
        editable.footprint.subtracted_cells.clear();
        for (int row = 0; row < depth; ++row) {
            for (int column = 0; column < width; ++column) {
                const geo::GridCell world{
                    editable.footprint.origin_cell.column + column,
                    editable.footprint.origin_cell.row + row};
                const bool present = std::any_of(occupied.begin(), occupied.end(),
                    [&](geo::GridCell cell) { return sameCell(cell, world); });
                if (!present) editable.footprint.subtracted_cells.push_back({column, row});
            }
        }
    }
    const geo::GridCell centre = tankCentreCell(editable);
    cursor_ = centre;
    draft_ = ConstructionDraft{
        ConstructionDraftOperation::Subtract, centre, centre, *selected, editable,
        AquariumResizeHandle::SouthEast};
    state_ = ConstructionState::SubtractFootprint;
    refreshDraftValidation();
    return true;
}

bool AquariumConstructionSession::toggleSubtractedCell() {
    if (state_ != ConstructionState::SubtractFootprint || !draft_ ||
        !draft_->candidate_tank) return false;
    geo::TankDesign tank = *draft_->candidate_tank;
    const int local_column = cursor_.column - tank.footprint.origin_cell.column;
    const int local_row = cursor_.row - tank.footprint.origin_cell.row;
    if (local_column < 0 || local_row < 0 ||
        local_column >= tank.footprint.width_cells ||
        local_row >= tank.footprint.depth_cells) return false;
    auto& cuts = tank.footprint.subtracted_cells;
    const auto found = std::find_if(cuts.begin(), cuts.end(), [&](geo::GridCell cell) {
        return cell.column == local_column && cell.row == local_row;
    });
    if (found == cuts.end()) cuts.push_back({local_column, local_row});
    else cuts.erase(found);
    std::sort(cuts.begin(), cuts.end(), [](geo::GridCell lhs, geo::GridCell rhs) {
        return lhs.row < rhs.row || (lhs.row == rhs.row && lhs.column < rhs.column);
    });
    draft_->candidate_tank = std::move(tank);
    refreshDraftValidation();
    return true;
}

} // namespace pr::gameplay::world3d::aquarium::construction
