#include "ui/transfer_system/GameBoxBrowserController.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

pr::GameTransferBoxNameDropdownStyle makeDropdownStyle() {
    pr::GameTransferBoxNameDropdownStyle style;
    style.enabled = true;
    style.open_smoothing = 20.0;
    style.close_smoothing = 20.0;
    return style;
}

void testEnterSeedsBoxSelectionAndResetsOverlayState() {
    pr::transfer_system::GameBoxBrowserController controller;
    controller.enter(8, 3);

    expect(controller.gameBoxIndex() == 3, "enter should keep the requested starting box");
    expect(!controller.gameBoxSpaceMode(), "enter should start outside box-space mode");
    expect(!controller.dropdownOpenTarget(), "enter should start with dropdown closed");
    expect(controller.dropdownHighlightIndex() == 4, "enter should seed dropdown highlight (active box row, after rename row)");
}

void testBoxSpaceModeClampsAndScrollsByRows() {
    pr::transfer_system::GameBoxBrowserController controller;
    controller.enter(43, 0);

    expect(controller.setGameBoxSpaceMode(true, 43), "enabling box-space mode should report a state change");
    expect(controller.gameBoxSpaceMaxRowOffset(43) == 3, "43 boxes should require 3 extra row offsets");
    expect(controller.stepGameBoxSpaceRowDown(43), "box-space mode should allow moving down while rows remain");
    expect(controller.gameBoxSpaceRowOffset() == 1, "row offset should advance after stepping down");
    expect(controller.stepGameBoxSpaceRowUp(), "box-space mode should allow moving back up");
    expect(controller.gameBoxSpaceRowOffset() == 0, "row offset should return to zero after stepping up");
}

/// Resort panel uses the same browser math with 60 in-memory boxes (30 slots visible per Box Space row).
void testSixtyBoxResortScaleMatchesRowScrollContract() {
    pr::transfer_system::GameBoxBrowserController controller;
    controller.enter(60, 0);

    expect(controller.setGameBoxSpaceMode(true, 60), "60 boxes should enter box-space mode");
    expect(controller.gameBoxSpaceMaxRowOffset(60) == 5, "60 boxes should allow 5 scroll steps (rows 1–6 of boxes)");
    for (int i = 0; i < 5; ++i) {
        expect(controller.stepGameBoxSpaceRowDown(60), "row step down should succeed until max row");
    }
    expect(controller.gameBoxSpaceRowOffset() == 5, "row offset should reach max for 60 boxes");
    expect(controller.stepGameBoxSpaceRowDown(60), "row step down at max should wrap to the first row");
    expect(controller.gameBoxSpaceRowOffset() == 0, "wrap should return to the first Box Space row");
    for (int i = 0; i < 5; ++i) {
        expect(controller.stepGameBoxSpaceRowDown(60), "row step down should succeed from first row back to max");
    }
    expect(controller.gameBoxSpaceRowOffset() == 5, "row offset should reach max for 60 boxes");
    for (int i = 0; i < 5; ++i) {
        expect(controller.stepGameBoxSpaceRowUp(), "row step up should succeed back to zero");
    }
    expect(controller.gameBoxSpaceRowOffset() == 0, "row offset should return to first Box Space screen");
}

void testAdvanceAndJumpRespectReadinessAndWrap() {
    pr::transfer_system::GameBoxBrowserController controller;
    controller.enter(4, 0);

    expect(!controller.advanceGameBox(1, 4, false), "advance should not run while panels are not ready");
    expect(controller.advanceGameBox(-1, 4, true), "advance should wrap when panels are ready");
    expect(controller.gameBoxIndex() == 3, "advancing left from zero should wrap to last box");

    expect(controller.jumpGameBoxToIndex(1, 4, true), "jump should allow selecting a new box");
    expect(controller.gameBoxIndex() == 1, "jump should update the active box");
}

void testDropdownOpenHighlightScrollAndApplySelection() {
    pr::transfer_system::GameBoxBrowserController controller;
    controller.enter(12, 2);
    controller.setDropdownRowHeightPx(20);

    expect(controller.toggleGameBoxDropdown(true, false, 12, 60), "toggle should open dropdown when eligible");
    expect(controller.dropdownOpenTarget(), "dropdown should be marked open");
    expect(controller.dropdownHighlightIndex() == 3, "opening dropdown should highlight the active box row (index 2 + rename row)");

    expect(controller.stepDropdownHighlight(3, 12, 60), "highlight step should move selection");
    expect(controller.dropdownHighlightIndex() == 6, "highlight should advance by the requested delta");

    controller.scrollDropdownBy(90.0, 12, 60);
    expect(controller.dropdownScrollPx() > 0.0, "scrolling should update dropdown scroll position");

    controller.updateDropdown(1.0, makeDropdownStyle());
    expect(controller.dropdownExpandT() > 0.95, "dropdown update should animate open");

    expect(controller.applyDropdownSelection(12, true), "apply should commit the highlighted selection");
    expect(controller.gameBoxIndex() == 5, "apply should promote highlighted row to active box");
    expect(!controller.dropdownOpenTarget(), "apply should close the dropdown");
}

} // namespace

int main() {
    testEnterSeedsBoxSelectionAndResetsOverlayState();
    testBoxSpaceModeClampsAndScrollsByRows();
    testSixtyBoxResortScaleMatchesRowScrollContract();
    testAdvanceAndJumpRespectReadinessAndWrap();
    testDropdownOpenHighlightScrollAndApplySelection();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
