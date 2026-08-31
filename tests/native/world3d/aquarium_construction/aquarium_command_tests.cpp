#include "gameplay/world3d/aquarium/construction/AquariumConstructionCommand.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumTankEditing.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace construction = pr::gameplay::world3d::aquarium::construction;
namespace geo = pr::aquarium::geometry;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

geo::TankDesign tank(std::string id, int column, int row, int width = 3, int depth = 3) {
    geo::TankDesign result;
    result.id = std::move(id);
    result.footprint.origin_cell = {column, row};
    result.footprint.width_cells = width;
    result.footprint.depth_cells = depth;
    return result;
}

construction::AquariumDesignDocument documentWithTwoTanks() {
    construction::AquariumDesignDocument document;
    document.design_id = "aqd_commands";
    document.map_id = "aquarium12";
    document.revision = 7;
    document.tanks = {tank("tank_alpha", 4, 5), tank("tank_beta", 12, 5, 4, 3)};
    return document;
}

void editCommandRoundTripsWithStableIdentityAndOrder() {
    const auto original = documentWithTwoTanks();
    const auto moved = construction::moveTankByCells(original.tanks.front(), 3, 2);
    construction::AquariumConstructionCommand command;
    command.kind = construction::AquariumCommandKind::EditTank;
    command.tank_id = moved.id;
    command.before = original.tanks.front();
    command.after = moved;
    command.before_index = 0;
    command.after_index = 0;

    std::string error;
    const auto forward = construction::applyAquariumConstructionCommand(
        original, command, construction::AquariumCommandDirection::Forward, &error);
    require(forward && error.empty() && forward->revision == 8,
        "forward edit did not produce the next durable revision");
    require(forward->tanks.size() == 2 && forward->tanks[0].id == "tank_alpha" &&
            forward->tanks[1].id == "tank_beta" &&
            forward->tanks[0].footprint.origin_cell.column == 7,
        "forward edit changed stable identity, order, or the wrong footprint");

    const auto reverse = construction::applyAquariumConstructionCommand(
        *forward, command, construction::AquariumCommandDirection::Reverse, &error);
    require(reverse && reverse->revision == 9 && reverse->tanks.size() == 2,
        "undo edit did not create a new monotonic revision");
    require(construction::tankDesignEquivalent(reverse->tanks[0], original.tanks[0]) &&
            construction::tankDesignEquivalent(reverse->tanks[1], original.tanks[1]),
        "undo edit did not restore the exact authored tank values and order");
}

void deleteCommandIsReversibleAndRedoable() {
    const auto original = documentWithTwoTanks();
    construction::AquariumConstructionCommand command;
    command.kind = construction::AquariumCommandKind::DeleteTank;
    command.tank_id = "tank_beta";
    command.before = original.tanks[1];
    command.before_index = 1;
    const auto deleted = construction::applyAquariumConstructionCommand(
        original, command, construction::AquariumCommandDirection::Forward);
    require(deleted && deleted->tanks.size() == 1 && deleted->tanks[0].id == "tank_alpha",
        "delete command did not remove only its stable target");
    const auto restored = construction::applyAquariumConstructionCommand(
        *deleted, command, construction::AquariumCommandDirection::Reverse);
    require(restored && restored->tanks.size() == 2 && restored->tanks[1].id == "tank_beta",
        "undo delete did not restore the target at its exact document position");
    const auto redone = construction::applyAquariumConstructionCommand(
        *restored, command, construction::AquariumCommandDirection::Forward);
    require(redone && redone->revision == 10 && redone->tanks.size() == 1,
        "redo delete did not remain monotonic and deterministic");
}

void historyPublishesOnlyAtCommitBoundariesAndBranches() {
    construction::AquariumConstructionHistory history;
    auto first = construction::AquariumConstructionCommand{};
    first.tank_id = "tank_first";
    history.publishNew(first);
    require(history.canUndo() && !history.canRedo() && history.undoCount() == 1,
        "newly published command did not enter undo history");
    require(history.publishUndo() && !history.canUndo() && history.canRedo(),
        "published undo did not move the command to redo history");
    require(history.publishRedo() && history.canUndo() && !history.canRedo(),
        "published redo did not move the command back to undo history");
    require(history.publishUndo(), "second published undo failed");
    auto branch = construction::AquariumConstructionCommand{};
    branch.tank_id = "tank_branch";
    history.publishNew(branch);
    require(history.undoCount() == 1 && history.undoCommand()->tank_id == "tank_branch" &&
            !history.canRedo(),
        "new command after undo did not clear the abandoned redo branch");
}

