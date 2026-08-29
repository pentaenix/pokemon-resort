#include "gameplay/world3d/aquarium/construction/AquariumConstructionSession.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace pr::gameplay::world3d::aquarium::construction {
namespace geo = pr::aquarium::geometry;

namespace {

bool sameCell(geo::GridCell lhs, geo::GridCell rhs) {
    return lhs.column == rhs.column && lhs.row == rhs.row;
}

} // namespace

std::vector<std::string> validateAquariumPlacement(
    const AquariumDesignDocument& document,
    const AquariumConstructionConfig& config,
    const std::vector<geo::GridCell>& authored_obstacles) {
    std::vector<std::string> diagnostics;
    if (document.tanks.size() > 8U) diagnostics.push_back("too_many_player_tanks");
    const auto allowed = [&](geo::GridCell cell) {
        return std::any_of(config.allowed_cells.begin(), config.allowed_cells.end(), [&](auto item) {
            return item.column == cell.column && item.row == cell.row;
        });
    };
    const auto authored = [&](geo::GridCell cell) {
        return std::any_of(authored_obstacles.begin(), authored_obstacles.end(), [&](auto item) {
            return item.column == cell.column && item.row == cell.row;
        });
    };
    std::set<std::pair<int, int>> occupied;
    for (const geo::TankDesign& tank : document.tanks) {
        geo::AquariumBuildRequest request;
        request.tank = tank;
        if (!geo::validateAquarium(request).valid()) {
            diagnostics.push_back("invalid_kernel_design:" + tank.id);
            continue;
        }
        for (int row = tank.footprint.origin_cell.row;
             row < tank.footprint.origin_cell.row + tank.footprint.depth_cells; ++row) {
            for (int column = tank.footprint.origin_cell.column;
                 column < tank.footprint.origin_cell.column + tank.footprint.width_cells; ++column) {
                const geo::GridCell cell{column, row};
                if (!allowed(cell)) diagnostics.push_back("tank_outside_build_zone:" + tank.id);
                if (authored(cell)) diagnostics.push_back("tank_overlaps_authored_obstacle:" + tank.id);
                if (!occupied.emplace(column, row).second) {
                    diagnostics.push_back("player_tanks_overlap:" + tank.id);
                }
            }
        }
    }
    std::sort(diagnostics.begin(), diagnostics.end());
    diagnostics.erase(std::unique(diagnostics.begin(), diagnostics.end()), diagnostics.end());
    return diagnostics;
}

void AquariumConstructionSession::configure(
    std::string map_id,
    const AquariumConstructionConfig& config,
    std::vector<geo::GridCell> authored_obstacles,
    AquariumDesignDocument committed) {
    exit();
    map_id_ = std::move(map_id);
    available_ = config.enabled && !config.allowed_cells.empty();
    allowed_cells_.clear();
    allowed_cells_.reserve(config.allowed_cells.size());
    for (const auto cell : config.allowed_cells) {
        allowed_cells_.push_back({cell.column, cell.row});
    }
    authored_obstacles_ = std::move(authored_obstacles);
    committed_ = std::move(committed);
    if (!validateAquariumPlacement(committed_, config, authored_obstacles_).empty()) {
        available_ = false;
    }
}

bool AquariumConstructionSession::enter(geo::GridCell preferred_cursor) {
    if (!available_ || active()) return false;
    state_ = ConstructionState::Browse;
    protected_player_cell_ = preferred_cursor;
    if (cellAllowed(preferred_cursor)) {
        cursor_ = preferred_cursor;
    } else if (!allowed_cells_.empty()) {
        cursor_ = *std::min_element(allowed_cells_.begin(), allowed_cells_.end(), [&](auto lhs, auto rhs) {
            const int lhs_distance = std::abs(lhs.column - preferred_cursor.column) +
                                     std::abs(lhs.row - preferred_cursor.row);
            const int rhs_distance = std::abs(rhs.column - preferred_cursor.column) +
                                     std::abs(rhs.row - preferred_cursor.row);
            return lhs_distance < rhs_distance;
        });
    }
    validation_message_.clear();
    return true;
}

void AquariumConstructionSession::exit() {
    state_ = ConstructionState::Dormant;
    draft_.reset();
    protected_player_cell_.reset();
    validation_message_.clear();
}

void AquariumConstructionSession::moveCursor(int column_delta, int row_delta) {
    pointAt({cursor_.column + column_delta, cursor_.row + row_delta});
}

void AquariumConstructionSession::pointAt(geo::GridCell cell) {
    if (!active() || !cellAllowed(cell)) return;
    cursor_ = cell;
    if (draft_ && state_ == ConstructionState::ResizeFootprint) {
        draft_->cursor = cell;
        refreshDraftValidation();
    }
}

bool AquariumConstructionSession::beginRectangle() {
    if (state_ != ConstructionState::Browse || !cellAllowed(cursor_)) return false;
    draft_ = ConstructionDraft{cursor_, cursor_};
    state_ = ConstructionState::ResizeFootprint;
    refreshDraftValidation();
    return true;
}

bool AquariumConstructionSession::reviewDraft() {
    if (state_ != ConstructionState::ResizeFootprint || !draft_) return false;
    refreshDraftValidation();
    state_ = ConstructionState::DraftReview;
    return true;
}

