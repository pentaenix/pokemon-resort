#pragma once

#include "aquarium_geometry/Kernel.hpp"
#include "gameplay/world3d/aquarium/AquariumConfig.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumConstructionCommand.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumDesign.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumTankEditing.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::aquarium::construction {

std::vector<std::string> validateAquariumPlacement(
    const AquariumDesignDocument& document,
    const AquariumConstructionConfig& config,
    const std::vector<pr::aquarium::geometry::GridCell>& authored_obstacles);

enum class ConstructionState {
    Dormant,
    Browse,
    Selected,
    ResizeFootprint,
    MoveTank,
    ResizeTank,
    DraftReview,
    DeleteConfirm,
    Building,
};

enum class ConstructionDraftOperation {
    Create,
    Move,
    Resize,
};

struct ConstructionDraft {
    ConstructionDraftOperation operation = ConstructionDraftOperation::Create;
    pr::aquarium::geometry::GridCell anchor;
    pr::aquarium::geometry::GridCell cursor;
    std::optional<pr::aquarium::geometry::TankDesign> original_tank;
    std::optional<pr::aquarium::geometry::TankDesign> candidate_tank;
    AquariumResizeHandle resize_handle = AquariumResizeHandle::SouthEast;
};

enum class ConstructionHistoryAction {
    RecordNew,
    Undo,
    Redo,
};

struct ConstructionCommitCandidate {
    AquariumDesignDocument document;
    AquariumConstructionCommand command;
    ConstructionHistoryAction history_action = ConstructionHistoryAction::RecordNew;
    std::uint64_t operation_token = 0;
    std::optional<std::string> selection_after_publish;
};

class AquariumConstructionSession {
public:
    void configure(
        std::string map_id,
        const AquariumConstructionConfig& config,
        std::vector<pr::aquarium::geometry::GridCell> authored_obstacles,
        AquariumDesignDocument committed);

    bool available() const { return available_; }
    bool active() const { return state_ != ConstructionState::Dormant; }
    ConstructionState state() const { return state_; }
    const AquariumDesignDocument& committedDesign() const { return committed_; }
    const std::optional<ConstructionDraft>& draft() const { return draft_; }
    const std::vector<pr::aquarium::geometry::GridCell>& allowedCells() const {
        return allowed_cells_;
    }
    pr::aquarium::geometry::GridCell cursor() const { return cursor_; }
    const std::string& validationMessage() const { return validation_message_; }
    const std::optional<std::string>& selectedTankId() const { return selected_tank_id_; }
    const pr::aquarium::geometry::TankDesign* selectedTank() const;
    AquariumResizeHandle preferredResizeHandle() const;
    bool canUndo() const { return history_.canUndo() && state_ != ConstructionState::Building; }
    bool canRedo() const { return history_.canRedo() && state_ != ConstructionState::Building; }
    std::size_t undoCount() const { return history_.undoCount(); }
    std::size_t redoCount() const { return history_.redoCount(); }

    bool enter(pr::aquarium::geometry::GridCell preferred_cursor);
    void exit();
    void moveCursor(int column_delta, int row_delta);
    void pointAt(pr::aquarium::geometry::GridCell cell);
    bool beginRectangle();
    bool selectAtCursor();
    void clearSelection();
    bool beginMoveSelected();
    bool beginResizeSelected(AquariumResizeHandle handle);
    bool requestDeleteSelected();
    bool cancelDelete();
    bool reviewDraft();
    bool adjustDraft();
    bool cancel();
    std::optional<ConstructionCommitCandidate> prepareCommit();
    std::optional<ConstructionCommitCandidate> prepareDelete();
    std::optional<ConstructionCommitCandidate> prepareUndo();
    std::optional<ConstructionCommitCandidate> prepareRedo();
    bool candidateCurrent(const ConstructionCommitCandidate& candidate) const;
    bool publish(ConstructionCommitCandidate candidate);
    void rejectCommit(std::string message);

    std::vector<pr::aquarium::geometry::GridCell> draftCells() const;
    bool draftValid() const;
    bool cellAllowed(pr::aquarium::geometry::GridCell cell) const;
    bool cellBlocked(pr::aquarium::geometry::GridCell cell) const;

private:
    pr::aquarium::geometry::TankDesign draftTank() const;
    bool occupiedByCommitted(
        pr::aquarium::geometry::GridCell cell,
        const std::string& ignored_tank_id = {}) const;
    std::optional<ConstructionCommitCandidate> prepareHistoryCommand(
        const AquariumConstructionCommand& command,
        AquariumCommandDirection direction,
        ConstructionHistoryAction history_action);
    std::uint64_t beginPendingOperation();
    void refreshDraftValidation();

    bool available_ = false;
    std::string map_id_;
    ConstructionState state_ = ConstructionState::Dormant;
    AquariumDesignDocument committed_;
    std::vector<pr::aquarium::geometry::GridCell> allowed_cells_;
    std::vector<pr::aquarium::geometry::GridCell> authored_obstacles_;
    std::optional<pr::aquarium::geometry::GridCell> protected_player_cell_;
    pr::aquarium::geometry::GridCell cursor_{};
    std::optional<ConstructionDraft> draft_;
    std::optional<std::string> selected_tank_id_;
    AquariumConstructionHistory history_;
    std::uint64_t next_operation_token_ = 1;
    std::uint64_t pending_operation_token_ = 0;
    std::string validation_message_;
};

} // namespace pr::gameplay::world3d::aquarium::construction
