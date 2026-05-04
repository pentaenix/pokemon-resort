#include "ui/transfer_system/TransferSystemUiStateController.hpp"

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

pr::GameTransferPillToggleStyle makePillStyle() {
    pr::GameTransferPillToggleStyle style;
    style.toggle_smoothing = 20.0;
    style.box_smoothing = 20.0;
    return style;
}

pr::GameTransferToolCarouselStyle makeCarouselStyle() {
    pr::GameTransferToolCarouselStyle style;
    style.slide_smoothing = 20.0;
    style.slide_span_pixels = 120;
    style.slot_center_left = 40;
    style.slot_center_middle = 120;
    style.slot_center_right = 200;
    return style;
}

void testEnterResetsDefaultState() {
    pr::transfer_system::TransferSystemUiStateController controller;
    controller.configure(0.5, 0.25);
    controller.enter();

    expect(controller.selectedToolIndex() == 1, "enter should default to BASIC tool");
    expect(controller.sliderT() == 0.0, "enter should reset pill slider to Pokemon");
    expect(controller.panelsReveal() == 0.0, "enter should start with panels hidden");
    expect(controller.uiEnter() == 0.0, "enter should start UI off-screen");
    expect(controller.bottomBannerReveal() == 0.0, "enter should start bottom banner hidden");
    expect(!controller.exitInProgress(), "enter should clear exit state");
}

void testTogglePillUpdatesTargetsAndRequestsSfx() {
    pr::transfer_system::TransferSystemUiStateController controller;
    controller.configure(0.5, 0.25);
    controller.enter();

    controller.togglePillTarget();
    expect(controller.consumeButtonSfxRequest(), "toggling pill should request button sfx");

    controller.update(0.5, makePillStyle(), makeCarouselStyle());
    expect(controller.sliderT() > 0.9, "pill toggle should animate toward Items");
    expect(controller.panelsReveal() < 0.1, "pill toggle should hide panels when Items is selected");

    controller.togglePillTarget();
    controller.update(0.5, makePillStyle(), makeCarouselStyle());
    expect(controller.sliderT() < 0.1, "second toggle should animate back toward Pokemon");
    expect(controller.panelsReveal() > 0.9, "second toggle should reveal panels again");
}

void testCarouselCyclingWrapsSelection() {
    pr::transfer_system::TransferSystemUiStateController controller;
    controller.configure(0.5, 0.25);
    controller.enter();

    controller.cycleToolCarousel(1, makeCarouselStyle());
    expect(controller.consumeButtonSfxRequest(), "carousel cycle should request button sfx");
    expect(controller.carouselSlideAnimating(), "carousel cycle should begin slide animation");

    controller.update(1.0, makePillStyle(), makeCarouselStyle());
    expect(!controller.carouselSlideAnimating(), "carousel update should settle the slide");
    expect(controller.selectedToolIndex() == 2, "cycling right should advance tool selection");

    controller.cycleToolCarousel(-1, makeCarouselStyle());
    controller.update(1.0, makePillStyle(), makeCarouselStyle());
    expect(controller.selectedToolIndex() == 1, "cycling left should return tool selection");
}

void testExitRequestsReturnAfterAnimationsComplete() {
    pr::transfer_system::TransferSystemUiStateController controller;
    controller.configure(0.5, 0.20);
    controller.enter();

    controller.togglePillTarget();
    controller.update(0.3, makePillStyle(), makeCarouselStyle());
    controller.startExit();
    controller.update(1.0, makePillStyle(), makeCarouselStyle());

    expect(controller.exitInProgress(), "startExit should keep exit state active during transition");
    expect(controller.consumeReturnToTicketListRequest(), "completed exit should request return to ticket list");
    expect(!controller.consumeReturnToTicketListRequest(), "return request should be one-shot");
}

} // namespace

int main() {
    testEnterResetsDefaultState();
    testTogglePillUpdatesTargetsAndRequestsSfx();
    testCarouselCyclingWrapsSelection();
    testExitRequestsReturnAfterAnimationsComplete();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
