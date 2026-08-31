#include "gameplay/world3d/aquarium/construction/AquariumConstructionSession.hpp"

#include <algorithm>
#include <set>

namespace pr::gameplay::world3d::aquarium::construction {
namespace geo = pr::aquarium::geometry;
namespace {

using CellKey = std::pair<int, int>;

std::set<CellKey> occupiedCells(const geo::TankDesign& tank) {
    std::set<CellKey> result;
    for (const geo::GridCell cell : geo::footprintCells(tank.footprint)) {
        result.emplace(cell.column, cell.row);
    }
    return result;
}

bool adjacentTo(const std::set<CellKey>& cells, geo::GridCell cell) {
    constexpr CellKey neighbours[]{{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    return std::any_of(std::begin(neighbours), std::end(neighbours), [&](CellKey delta) {
        return cells.count({cell.column + delta.first, cell.row + delta.second}) != 0;
    });
}

geo::TankDesign tankFromCells(
    const geo::TankDesign& source,
    const std::set<CellKey>& cells) {
    geo::TankDesign result = source;
    int min_column = cells.begin()->first;
    int min_row = cells.begin()->second;
    int max_column = min_column;
    int max_row = min_row;
    for (const auto [column, row] : cells) {
        min_column = std::min(min_column, column);
        min_row = std::min(min_row, row);
        max_column = std::max(max_column, column);
        max_row = std::max(max_row, row);
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
            if (!cells.count({column, row})) {
                result.footprint.subtracted_cells.push_back({column - min_column, row - min_row});
            }
        }
    }
    return result;
}

} // namespace

bool AquariumConstructionSession::beginPaintSelected(bool subtract) {
    const geo::TankDesign* selected = selectedTank();
    if (state_ != ConstructionState::Selected || !selected) return false;
    const auto initial_cells = occupiedCells(*selected);
    const bool initially_occupied = initial_cells.count({cursor_.column, cursor_.row}) != 0;
    if ((subtract && !initially_occupied) || (!subtract && initially_occupied)) return false;
    const geo::GridCell anchor = cursor_;
    draft_ = ConstructionDraft{
        subtract ? ConstructionDraftOperation::Subtract : ConstructionDraftOperation::Add,
        anchor, anchor, *selected, *selected, AquariumResizeHandle::SouthEast, false};
    state_ = ConstructionState::PaintFootprint;
    if (paintCursorCell()) return true;
    draft_.reset();
    state_ = ConstructionState::Selected;
    return false;
}

bool AquariumConstructionSession::paintCursorCell() {
    if (state_ != ConstructionState::PaintFootprint || !draft_ ||
        !draft_->original_tank) return false;
    const geo::TankDesign source = draft_->candidate_tank
        ? *draft_->candidate_tank : *draft_->original_tank;
    auto cells = occupiedCells(source);
    const CellKey cursor_key{cursor_.column, cursor_.row};
    if (draft_->operation == ConstructionDraftOperation::Add) {
        if (cells.count(cursor_key)) return true;
        if (!cellAllowed(cursor_) || occupiedByCommitted(cursor_, source.id) ||
            !adjacentTo(cells, cursor_)) return false;
        cells.insert(cursor_key);
    } else {
        if (!cells.erase(cursor_key)) return true;
    }
    draft_->cursor = cursor_;
    draft_->delete_candidate = cells.empty();
    if (!cells.empty()) draft_->candidate_tank = tankFromCells(*draft_->original_tank, cells);
    else draft_->candidate_tank.reset();
    refreshDraftValidation();
    return true;
}

} // namespace pr::gameplay::world3d::aquarium::construction
