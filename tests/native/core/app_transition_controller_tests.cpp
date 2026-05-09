#include "core/app/transition/AppTransitionController.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct TestFailure : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

void testSuccessfulSaveTransitionBlocksInputAndRequestsLoadingQuickly() {
    pr::AppTransitionController controller;
    controller.startSuccessfulSaveQuickTransition();

    expect(controller.active(), "transition should be active after start");
    expect(controller.blocksInput(), "transition should block input while active");

    pr::AppTransitionController::StepResult step{};
    int n = 0;
    while (!step.begin_loading && n < 30) {
        step = controller.update(0.05, false);
        ++n;
    }
    expect(step.begin_loading, "zero-delay transition should request loading within a few updates");
}

void testSuccessfulSaveTransitionFinishesDestinationAfterLoadingCompletes() {
    pr::AppTransitionController controller;
    controller.startSuccessfulSaveQuickTransition();

    pr::AppTransitionController::StepResult step{};
    while (!step.begin_loading) {
        step = controller.update(0.05, false);
    }
    controller.update(0.05, false);
    controller.update(0.05, false);

    step = controller.update(0.05, false);
    expect(!step.finish_destination, "loading-active phase should wait for loading completion");

    step = controller.update(0.05, true);
    expect(!step.finish_destination, "loading completion should start fade-out before destination switch");
    int m = 0;
    while (!step.finish_destination && m < 30) {
        step = controller.update(0.05, true);
        ++m;
    }
    expect(step.finish_destination, "fade-out completion should request destination switch");

    controller.update(0.05, true);
    expect(!controller.active(), "transition should become inactive after destination fade-in");
    expect(controller.overlayAlpha() == 0.0, "inactive transition should leave overlay transparent");
}

} // namespace

int main() {
    try {
        testSuccessfulSaveTransitionBlocksInputAndRequestsLoadingQuickly();
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