void staleCommandIsRejectedWithoutMutation() {
    auto document = documentWithTwoTanks();
    construction::AquariumConstructionCommand command;
    command.kind = construction::AquariumCommandKind::EditTank;
    command.tank_id = "tank_alpha";
    command.before = document.tanks[0];
    command.after = construction::moveTankByCells(document.tanks[0], 1, 0);
    document.tanks[0].height_steps += 1;
    std::string error;
    const auto rejected = construction::applyAquariumConstructionCommand(
        document, command, construction::AquariumCommandDirection::Forward, &error);
    require(!rejected && !error.empty() && document.revision == 7 &&
            document.tanks[0].footprint.origin_cell.column == 4,
        "stale command changed the current document or failed silently");
}

void tankSetCommandRoundTripsMergesAndMultiDeletes() {
    const auto original = documentWithTwoTanks();
    auto merged = original.tanks.front();
    merged.footprint.origin_cell = {4, 5};
    merged.footprint.width_cells = 12;
    merged.footprint.depth_cells = 3;

    construction::AquariumConstructionCommand command;
    command.kind = construction::AquariumCommandKind::EditTankSet;
    command.tank_id = "tank_alpha";
    command.tanks_before = original.tanks;
    command.tanks_after = {merged};
    const auto forward = construction::applyAquariumConstructionCommand(
        original, command, construction::AquariumCommandDirection::Forward);
    require(forward && forward->tanks.size() == 1 &&
            forward->tanks.front().id == "tank_alpha" && forward->revision == 8,
        "tank-set merge did not preserve the primary ID in one revision");
    const auto restored = construction::applyAquariumConstructionCommand(
        *forward, command, construction::AquariumCommandDirection::Reverse);
    require(restored && restored->tanks.size() == 2 &&
            construction::tankDesignEquivalent(restored->tanks[0], original.tanks[0]) &&
            construction::tankDesignEquivalent(restored->tanks[1], original.tanks[1]),
        "undo did not restore every merged tank exactly");
    const auto redone = construction::applyAquariumConstructionCommand(
        *restored, command, construction::AquariumCommandDirection::Forward);
    require(redone && redone->tanks.size() == 1 && redone->revision == 10,
        "redo did not reapply the multi-tank merge");

    command.tanks_after.clear();
    const auto deleted = construction::applyAquariumConstructionCommand(
        original, command, construction::AquariumCommandDirection::Forward);
    require(deleted && deleted->tanks.empty(),
        "one tank-set command could not delete multiple complete tanks");
}

void selectionMovementAndEveryResizeHandleStayCellAligned() {
    const auto document = documentWithTwoTanks();
    const auto* selected = construction::playerTankAtCell(document, {13, 6});
    require(selected && selected->id == "tank_beta",
        "cell selection did not resolve the player tank occupying that cell");
    require(!construction::playerTankAtCell(document, {9, 9}),
        "cell selection resolved empty space as a player tank");
    const auto centre = construction::tankCentreCell(document.tanks[1]);
    require(centre.column == 13 && centre.row == 6,
        "tank centre handle did not use the canonical whole-cell convention");

    const auto north_west = construction::resizeTankToCell(
        document.tanks[1], construction::AquariumResizeHandle::NorthWest, {10, 3});
    require(north_west.footprint.origin_cell.column == 10 &&
            north_west.footprint.origin_cell.row == 3 &&
            north_west.footprint.width_cells == 6 && north_west.footprint.depth_cells == 5,
        "north-west resize did not keep opposite boundaries fixed");
    const auto east = construction::resizeTankToCell(
        document.tanks[1], construction::AquariumResizeHandle::East, {18, 9});
    require(east.footprint.origin_cell.column == 12 &&
            east.footprint.origin_cell.row == 5 &&
            east.footprint.width_cells == 7 && east.footprint.depth_cells == 3,
        "east edge resize changed an unrelated boundary");
    const auto south = construction::resizeTankToCell(
        document.tanks[1], construction::AquariumResizeHandle::South, {2, 10});
    require(south.footprint.origin_cell.column == 12 &&
            south.footprint.width_cells == 4 && south.footprint.depth_cells == 6,
        "south edge resize changed width or ignored its whole-cell boundary");
}

} // namespace

int main() {
    try {
        editCommandRoundTripsWithStableIdentityAndOrder();
        deleteCommandIsReversibleAndRedoable();
        historyPublishesOnlyAtCommitBoundariesAndBranches();
        staleCommandIsRejectedWithoutMutation();
        tankSetCommandRoundTripsMergesAndMultiDeletes();
        selectionMovementAndEveryResizeHandleStayCellAligned();
        std::cout << "aquarium_command_tests: ok\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "aquarium_command_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
