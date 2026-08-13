#include "core/app/frame/AppFrameRequests.hpp"
#include "core/app/frame/FrameTiming.hpp"

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

void testSfxRequestsAccumulateAndConsumeOnce() {
    pr::AppFrameRequests requests;
    requests.requestButtonSfx();
    requests.requestRipSfxIf(false);
    requests.requestUiMoveSfxIf(true);
    requests.requestSaveSfx();

    pr::AppSfxRequests first = requests.consumeSfxRequests();
    expect(first.button, "button sfx should be requested");
    expect(!first.rip, "false conditional sfx should not be requested");
    expect(first.ui_move, "ui move sfx should be requested");
    expect(first.save, "save sfx should be requested");

    pr::AppSfxRequests second = requests.consumeSfxRequests();
    expect(!second.button && !second.ui_move && !second.save,
           "sfx requests should be one-shot after consumption");
}

void testUserSettingsSaveRequestConsumesOnce() {
    pr::AppFrameRequests requests;
    expect(!requests.consumeUserSettingsSaveRequest(),
           "settings save should start unrequested");

    requests.requestUserSettingsSave();
    expect(requests.consumeUserSettingsSaveRequest(),
           "settings save should be returned after request");
    expect(!requests.consumeUserSettingsSaveRequest(),
           "settings save should be one-shot after consumption");
}

void testSimulationDeltaDoesNotConsumeResourceStalls() {
    expect(pr::clampSimulationDeltaSeconds(0.016) == 0.016,
           "normal frame deltas should pass through unchanged");
    expect(pr::clampSimulationDeltaSeconds(2.0) == 0.1,
           "resource stalls should be capped before transition simulation");
    expect(pr::clampSimulationDeltaSeconds(-1.0) == 0.0,
           "negative frame deltas should be rejected");
}

} // namespace

int main() {
    try {
        testSfxRequestsAccumulateAndConsumeOnce();
        testUserSettingsSaveRequestConsumesOnce();
        testSimulationDeltaDoesNotConsumeResourceStalls();
        std::cout << "app_frame_requests_tests: OK\n";
        return EXIT_SUCCESS;
    } catch (const TestFailure& ex) {
        std::cerr << "app_frame_requests_tests: FAILED: " << ex.what() << '\n';
        return EXIT_FAILURE;
    } catch (const std::exception& ex) {
        std::cerr << "app_frame_requests_tests: ERROR: " << ex.what() << '\n';
        return 2;
    }
}
