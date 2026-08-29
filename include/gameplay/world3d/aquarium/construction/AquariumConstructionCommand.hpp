#pragma once

#include "gameplay/world3d/aquarium/construction/AquariumDesign.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::aquarium::construction {

enum class AquariumCommandKind {
    CreateTank,
    EditTank,
    DeleteTank,
};

enum class AquariumCommandDirection {
    Forward,
    Reverse,
};

struct AquariumConstructionCommand {
    AquariumCommandKind kind = AquariumCommandKind::CreateTank;
    std::string tank_id;
    std::optional<pr::aquarium::geometry::TankDesign> before;
    std::optional<pr::aquarium::geometry::TankDesign> after;
    std::size_t before_index = 0;
    std::size_t after_index = 0;
};

bool tankDesignEquivalent(
    const pr::aquarium::geometry::TankDesign& lhs,
    const pr::aquarium::geometry::TankDesign& rhs);

std::optional<AquariumDesignDocument> applyAquariumConstructionCommand(
    const AquariumDesignDocument& current,
    const AquariumConstructionCommand& command,
    AquariumCommandDirection direction,
    std::string* error = nullptr);

class AquariumConstructionHistory {
public:
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    std::size_t undoCount() const { return undo_.size(); }
    std::size_t redoCount() const { return redo_.size(); }

    const AquariumConstructionCommand* undoCommand() const;
    const AquariumConstructionCommand* redoCommand() const;
    void publishNew(AquariumConstructionCommand command);
    bool publishUndo();
    bool publishRedo();
    void clear();

private:
    std::vector<AquariumConstructionCommand> undo_;
    std::vector<AquariumConstructionCommand> redo_;
};

} // namespace pr::gameplay::world3d::aquarium::construction
