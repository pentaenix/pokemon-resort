#include "ui/transfer_ticket/TransferTicketListController.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

pr::TransferSaveSelection makeSelection(const std::string& game_key) {
    pr::TransferSaveSelection selection;
    selection.game_key = game_key;
    selection.game_title = game_key;
    selection.source_filename = game_key + ".sav";
    selection.source_path = "/tmp/" + game_key + ".sav";
    return selection;
}

pr::transfer_ticket::TransferTicketListController makeController() {
    pr::transfer_ticket::TransferTicketListController controller;
    controller.configure(
        pr::transfer_ticket::TicketListMetrics{
            45,
            167,
            308,
            SDL_Rect{0, 156, 1280, 615},
            14.0,
            432,
            262},
        pr::transfer_ticket::TicketRipAnimationConfig{
            true,
            28,
            3,
            0.08,
            0.35},
        pr::transfer_ticket::TicketSelectionTransitionConfig{
            0.12,
            255});
    return controller;
}

void testNavigationWrapsAndScrolls() {
    auto controller = makeController();
    controller.setSelections({
        makeSelection("ruby"),
        makeSelection("diamond"),
        makeSelection("platinum")});
    controller.enter();

    expect(controller.selectedTicketIndex() == 0, "enter selects the first ticket");
    expect(controller.scrollOffset() == 0.0, "enter starts at scroll offset 0");

    expect(controller.navigate(1), "navigate down should move selection");
    expect(controller.selectedTicketIndex() == 1, "navigate down selects next ticket");
    expect(controller.scrollOffset() == 0.0, "scroll remains eased until update");

    controller.update(0.5);
    expect(controller.scrollOffset() > 0.0, "update should ease toward centered scroll for lower tickets");

    expect(controller.navigate(-1), "navigate up should move selection back");
    expect(controller.selectedTicketIndex() == 0, "navigate up returns to first ticket");
    controller.update(0.5);
    expect(controller.scrollOffset() < 1.0, "returning to first ticket should settle scroll back near 0");

    expect(controller.navigate(-1), "navigate above first wraps to last ticket");
    expect(controller.selectedTicketIndex() == 2, "navigate above first wraps to last ticket");
    expect(controller.scrollOffset() > 0.0, "wrap to last should snap to its scroll position");
}

void testAdvanceRunsRipThenOpensTransferRequest() {
    auto controller = makeController();
    controller.setSelections({
        makeSelection("ruby"),
        makeSelection("diamond")});
    controller.enter();
    controller.navigate(1);

    expect(controller.advance(), "advance should start activation on the selected ticket");
    expect(controller.isRipAnimationActive(1), "advance starts rip animation for selected ticket");

    controller.update(0.60);
    expect(!controller.isRipAnimationActive(1), "rip animation should finish after enough time");
    expect(controller.isRipped(1), "selected ticket should be marked ripped after animation");
    expect(controller.fadeToBlackActive(), "handoff should enter fade-to-black before opening transfer");

    controller.update(0.20);
    pr::TransferSaveSelection selected;
    expect(controller.consumeOpenTransferSystemRequest(selected), "fade completion should request transfer-system open");
    expect(selected.game_key == "diamond", "handoff should preserve the selected transfer save");
}

void testBackAndReturnPreparationKeepSelectionStable() {
    auto controller = makeController();
    controller.setSelections({
        makeSelection("ruby"),
        makeSelection("diamond"),
        makeSelection("platinum")});
    controller.enter();
    controller.navigate(1);

    controller.back();
    expect(controller.consumeReturnToMainMenuRequest(), "back should request return to main menu");
    expect(!controller.consumeReturnToMainMenuRequest(), "return request should be one-shot");

    expect(controller.advance(), "advance should still work before preparing return");
    controller.update(0.60);
    controller.prepareReturnFromGameTransferScreen();
    expect(controller.selectedTicketIndex() == 1, "return from transfer keeps the prior ticket selected");
    expect(!controller.fadeToBlackActive(), "return preparation clears fade state");
    expect(!controller.isRipped(1), "return preparation restores unripped ticket art");
}

void testPointerDragScrollsAndClickActivates() {
    auto controller = makeController();
    controller.setSelections({
        makeSelection("ruby"),
        makeSelection("diamond"),
        makeSelection("platinum")});
    controller.enter();
    controller.navigate(1);
    controller.update(0.5);

    expect(controller.handlePointerPressed(200, 300), "press inside viewport should be consumed");
    controller.handlePointerMoved(200, 180);
    expect(controller.scrollOffset() > 0.0, "dragging upward should scroll the list");
    expect(controller.handlePointerReleased(200, 180), "releasing after drag should still consume the gesture");
    expect(controller.selectedTicketIndex() == 1, "dragging should not change selection");

    controller.enter();
    expect(controller.handlePointerPressed(200, 300), "press on first ticket should be consumed");
    expect(controller.handlePointerReleased(200, 300), "click release on first ticket should activate it");
    expect(controller.selectedTicketIndex() == 0, "click selects the clicked ticket");
    expect(controller.isRipAnimationActive(0), "click activation should start rip animation");
}

} // namespace

int main() {
    testNavigationWrapsAndScrolls();
    testAdvanceRunsRipThenOpensTransferRequest();
    testBackAndReturnPreparationKeepSelectionStable();
    testPointerDragScrollsAndClickActivates();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
