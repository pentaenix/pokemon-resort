#include "mapmaker/commands/CommandStack.hpp"
#include "mapmaker/commands/TerrainCommands.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void expect(bool condition, const std::string& message) {
    if (!condition) throw TestFailure(message);
}

pr::mapmaker::TerrainCell height(int value) {
    return {static_cast<std::uint8_t>(value), 0, false};
}

void testExecuteUndoRedoAndCleanRevision() {
    pr::mapmaker::TerrainGrid terrain(3, 2);
    pr::mapmaker::CommandStack commands;
    expect(!commands.isDirty(), "fresh stack starts clean");
    expect(commands.execute(pr::mapmaker::makeSetTerrainCellCommand(
        terrain, 1, 0, height(4), "Raise terrain")), "edit executes");
    expect(terrain.cell(1, 0).height == 4, "execute applies terrain value");
    expect(commands.canUndo() && commands.undoLabel() == "Raise terrain", "undo describes edit");
    expect(commands.isDirty(), "edit dirties stack");

    commands.markClean();
    expect(!commands.isDirty(), "markClean records current revision");
    expect(commands.undo(), "undo succeeds");
    expect(terrain.cell(1, 0).height == 0, "undo restores prior value");
    expect(commands.isDirty(), "undo away from saved revision is dirty");
    expect(commands.redo(), "redo succeeds");
    expect(terrain.cell(1, 0).height == 4 && !commands.isDirty(),
        "redo returns to saved revision");
}

void testCoalescingProducesOneUndoStep() {
    pr::mapmaker::TerrainGrid terrain(2, 2);
    pr::mapmaker::CommandStack commands;
    expect(commands.execute(pr::mapmaker::makeSetTerrainCellCommand(
        terrain, 0, 0, height(1), "Drag height", "height-drag")), "first drag sample applies");
    expect(commands.execute(pr::mapmaker::makeSetTerrainCellCommand(
        terrain, 0, 0, height(2), "Drag height", "height-drag")), "second sample applies");
    expect(commands.execute(pr::mapmaker::makeSetTerrainCellCommand(
        terrain, 0, 0, height(3), "Drag height", "height-drag")), "third sample applies");
    expect(terrain.cell(0, 0).height == 3, "latest coalesced value wins");
    expect(commands.undoCount() == 1U, "drag samples coalesce into one history entry");
    expect(commands.undo() && terrain.cell(0, 0).height == 0,
        "one undo restores the value before the drag");
}

void testCoalescingNetNoopRemovesHistory() {
    pr::mapmaker::TerrainGrid terrain(1, 1);
    pr::mapmaker::CommandStack commands;
    commands.execute(pr::mapmaker::makeSetTerrainCellCommand(
        terrain, 0, 0, height(7), "Drag", "drag"));
    commands.execute(pr::mapmaker::makeSetTerrainCellCommand(
        terrain, 0, 0, height(0), "Drag", "drag"));
    expect(terrain.cell(0, 0).height == 0, "net-noop drag restores original terrain");
    expect(!commands.canUndo() && !commands.isDirty(), "net-noop drag leaves no history or dirty state");
}

void testTransactionIsAtomicAndCancelable() {
    pr::mapmaker::TerrainGrid terrain(3, 1);
    pr::mapmaker::CommandStack commands;
    expect(commands.beginTransaction("Paint stroke"), "transaction opens");
    commands.execute(pr::mapmaker::makeSetTerrainCellCommand(terrain, 0, 0, height(2)));
    commands.execute(pr::mapmaker::makeSetTerrainCellCommand(terrain, 1, 0, height(3)));
    expect(commands.isDirty() && !commands.canUndo(), "open transaction is dirty but not partially undoable");
    expect(commands.commitTransaction(), "transaction commits");
    expect(commands.undoCount() == 1U && commands.undoLabel() == "Paint stroke",
        "transaction becomes one named history entry");
    expect(commands.undo(), "stroke undoes atomically");
    expect(terrain.cell(0, 0).height == 0 && terrain.cell(1, 0).height == 0,
        "atomic undo restores every touched cell");

    expect(commands.beginTransaction("Canceled stroke"), "second transaction opens");
    commands.execute(pr::mapmaker::makeSetTerrainCellCommand(terrain, 2, 0, height(9)));
    expect(commands.cancelTransaction(), "transaction cancels");
    expect(terrain.cell(2, 0).height == 0, "cancel reverts applied preview edits");
}

void testBatchPatchSupportsHeightSpecialAndCollision() {
    pr::mapmaker::TerrainGrid terrain(2, 2);
    pr::mapmaker::TerrainCell changed{8, 4, true};
    std::vector<pr::mapmaker::TerrainCellPatch> patches{
        {0, 0, terrain.cell(0, 0), changed},
        {1, 1, terrain.cell(1, 1), {2, 1, false}},
    };
    pr::mapmaker::CommandStack commands;
    commands.execute(std::make_unique<pr::mapmaker::TerrainPatchCommand>(
        terrain, std::move(patches), "Paint terrain properties"));
    expect(terrain.cell(0, 0) == changed, "batch patch updates all terrain cell channels");
    expect(terrain.cell(1, 1).height == 2 && terrain.cell(1, 1).special == 1,
        "batch patch updates independent cells");
    commands.undo();
    expect(terrain.cell(0, 0) == pr::mapmaker::TerrainCell{} &&
        terrain.cell(1, 1) == pr::mapmaker::TerrainCell{}, "batch patch fully reverses");
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests{
        {"execute undo redo and clean revision", testExecuteUndoRedoAndCleanRevision},
        {"coalescing produces one undo step", testCoalescingProducesOneUndoStep},
        {"coalescing net noop removes history", testCoalescingNetNoopRemovesHistory},
        {"transaction is atomic and cancelable", testTransactionIsAtomicAndCancelable},
        {"batch patch supports terrain channels", testBatchPatchSupportsHeightSpecialAndCollision},
    };
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
