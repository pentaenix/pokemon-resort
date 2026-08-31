#include "gameplay/world3d/aquarium/construction/AquariumConstructionCommand.hpp"

#include <algorithm>
#include <utility>

namespace pr::gameplay::world3d::aquarium::construction {
namespace geo = pr::aquarium::geometry;

namespace {

bool sameCell(geo::CellPoint lhs, geo::CellPoint rhs) {
    return lhs.column == rhs.column && lhs.row == rhs.row;
}

bool tunnelEquivalent(const geo::TunnelDesign& lhs, const geo::TunnelDesign& rhs) {
    if (lhs.id != rhs.id || lhs.route != rhs.route ||
        lhs.centreline_cells.size() != rhs.centreline_cells.size()) return false;
    for (std::size_t index = 0; index < lhs.centreline_cells.size(); ++index) {
        if (!sameCell(lhs.centreline_cells[index], rhs.centreline_cells[index])) return false;
    }
    return true;
}

std::vector<geo::TankDesign>::const_iterator findTank(
    const AquariumDesignDocument& document, const std::string& tank_id) {
    return std::find_if(document.tanks.begin(), document.tanks.end(),
        [&](const auto& tank) { return tank.id == tank_id; });
}

} // namespace

bool tankDesignEquivalent(const geo::TankDesign& lhs, const geo::TankDesign& rhs) {
    const auto& a = lhs.footprint;
    const auto& b = rhs.footprint;
    if (lhs.id != rhs.id || a.shape != b.shape ||
        a.origin_cell.column != b.origin_cell.column ||
        a.origin_cell.row != b.origin_cell.row ||
        a.width_cells != b.width_cells || a.depth_cells != b.depth_cells ||
        a.rotation_quarter_turns != b.rotation_quarter_turns ||
        a.notch_width_cells != b.notch_width_cells ||
        a.notch_depth_cells != b.notch_depth_cells ||
        a.subtracted_cells.size() != b.subtracted_cells.size() ||
        lhs.height_steps != rhs.height_steps ||
        lhs.corner_radius_steps != rhs.corner_radius_steps ||
        lhs.corner_radii.size() != rhs.corner_radii.size() ||
        lhs.tunnels.size() != rhs.tunnels.size()) return false;
    for (std::size_t index = 0; index < a.subtracted_cells.size(); ++index) {
        if (a.subtracted_cells[index].column != b.subtracted_cells[index].column ||
            a.subtracted_cells[index].row != b.subtracted_cells[index].row) return false;
    }
    for (std::size_t index = 0; index < lhs.corner_radii.size(); ++index) {
        const auto& left = lhs.corner_radii[index];
        const auto& right = rhs.corner_radii[index];
        if (left.vertex.column != right.vertex.column ||
            left.vertex.row != right.vertex.row ||
            left.radius_steps != right.radius_steps) return false;
    }
    for (std::size_t index = 0; index < lhs.tunnels.size(); ++index) {
        if (!tunnelEquivalent(lhs.tunnels[index], rhs.tunnels[index])) return false;
    }
    return true;
}

std::optional<AquariumDesignDocument> applyAquariumConstructionCommand(
    const AquariumDesignDocument& current,
    const AquariumConstructionCommand& command,
    AquariumCommandDirection direction,
    std::string* error) {
    const auto fail = [&](std::string message) -> std::optional<AquariumDesignDocument> {
        if (error) *error = std::move(message);
        return std::nullopt;
    };
    if (command.kind == AquariumCommandKind::EditTankSet) {
        const auto& expected_tanks = direction == AquariumCommandDirection::Forward
            ? command.tanks_before : command.tanks_after;
        const auto& replacement_tanks = direction == AquariumCommandDirection::Forward
            ? command.tanks_after : command.tanks_before;
        if (current.tanks.size() != expected_tanks.size()) {
            return fail("Tank-set command no longer matches the aquarium document");
        }
        for (std::size_t index = 0; index < current.tanks.size(); ++index) {
            if (!tankDesignEquivalent(current.tanks[index], expected_tanks[index])) {
                return fail("Tank-set command target changed before publication");
            }
        }
        AquariumDesignDocument result = current;
        result.tanks = replacement_tanks;
        ++result.revision;
        if (error) error->clear();
        return result;
    }
    const auto& expected = direction == AquariumCommandDirection::Forward
        ? command.before : command.after;
    const auto& replacement = direction == AquariumCommandDirection::Forward
        ? command.after : command.before;
    const std::size_t replacement_index = direction == AquariumCommandDirection::Forward
        ? command.after_index : command.before_index;

    const auto found = findTank(current, command.tank_id);
    if (expected) {
        if (found == current.tanks.end()) {
            return fail("Command target is missing: " + command.tank_id);
        }
        if (!tankDesignEquivalent(*found, *expected)) {
            return fail("Command target changed before publication: " + command.tank_id);
        }
    } else if (found != current.tanks.end()) {
        return fail("Command would create a duplicate tank ID: " + command.tank_id);
    }

    AquariumDesignDocument result = current;
    auto mutable_found = std::find_if(result.tanks.begin(), result.tanks.end(),
        [&](const auto& tank) { return tank.id == command.tank_id; });
    if (mutable_found != result.tanks.end()) result.tanks.erase(mutable_found);
    if (replacement) {
        if (replacement->id != command.tank_id) {
            return fail("Command replacement changed its stable tank ID");
        }
        const std::size_t index = std::min(replacement_index, result.tanks.size());
        result.tanks.insert(result.tanks.begin() + static_cast<std::ptrdiff_t>(index), *replacement);
    }
    ++result.revision;
    if (error) error->clear();
    return result;
}

const AquariumConstructionCommand* AquariumConstructionHistory::undoCommand() const {
    return undo_.empty() ? nullptr : &undo_.back();
}

const AquariumConstructionCommand* AquariumConstructionHistory::redoCommand() const {
    return redo_.empty() ? nullptr : &redo_.back();
}

void AquariumConstructionHistory::publishNew(AquariumConstructionCommand command) {
    undo_.push_back(std::move(command));
    redo_.clear();
}

bool AquariumConstructionHistory::publishUndo() {
    if (undo_.empty()) return false;
    redo_.push_back(std::move(undo_.back()));
    undo_.pop_back();
    return true;
}

bool AquariumConstructionHistory::publishRedo() {
    if (redo_.empty()) return false;
    undo_.push_back(std::move(redo_.back()));
    redo_.pop_back();
    return true;
}

void AquariumConstructionHistory::clear() {
    undo_.clear();
    redo_.clear();
}

} // namespace pr::gameplay::world3d::aquarium::construction
