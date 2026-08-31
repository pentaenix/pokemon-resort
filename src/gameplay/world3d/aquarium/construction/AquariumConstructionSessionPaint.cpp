#include "gameplay/world3d/aquarium/construction/AquariumConstructionSession.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
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

auto findTankById(std::vector<geo::TankDesign>& tanks, const std::string& id) {
    return std::find_if(tanks.begin(), tanks.end(),
        [&](const auto& tank) { return tank.id == id; });
}

auto findTankAt(std::vector<geo::TankDesign>& tanks, geo::GridCell cell) {
    return std::find_if(tanks.begin(), tanks.end(), [&](const auto& tank) {
        const auto cells = occupiedCells(tank);
        return cells.count({cell.column, cell.row}) != 0;
    });
}

void markAffected(ConstructionDraft& draft, const std::string& id) {
    if (std::find(draft.paint_affected_ids.begin(), draft.paint_affected_ids.end(), id) ==
        draft.paint_affected_ids.end()) {
        draft.paint_affected_ids.push_back(id);
    }
}

} // namespace

bool AquariumConstructionSession::beginPaintSelected(bool subtract) {
    if (subtract && state_ == ConstructionState::Browse && !committed_.tanks.empty()) {
        const auto nearest = std::min_element(
            committed_.tanks.begin(), committed_.tanks.end(), [&](const auto& lhs, const auto& rhs) {
                const auto distance = [&](const auto& tank) {
                    int best = std::numeric_limits<int>::max();
                    for (const auto cell : geo::footprintCells(tank.footprint)) {
                        best = std::min(best, std::abs(cell.column - cursor_.column) +
                            std::abs(cell.row - cursor_.row));
                    }
                    return best;
                };
                return distance(lhs) < distance(rhs);
            });
        selected_tank_id_ = nearest->id;
        state_ = ConstructionState::Selected;
    }
    const geo::TankDesign* selected = selectedTank();
    if (state_ != ConstructionState::Selected || !selected || !cellAllowed(cursor_)) return false;
    const geo::GridCell anchor = cursor_;
    draft_ = ConstructionDraft{
        subtract ? ConstructionDraftOperation::Subtract : ConstructionDraftOperation::Add,
        anchor, anchor, *selected, *selected, AquariumResizeHandle::SouthEast, false};
    draft_->paint_original_tanks = committed_.tanks;
    draft_->paint_candidate_tanks = committed_.tanks;
    state_ = ConstructionState::PaintFootprint;

    // The first click may be outside every tank. It arms a click-move-click
    // gesture; painting starts when the path reaches an editable footprint.
    paintCursorCell();
    refreshDraftValidation();
    return true;
}

bool AquariumConstructionSession::paintCursorCell() {
    if (state_ != ConstructionState::PaintFootprint || !draft_ ||
        !draft_->original_tank) return false;

    auto& tanks = draft_->paint_candidate_tanks;
    const CellKey cursor_key{cursor_.column, cursor_.row};
    if (draft_->operation == ConstructionDraftOperation::Add) {
        auto primary = findTankById(tanks, draft_->original_tank->id);
        if (primary == tanks.end()) return false;
        auto primary_cells = occupiedCells(*primary);
        if (primary_cells.count(cursor_key)) {
            draft_->cursor = cursor_;
            return true;
        }

        auto touched = findTankAt(tanks, cursor_);
        if (touched != tanks.end() && touched->id != primary->id) {
            if (!adjacentTo(primary_cells, cursor_)) {
                draft_->cursor = cursor_;
                return true;
            }
            const std::string absorbed_id = touched->id;
            const auto absorbed_cells = occupiedCells(*touched);
            primary_cells.insert(absorbed_cells.begin(), absorbed_cells.end());
            markAffected(*draft_, absorbed_id);
            const std::string primary_id = primary->id;
            *primary = tankFromCells(*primary, primary_cells);
            tanks.erase(findTankById(tanks, absorbed_id));
            primary = findTankById(tanks, primary_id);
        } else {
            // Moving through empty exterior cells is allowed. Once the path
            // reaches an edge, every following cell grows the footprint.
            if (!adjacentTo(primary_cells, cursor_)) {
                draft_->cursor = cursor_;
                return true;
            }
            if (!cellAllowed(cursor_) || occupiedByCommitted(cursor_, primary->id)) return false;
            primary_cells.insert(cursor_key);
            *primary = tankFromCells(*primary, primary_cells);
        }
        markAffected(*draft_, draft_->original_tank->id);
        draft_->candidate_tank = *findTankById(tanks, draft_->original_tank->id);
        draft_->paint_changed = true;
    } else {
        auto touched = findTankAt(tanks, cursor_);
        if (touched == tanks.end()) {
            draft_->cursor = cursor_;
            return true;
        }
        const std::string touched_id = touched->id;
        auto cells = occupiedCells(*touched);
        cells.erase(cursor_key);
        markAffected(*draft_, touched_id);
        if (cells.empty()) tanks.erase(touched);
        else *touched = tankFromCells(*touched, cells);
        const auto primary = findTankById(tanks, draft_->original_tank->id);
        draft_->candidate_tank = primary == tanks.end()
            ? std::optional<geo::TankDesign>{}
            : std::optional<geo::TankDesign>{*primary};
        draft_->paint_changed = true;
    }
    draft_->cursor = cursor_;
    draft_->delete_candidate = tanks.empty();
    refreshDraftValidation();
    return true;
}

} // namespace pr::gameplay::world3d::aquarium::construction