bool AquariumConstructionSession::adjustDraft() {
    if (state_ != ConstructionState::DraftReview || !draft_) return false;
    state_ = ConstructionState::ResizeFootprint;
    return true;
}

bool AquariumConstructionSession::cancel() {
    if (state_ == ConstructionState::ResizeFootprint ||
        state_ == ConstructionState::DraftReview ||
        state_ == ConstructionState::Building) {
        draft_.reset();
        state_ = ConstructionState::Browse;
        validation_message_.clear();
        return true;
    }
    return false;
}

geo::TankDesign AquariumConstructionSession::draftTank() const {
    geo::TankDesign tank;
    if (!draft_) return tank;
    const int min_column = std::min(draft_->anchor.column, draft_->cursor.column);
    const int min_row = std::min(draft_->anchor.row, draft_->cursor.row);
    const std::string base_id = "tank_" + std::to_string(committed_.revision + 1);
    tank.id = base_id;
    int suffix = 2;
    while (std::any_of(committed_.tanks.begin(), committed_.tanks.end(), [&](const auto& existing) {
        return existing.id == tank.id;
    })) {
        tank.id = base_id + "_" + std::to_string(suffix++);
    }
    tank.footprint.origin_cell = {min_column, min_row};
    tank.footprint.width_cells = std::abs(draft_->cursor.column - draft_->anchor.column) + 1;
    tank.footprint.depth_cells = std::abs(draft_->cursor.row - draft_->anchor.row) + 1;
    tank.height_steps = 8;
    tank.corner_radius_steps = 0;
    return tank;
}

std::vector<geo::GridCell> AquariumConstructionSession::draftCells() const {
    std::vector<geo::GridCell> cells;
    if (!draft_) return cells;
    const geo::TankDesign tank = draftTank();
    for (int row = tank.footprint.origin_cell.row;
         row < tank.footprint.origin_cell.row + tank.footprint.depth_cells; ++row) {
        for (int column = tank.footprint.origin_cell.column;
             column < tank.footprint.origin_cell.column + tank.footprint.width_cells; ++column) {
            cells.push_back({column, row});
        }
    }
    return cells;
}

bool AquariumConstructionSession::cellAllowed(geo::GridCell cell) const {
    return std::any_of(allowed_cells_.begin(), allowed_cells_.end(),
        [&](auto allowed) { return sameCell(cell, allowed); });
}

bool AquariumConstructionSession::occupiedByCommitted(geo::GridCell cell) const {
    if (protected_player_cell_ && sameCell(cell, *protected_player_cell_)) return true;
    if (std::any_of(authored_obstacles_.begin(), authored_obstacles_.end(),
            [&](auto occupied) { return sameCell(cell, occupied); })) return true;
    for (const geo::TankDesign& tank : committed_.tanks) {
        const int left = tank.footprint.origin_cell.column;
        const int top = tank.footprint.origin_cell.row;
        if (cell.column >= left && cell.column < left + tank.footprint.width_cells &&
            cell.row >= top && cell.row < top + tank.footprint.depth_cells) return true;
    }
    return false;
}

bool AquariumConstructionSession::cellBlocked(geo::GridCell cell) const {
    return occupiedByCommitted(cell);
}

void AquariumConstructionSession::refreshDraftValidation() {
    validation_message_.clear();
    if (!draft_) return;
    const geo::TankDesign tank = draftTank();
    if (tank.footprint.width_cells < 3 || tank.footprint.depth_cells < 3) {
        validation_message_ = "Expand the tank to at least three cells in both directions";
        return;
    }
    for (const geo::GridCell cell : draftCells()) {
        if (!cellAllowed(cell)) {
            validation_message_ = "Tank footprint leaves the construction area";
            return;
        }
        if (occupiedByCommitted(cell)) {
            validation_message_ = "Tank footprint overlaps an existing obstacle";
            return;
        }
    }
    geo::AquariumBuildRequest request;
    request.tank = tank;
    const geo::ValidationReport validation = geo::validateAquarium(request);
    if (!validation.valid() && !validation.diagnostics.empty()) {
        validation_message_ = validation.diagnostics.front().message;
    }
}

bool AquariumConstructionSession::draftValid() const {
    return draft_.has_value() && validation_message_.empty();
}

std::optional<ConstructionCommitCandidate> AquariumConstructionSession::prepareCommit() {
    if (state_ != ConstructionState::DraftReview) return std::nullopt;
    refreshDraftValidation();
    if (!draftValid() || committed_.tanks.size() >= 8U) {
        if (committed_.tanks.size() >= 8U) validation_message_ = "This room supports eight player tanks";
        return std::nullopt;
    }
    AquariumDesignDocument candidate = committed_;
    candidate.revision += 1;
    candidate.tanks.push_back(draftTank());
    state_ = ConstructionState::Building;
    return ConstructionCommitCandidate{std::move(candidate)};
}

void AquariumConstructionSession::publish(ConstructionCommitCandidate candidate) {
    committed_ = std::move(candidate.document);
    draft_.reset();
    state_ = ConstructionState::Browse;
    validation_message_.clear();
}

void AquariumConstructionSession::rejectCommit(std::string message) {
    state_ = draft_ ? ConstructionState::DraftReview : ConstructionState::Browse;
    validation_message_ = std::move(message);
}

} // namespace pr::gameplay::world3d::aquarium::construction
