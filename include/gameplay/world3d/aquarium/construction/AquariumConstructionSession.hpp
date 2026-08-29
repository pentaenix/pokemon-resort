#pragma once

#include "aquarium_geometry/Kernel.hpp"
#include "gameplay/world3d/aquarium/AquariumConfig.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumDesign.hpp"

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
    ResizeFootprint,
    DraftReview,
    Building,
};

struct ConstructionDraft {
    pr::aquarium::geometry::GridCell anchor;
    pr::aquarium::geometry::GridCell cursor;
};

struct ConstructionCommitCandidate {
    AquariumDesignDocument document;
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

    bool enter(pr::aquarium::geometry::GridCell preferred_cursor);
    void exit();
    void moveCursor(int column_delta, int row_delta);
    void pointAt(pr::aquarium::geometry::GridCell cell);
    bool beginRectangle();
    bool reviewDraft();
    bool adjustDraft();
    bool cancel();
    std::optional<ConstructionCommitCandidate> prepareCommit();
    void publish(ConstructionCommitCandidate candidate);
    void rejectCommit(std::string message);

    std::vector<pr::aquarium::geometry::GridCell> draftCells() const;
    bool draftValid() const;
    bool cellAllowed(pr::aquarium::geometry::GridCell cell) const;
    bool cellBlocked(pr::aquarium::geometry::GridCell cell) const;

private:
    pr::aquarium::geometry::TankDesign draftTank() const;
    bool occupiedByCommitted(pr::aquarium::geometry::GridCell cell) const;
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
    std::string validation_message_;
};

} // namespace pr::gameplay::world3d::aquarium::construction
