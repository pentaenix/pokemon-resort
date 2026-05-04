#include "core/app/transition/AppTransitionController.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

void testSuccessfulSaveTransitionBlocksInputAndRequestsLoading() {
    pr::AppTransitionController controller;
    controller.startSuccessfulSaveQuickTransition();

    expect(controller.active(), "transition should be active after start");
    expect(controller.blocksInput(), "transition should block input while active");

    auto step = controller.update(0.5, false);
    expect(!step.begin_loading, "delay step should not begin loading yet");
    step = controller.update(0.25, false);
    expect(step.begin_loading, "fade-out completion should request loading screen entry");
    expect(controller.overlayAlpha() == 1.0, "loading entry should happen under full overlay");
}

void testSuccessfulSaveTransitionFinishesDestinationAfterLoadingCompletes() {
    pr::AppTransitionController controller;
    controller.startSuccessfulSaveQuickTransition();

    controller.update(0.5, false);
    controller.update(0.25, false);
    controller.update(0.25, false);

    auto step = controller.update(0.1, false);
    expect(!step.finish_destination, "loading-active phase should wait for loading completion");

    step = controller.update(0.1, true);
    expect(!step.finish_destination, "loading completion should start fade-out before destination switch");
    step = controller.update(0.25, true);
    expect(step.finish_destination, "fade-out completion should request destination switch");

    controller.update(0.25, true);
    expect(!controller.active(), "transition should become inactive after destination fade-in");
    expect(controller.overlayAlpha() == 0.0, "inactive transition should leave overlay transparent");
}

} // namespace

int main() {
    try {
        testSuccessfulSaveTransitionBlocksInputAndRequestsLoading();
        testSuccessfulSaveTransitionFinishesDestinationAfterLoadingCompletes();
        std::cout << "app_transition_controller_tests: OK\n";
        return EXIT_SUCCESS;
    } catch (const TestFailure& ex) {
        std::cerr << "app_transition_controller_tests: FAILED: " << ex.what() << '\n';
        return EXIT_FAILURE;
    } catch (const std::exception& ex) {
        std::cerr << "app_transition_controller_tests: ERROR: " << ex.what() << '\n';
        return 2;
    }
}
