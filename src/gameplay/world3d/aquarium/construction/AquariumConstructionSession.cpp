#include "gameplay/world3d/aquarium/construction/AquariumConstructionSession.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

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
        for (const geo::GridCell cell : tankFootprintCells(tank)) {
            if (!allowed(cell)) diagnostics.push_back("tank_outside_build_zone:" + tank.id);
            if (authored(cell)) diagnostics.push_back("tank_overlaps_authored_obstacle:" + tank.id);
            if (!occupied.emplace(cell.column, cell.row).second) {
                diagnostics.push_back("player_tanks_overlap:" + tank.id);
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
    for (const auto cell : config.allowed_cells) allowed_cells_.push_back({cell.column, cell.row});
    authored_obstacles_ = std::move(authored_obstacles);
    committed_ = std::move(committed);
    history_.clear();
    selected_tank_id_.reset();
    if (!validateAquariumPlacement(committed_, config, authored_obstacles_).empty()) {
        available_ = false;
    }
}

bool AquariumConstructionSession::enter(geo::GridCell preferred_cursor) {
    if (!available_ || active()) return false;
    state_ = ConstructionState::Browse;
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
    selected_tank_id_.reset();
    pending_operation_token_ = 0;
    validation_message_.clear();
}

const geo::TankDesign* AquariumConstructionSession::selectedTank() const {
    if (!selected_tank_id_) return nullptr;
    const auto index = playerTankIndex(committed_, *selected_tank_id_);
    return index ? &committed_.tanks[*index] : nullptr;
}

AquariumResizeHandle AquariumConstructionSession::preferredResizeHandle() const {
    const geo::TankDesign* tank = selectedTank();
    if (!tank) return AquariumResizeHandle::SouthEast;
    return nearestResizeHandle(*tank, cursor_);
}

void AquariumConstructionSession::moveCursor(int column_delta, int row_delta) {
    pointAt({cursor_.column + column_delta, cursor_.row + row_delta});
}

void AquariumConstructionSession::pointAt(geo::GridCell cell) {
    if (!active() || !cellAllowed(cell)) return;
    if (state_ == ConstructionState::TunnelRoute && draft_) {
        extendTunnelRouteTo(cell);
        return;
    }
    if (state_ == ConstructionState::PaintFootprint && draft_) {
        if (draft_->area_selection) {
            cursor_ = cell;
            draft_->cursor = cell;
            paintCursorCell();
            return;
        }
        while (cursor_.column != cell.column) {
            cursor_.column += cursor_.column < cell.column ? 1 : -1;
            if (!cellAllowed(cursor_) || !paintCursorCell()) return;
        }
        while (cursor_.row != cell.row) {
            cursor_.row += cursor_.row < cell.row ? 1 : -1;
            if (!cellAllowed(cursor_) || !paintCursorCell()) return;
        }
        return;
    }
    cursor_ = cell;
    if (!draft_ || (state_ != ConstructionState::ResizeFootprint &&
        state_ != ConstructionState::MoveTank && state_ != ConstructionState::ResizeTank &&
        state_ != ConstructionState::SubtractFootprint &&
        state_ != ConstructionState::PaintFootprint)) return;
    draft_->cursor = cell;
    if (state_ == ConstructionState::ResizeFootprint && draft_->candidate_tank) {
        const int min_column = std::min(draft_->anchor.column, cell.column);
        const int min_row = std::min(draft_->anchor.row, cell.row);
        draft_->candidate_tank->footprint.origin_cell = {min_column, min_row};
        draft_->candidate_tank->footprint.width_cells =
            std::abs(cell.column - draft_->anchor.column) + 1;
        draft_->candidate_tank->footprint.depth_cells =
            std::abs(cell.row - draft_->anchor.row) + 1;
    } else if (state_ == ConstructionState::MoveTank && draft_->original_tank) {
        draft_->candidate_tank = moveTankByCells(
            *draft_->original_tank,
            cell.column - draft_->anchor.column,
            cell.row - draft_->anchor.row);
    } else if (state_ == ConstructionState::ResizeTank && draft_->original_tank) {
        draft_->candidate_tank = resizeTankToCell(
            *draft_->original_tank, draft_->resize_handle, cell);
    }
    refreshDraftValidation();
}

bool AquariumConstructionSession::beginRectangle() {
    if ((state_ != ConstructionState::Browse && state_ != ConstructionState::Selected) ||
        !cellAllowed(cursor_)) return false;
    selected_tank_id_.reset();
    draft_ = ConstructionDraft{ConstructionDraftOperation::Create, cursor_, cursor_};
    state_ = ConstructionState::ResizeFootprint;
    refreshDraftValidation();
    return true;
}

bool AquariumConstructionSession::selectAtCursor() {
    if (state_ != ConstructionState::Browse && state_ != ConstructionState::Selected) return false;
    const geo::TankDesign* tank = playerTankAtCell(committed_, cursor_);
    if (!tank) {
        clearSelection();
        return false;
    }
    selected_tank_id_ = tank->id;
    state_ = ConstructionState::Selected;
    validation_message_.clear();
    return true;
}

void AquariumConstructionSession::clearSelection() {
    if (state_ == ConstructionState::Building) return;
    selected_tank_id_.reset();
    draft_.reset();
    state_ = active() ? ConstructionState::Browse : ConstructionState::Dormant;
    validation_message_.clear();
}

bool AquariumConstructionSession::beginMoveSelected() {
    const geo::TankDesign* tank = selectedTank();
    if (state_ != ConstructionState::Selected || !tank) return false;
    const geo::GridCell centre = tankCentreCell(*tank);
    cursor_ = centre;
    draft_ = ConstructionDraft{
        ConstructionDraftOperation::Move, centre, centre, *tank, *tank,
        AquariumResizeHandle::SouthEast};
    state_ = ConstructionState::MoveTank;
    refreshDraftValidation();
    return true;
}

bool AquariumConstructionSession::beginResizeSelected(AquariumResizeHandle handle) {
    const geo::TankDesign* tank = selectedTank();
    if (state_ != ConstructionState::Selected || !tank) return false;
    const geo::GridCell handle_cell = resizeHandleCell(*tank, handle);
    cursor_ = handle_cell;
    draft_ = ConstructionDraft{
        ConstructionDraftOperation::Resize, handle_cell, handle_cell, *tank, *tank, handle};
    state_ = ConstructionState::ResizeTank;
    refreshDraftValidation();
    return true;
}

bool AquariumConstructionSession::requestDeleteSelected() {
    if (state_ != ConstructionState::Selected || !selectedTank()) return false;
    state_ = ConstructionState::DeleteConfirm;
    validation_message_.clear();
    return true;
}

bool AquariumConstructionSession::cancelDelete() {
    if (state_ != ConstructionState::DeleteConfirm) return false;
    state_ = ConstructionState::Selected;
    return true;
}

bool AquariumConstructionSession::reviewDraft() {
    if (!draft_ || (state_ != ConstructionState::ResizeFootprint &&
        state_ != ConstructionState::MoveTank && state_ != ConstructionState::ResizeTank &&
        state_ != ConstructionState::SubtractFootprint &&
        state_ != ConstructionState::PaintFootprint &&
        state_ != ConstructionState::TunnelRoute)) return false;
    refreshDraftValidation();
    state_ = ConstructionState::DraftReview;
    return true;
}

bool AquariumConstructionSession::adjustDraft() {
    if (state_ != ConstructionState::DraftReview || !draft_) return false;
    switch (draft_->operation) {
        case ConstructionDraftOperation::Create: state_ = ConstructionState::ResizeFootprint; break;
        case ConstructionDraftOperation::Move: state_ = ConstructionState::MoveTank; break;
        case ConstructionDraftOperation::Resize: state_ = ConstructionState::ResizeTank; break;
        case ConstructionDraftOperation::Subtract: state_ = ConstructionState::SubtractFootprint; break;
        case ConstructionDraftOperation::Add: state_ = ConstructionState::PaintFootprint; break;
        case ConstructionDraftOperation::Properties: state_ = ConstructionState::DraftReview; break;
        case ConstructionDraftOperation::Tunnel: state_ = ConstructionState::TunnelRoute; break;
        case ConstructionDraftOperation::TunnelRemove: state_ = ConstructionState::DraftReview; break;
    }
    return true;
}

bool AquariumConstructionSession::cancel() {
    if (state_ == ConstructionState::Selected) {
        clearSelection();
        return true;
    }
    if (state_ == ConstructionState::DeleteConfirm) return cancelDelete();
    if (state_ == ConstructionState::ResizeFootprint || state_ == ConstructionState::MoveTank ||
        state_ == ConstructionState::ResizeTank || state_ == ConstructionState::SubtractFootprint ||
        state_ == ConstructionState::PaintFootprint ||
        state_ == ConstructionState::TunnelRoute ||
        state_ == ConstructionState::DraftReview ||
        state_ == ConstructionState::Building) {
        const bool restore_browse = draft_ && draft_->restore_browse_on_cancel;
        const bool editing_existing = draft_ &&
            draft_->operation != ConstructionDraftOperation::Create && selectedTank();
        draft_.reset();
        pending_operation_token_ = 0;
        if (restore_browse) selected_tank_id_.reset();
        state_ = editing_existing && !restore_browse
            ? ConstructionState::Selected : ConstructionState::Browse;
        validation_message_.clear();
        return true;
    }
    return false;
}

bool AquariumConstructionSession::finishNoOpDraft() {
    if (state_ != ConstructionState::DraftReview || !draft_ ||
        !draft_->area_selection || draft_->paint_changed) return false;
    return cancel();
}

geo::TankDesign AquariumConstructionSession::draftTank() const {
    if (!draft_) return {};
    if (!draft_->paint_original_tanks.empty() && draft_->original_tank) {
        const auto found = std::find_if(
            draft_->paint_candidate_tanks.begin(), draft_->paint_candidate_tanks.end(),
            [&](const auto& tank) { return tank.id == draft_->original_tank->id; });
        if (found != draft_->paint_candidate_tanks.end()) return *found;
        return {};
    }
    if (draft_->candidate_tank) return *draft_->candidate_tank;
    geo::TankDesign tank;
    const int min_column = std::min(draft_->anchor.column, draft_->cursor.column);
    const int min_row = std::min(draft_->anchor.row, draft_->cursor.row);
    const std::string base_id = "tank_" + std::to_string(committed_.revision + 1);
    tank.id = base_id;
    int suffix = 2;
    while (std::any_of(committed_.tanks.begin(), committed_.tanks.end(), [&](const auto& existing) {
        return existing.id == tank.id;
    })) tank.id = base_id + "_" + std::to_string(suffix++);
    tank.footprint.origin_cell = {min_column, min_row};
    tank.footprint.width_cells = std::abs(draft_->cursor.column - draft_->anchor.column) + 1;
    tank.footprint.depth_cells = std::abs(draft_->cursor.row - draft_->anchor.row) + 1;
    tank.height_steps = 8;
    tank.corner_radius_steps = 0;
    return tank;
}

std::vector<geo::GridCell> AquariumConstructionSession::draftCells() const {
    if (draft_ && draft_->operation == ConstructionDraftOperation::Tunnel) {
        return tunnelRouteCells();
    }
    if (draft_ && draft_->area_selection) {
        std::vector<geo::GridCell> cells;
        const int min_column = std::min(draft_->anchor.column, draft_->cursor.column);
        const int max_column = std::max(draft_->anchor.column, draft_->cursor.column);
        const int min_row = std::min(draft_->anchor.row, draft_->cursor.row);
        const int max_row = std::max(draft_->anchor.row, draft_->cursor.row);
        cells.reserve(static_cast<std::size_t>(max_column - min_column + 1) *
            static_cast<std::size_t>(max_row - min_row + 1));
        for (int row = min_row; row <= max_row; ++row) {
            for (int column = min_column; column <= max_column; ++column) {
                cells.push_back({column, row});
            }
        }
        return cells;
    }
    if (draft_ && !draft_->paint_candidate_tanks.empty()) {
        std::vector<geo::GridCell> cells;
        for (const auto& tank : draft_->paint_candidate_tanks) {
            if (std::find(draft_->paint_affected_ids.begin(), draft_->paint_affected_ids.end(),
                    tank.id) == draft_->paint_affected_ids.end()) continue;
            const auto tank_cells = tankFootprintCells(tank);
            cells.insert(cells.end(), tank_cells.begin(), tank_cells.end());
        }
        return cells;
    }
    if (draft_ && !draft_->delete_candidate) {
        if (draft_->operation == ConstructionDraftOperation::Create) {
            return tankFootprintCells(
                resolveDrawnTank(committed_.tanks, draftTank()).preview_tank);
        }
        return tankFootprintCells(draftTank());
    }
    return {};
}

std::vector<geo::GridCell> AquariumConstructionSession::draftOriginalCells() const {
    std::vector<geo::GridCell> cells;
    if (!draft_) return cells;
    if (draft_->operation == ConstructionDraftOperation::Create) {
        const auto resolution = resolveDrawnTank(committed_.tanks, draftTank());
        for (const auto& tank : committed_.tanks) {
            if (std::find(resolution.affected_tank_ids.begin(),
                    resolution.affected_tank_ids.end(), tank.id) ==
                resolution.affected_tank_ids.end()) continue;
            const auto tank_cells = tankFootprintCells(tank);
            cells.insert(cells.end(), tank_cells.begin(), tank_cells.end());
        }
        return cells;
    }
    if (!draft_->paint_original_tanks.empty()) {
        for (const auto& tank : draft_->paint_original_tanks) {
            if (std::find(draft_->paint_affected_ids.begin(), draft_->paint_affected_ids.end(),
                    tank.id) == draft_->paint_affected_ids.end()) continue;
            const auto tank_cells = tankFootprintCells(tank);
            cells.insert(cells.end(), tank_cells.begin(), tank_cells.end());
        }
        return cells;
    }
    if (draft_->original_tank) return tankFootprintCells(*draft_->original_tank);
    return cells;
}

std::vector<geo::GridCell> AquariumConstructionSession::draftCutCells() const {
    if (!draft_ || draft_->paint_original_tanks.empty()) return {};
    const auto original = draftOriginalCells();
    std::vector<geo::GridCell> candidate;
    for (const auto& tank : draft_->paint_candidate_tanks) {
        if (std::find(draft_->paint_affected_ids.begin(), draft_->paint_affected_ids.end(),
                tank.id) == draft_->paint_affected_ids.end()) continue;
        const auto tank_cells = tankFootprintCells(tank);
        candidate.insert(candidate.end(), tank_cells.begin(), tank_cells.end());
    }
    std::vector<geo::GridCell> cut;
    for (const auto cell : original) {
        if (std::none_of(candidate.begin(), candidate.end(), [&](auto retained) {
                return sameCell(cell, retained);
            })) cut.push_back(cell);
    }
    return cut;
}

bool AquariumConstructionSession::cellAllowed(geo::GridCell cell) const {
    return std::any_of(allowed_cells_.begin(), allowed_cells_.end(),
        [&](auto allowed) { return sameCell(cell, allowed); });
}

bool AquariumConstructionSession::occupiedByCommitted(
    geo::GridCell cell, const std::string& ignored_tank_id) const {
    if (std::any_of(authored_obstacles_.begin(), authored_obstacles_.end(),
            [&](auto occupied) { return sameCell(cell, occupied); })) return true;
    for (const geo::TankDesign& tank : committed_.tanks) {
        if (!ignored_tank_id.empty() && tank.id == ignored_tank_id) continue;
        const auto cells = tankFootprintCells(tank);
        if (std::any_of(cells.begin(), cells.end(),
                [&](geo::GridCell occupied) { return sameCell(cell, occupied); })) return true;
    }
    return false;
}

bool AquariumConstructionSession::cellBlocked(geo::GridCell cell) const {
    return occupiedByCommitted(cell);
}

void AquariumConstructionSession::refreshDraftValidation() {
    validation_message_.clear();
    if (!draft_) return;
    if (draft_->operation == ConstructionDraftOperation::Tunnel) {
        if (draft_->tunnel_route.size() < 3U) {
            validation_message_ = "Route through the tank to another portal";
            return;
        }
        const geo::GridCell endpoint{draft_->tunnel_route.back().column,
            draft_->tunnel_route.back().row};
        if (!isTunnelPortalCell(endpoint) || sameCell(endpoint, draft_->anchor)) {
            validation_message_ = "Finish on a different glowing tank-edge portal";
            return;
        }
    }
    if (!draft_->paint_original_tanks.empty()) {
        if (!draft_->paint_changed) {
            if (draft_->area_selection) return;
            validation_message_ = draft_->operation == ConstructionDraftOperation::Add
                ? "Move the path to a tank edge" : "Move the selection across a tank";
            return;
        }
        if (draft_->paint_candidate_tanks.size() > 8U) {
            validation_message_ = "This room supports eight player tanks";
            return;
        }
        std::set<std::pair<int, int>> occupied;
        for (const auto& candidate : draft_->paint_candidate_tanks) {
            if (candidate.footprint.width_cells < 3 || candidate.footprint.depth_cells < 3) {
                validation_message_ = "Keep every tank at least three cells in both directions";
                return;
            }
            const bool affected = std::find(
                draft_->paint_affected_ids.begin(), draft_->paint_affected_ids.end(),
                candidate.id) != draft_->paint_affected_ids.end();
            if (affected && !geo::footprintHasMinimumThreeCellSections(candidate.footprint)) {
                validation_message_ =
                    "Keep every added or remaining tank section at least three cells wide";
                return;
            }
            for (const auto cell : tankFootprintCells(candidate)) {
                if (!cellAllowed(cell)) {
                    validation_message_ = "Tank footprint leaves the construction area";
                    return;
                }
                if (std::any_of(authored_obstacles_.begin(), authored_obstacles_.end(),
                        [&](auto blocked) { return sameCell(cell, blocked); })) {
                    validation_message_ = "Tank footprint overlaps an existing obstacle";
                    return;
                }
                if (!occupied.emplace(cell.column, cell.row).second) {
                    validation_message_ = "Tank footprint overlaps another player tank";
                    return;
                }
            }
            geo::AquariumBuildRequest request;
            request.tank = candidate;
            const auto validation = geo::validateAquarium(request);
            if (!validation.valid() && !validation.diagnostics.empty()) {
                validation_message_ = validation.diagnostics.front().message;
                return;
            }
        }
        return;
    }
    if (draft_->delete_candidate) return;
    geo::TankDesign tank = draftTank();
    if (draft_->operation == ConstructionDraftOperation::Create) {
        const auto resolution = resolveDrawnTank(committed_.tanks, tank);
        tank = resolution.preview_tank;
        if (resolution.extends_existing &&
            !geo::footprintHasMinimumThreeCellSections(tank.footprint)) {
            validation_message_ =
                "Keep every added or remaining tank section at least three cells wide";
            return;
        }
    }
    if (tank.footprint.width_cells < 3 || tank.footprint.depth_cells < 3) {
        validation_message_ = "Expand the tank to at least three cells in both directions";
        return;
    }
    if ((draft_->operation == ConstructionDraftOperation::Add ||
         draft_->operation == ConstructionDraftOperation::Subtract) &&
        !geo::footprintHasMinimumThreeCellSections(tank.footprint)) {
        validation_message_ =
            "Keep every added or remaining tank section at least three cells wide";
        return;
    }
    const std::string ignored_id = draft_->original_tank ? draft_->original_tank->id : std::string{};
    const auto create_resolution = draft_->operation == ConstructionDraftOperation::Create
        ? std::optional<DrawnTankResolution>(resolveDrawnTank(committed_.tanks, draftTank()))
        : std::nullopt;
    for (const geo::GridCell cell : tankFootprintCells(tank)) {
        if (!cellAllowed(cell)) {
            validation_message_ = "Tank footprint leaves the construction area";
            return;
        }
        const bool belongs_to_merged_tank = create_resolution &&
            std::any_of(create_resolution->affected_tank_ids.begin(),
                create_resolution->affected_tank_ids.end(), [&](const auto& id) {
                    const auto existing = std::find_if(
                        committed_.tanks.begin(), committed_.tanks.end(),
                        [&](const auto& candidate) { return candidate.id == id; });
                    if (existing == committed_.tanks.end()) return false;
                    const auto existing_cells = tankFootprintCells(*existing);
                    return std::any_of(existing_cells.begin(), existing_cells.end(),
                        [&](auto occupied) { return sameCell(cell, occupied); });
                });
        if (!belongs_to_merged_tank && occupiedByCommitted(cell, ignored_id)) {
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

std::uint64_t AquariumConstructionSession::beginPendingOperation() {
    pending_operation_token_ = next_operation_token_++;
    if (next_operation_token_ == 0) next_operation_token_ = 1;
    state_ = ConstructionState::Building;
    return pending_operation_token_;
}

std::optional<ConstructionCommitCandidate> AquariumConstructionSession::prepareHistoryCommand(
    const AquariumConstructionCommand& command,
    AquariumCommandDirection direction,
    ConstructionHistoryAction history_action) {
    std::string error;
    auto document = applyAquariumConstructionCommand(committed_, command, direction, &error);
    if (!document) {
        validation_message_ = std::move(error);
        return std::nullopt;
    }
    std::optional<std::string> selection;
    if (command.kind == AquariumCommandKind::EditTankSet) {
        const auto& resulting_tanks = direction == AquariumCommandDirection::Forward
            ? command.tanks_after : command.tanks_before;
        if (std::any_of(resulting_tanks.begin(), resulting_tanks.end(),
                [&](const auto& tank) { return tank.id == command.tank_id; })) {
            selection = command.tank_id;
        }
    } else {
        const auto& resulting_tank = direction == AquariumCommandDirection::Forward
            ? command.after : command.before;
        if (resulting_tank) selection = command.tank_id;
    }
    return ConstructionCommitCandidate{
        std::move(*document), command, history_action, beginPendingOperation(), selection};
}

std::optional<ConstructionCommitCandidate> AquariumConstructionSession::prepareCommit() {
    if (state_ != ConstructionState::DraftReview || !draft_) return std::nullopt;
    refreshDraftValidation();
    if (!draftValid()) {
        return std::nullopt;
    }
    if (!draft_->paint_original_tanks.empty()) {
        AquariumConstructionCommand command;
        command.kind = AquariumCommandKind::EditTankSet;
        command.tank_id = draft_->original_tank ? draft_->original_tank->id : std::string{};
        command.tanks_before = draft_->paint_original_tanks;
        command.tanks_after = draft_->paint_candidate_tanks;
        auto candidate = prepareHistoryCommand(command, AquariumCommandDirection::Forward,
            ConstructionHistoryAction::RecordNew);
        if (candidate && draft_->restore_browse_on_cancel) {
            candidate->selection_after_publish.reset();
        }
        return candidate;
    }
    if (draft_->delete_candidate) {
        if (!draft_->original_tank) return std::nullopt;
        const auto index = playerTankIndex(committed_, draft_->original_tank->id);
        if (!index) return std::nullopt;
        AquariumConstructionCommand command;
        command.kind = AquariumCommandKind::DeleteTank;
        command.tank_id = draft_->original_tank->id;
        command.before = *draft_->original_tank;
        command.before_index = *index;
        return prepareHistoryCommand(command, AquariumCommandDirection::Forward,
            ConstructionHistoryAction::RecordNew);
    }
    const geo::TankDesign candidate_tank = draftTank();
    if (draft_->operation == ConstructionDraftOperation::Create) {
        const auto resolution = resolveDrawnTank(committed_.tanks, candidate_tank);
        if (resolution.extends_existing) {
            AquariumConstructionCommand command;
            command.kind = AquariumCommandKind::EditTankSet;
            command.tank_id = resolution.primary_tank_id;
            command.tanks_before = committed_.tanks;
            command.tanks_after = resolution.tanks;
            auto candidate = prepareHistoryCommand(command,
                AquariumCommandDirection::Forward, ConstructionHistoryAction::RecordNew);
            if (candidate) candidate->selection_after_publish.reset();
            return candidate;
        }
        if (committed_.tanks.size() >= 8U) {
            validation_message_ = "This room supports eight player tanks";
            return std::nullopt;
        }
    }
    AquariumConstructionCommand command;
    command.tank_id = candidate_tank.id;
    command.after = candidate_tank;
    if (draft_->operation == ConstructionDraftOperation::Create) {
        command.kind = AquariumCommandKind::CreateTank;
        command.after_index = committed_.tanks.size();
    } else {
        if (!draft_->original_tank) return std::nullopt;
        const auto index = playerTankIndex(committed_, candidate_tank.id);
        if (!index) return std::nullopt;
        command.kind = AquariumCommandKind::EditTank;
        command.before = *draft_->original_tank;
        command.before_index = *index;
        command.after_index = *index;
    }
    auto candidate = prepareHistoryCommand(
        command, AquariumCommandDirection::Forward, ConstructionHistoryAction::RecordNew);
    if (candidate && command.kind == AquariumCommandKind::CreateTank) {
        candidate->selection_after_publish.reset();
    }
    return candidate;
}

std::optional<ConstructionCommitCandidate> AquariumConstructionSession::prepareDelete() {
    const geo::TankDesign* tank = selectedTank();
    if (state_ != ConstructionState::DeleteConfirm || !tank) return std::nullopt;
    const auto index = playerTankIndex(committed_, tank->id);
    if (!index) return std::nullopt;
    AquariumConstructionCommand command;
    command.kind = AquariumCommandKind::DeleteTank;
    command.tank_id = tank->id;
    command.before = *tank;
    command.before_index = *index;
    return prepareHistoryCommand(
        command, AquariumCommandDirection::Forward, ConstructionHistoryAction::RecordNew);
}

std::optional<ConstructionCommitCandidate> AquariumConstructionSession::prepareUndo() {
    if ((state_ != ConstructionState::Browse && state_ != ConstructionState::Selected) ||
        !history_.undoCommand()) return std::nullopt;
    return prepareHistoryCommand(*history_.undoCommand(),
        AquariumCommandDirection::Reverse, ConstructionHistoryAction::Undo);
}

std::optional<ConstructionCommitCandidate> AquariumConstructionSession::prepareRedo() {
    if ((state_ != ConstructionState::Browse && state_ != ConstructionState::Selected) ||
        !history_.redoCommand()) return std::nullopt;
    return prepareHistoryCommand(*history_.redoCommand(),
        AquariumCommandDirection::Forward, ConstructionHistoryAction::Redo);
}

bool AquariumConstructionSession::candidateCurrent(
    const ConstructionCommitCandidate& candidate) const {
    return state_ == ConstructionState::Building && pending_operation_token_ != 0 &&
        pending_operation_token_ == candidate.operation_token;
}

bool AquariumConstructionSession::publish(ConstructionCommitCandidate candidate) {
    if (!candidateCurrent(candidate)) return false;
    committed_ = std::move(candidate.document);
    switch (candidate.history_action) {
        case ConstructionHistoryAction::RecordNew: history_.publishNew(std::move(candidate.command)); break;
        case ConstructionHistoryAction::Undo: history_.publishUndo(); break;
        case ConstructionHistoryAction::Redo: history_.publishRedo(); break;
    }
    selected_tank_id_ = std::move(candidate.selection_after_publish);
    draft_.reset();
    pending_operation_token_ = 0;
    state_ = selectedTank() ? ConstructionState::Selected : ConstructionState::Browse;
    validation_message_.clear();
    return true;
}

void AquariumConstructionSession::rejectCommit(std::string message) {
    pending_operation_token_ = 0;
    state_ = draft_ ? ConstructionState::DraftReview
        : selectedTank() ? ConstructionState::Selected : ConstructionState::Browse;
    validation_message_ = std::move(message);
}

} // namespace pr::gameplay::world3d::aquarium::construction
