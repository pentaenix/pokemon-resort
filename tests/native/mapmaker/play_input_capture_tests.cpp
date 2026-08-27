#include "mapmaker/interaction/PlayInputCapture.hpp"

#include <iostream>
#include <stdexcept>

int main() {
    try {
        pr::mapmaker::PlayInputCapture capture;
        if (capture.captured()) throw std::runtime_error("capture must start released");

        capture.setPlayVisible(true);
        if (!capture.captured()) throw std::runtime_error("entering Play must capture movement");

        capture.handlePointer(false, true);
        if (capture.captured()) throw std::runtime_error("outside click must release movement");

        capture.handlePointer(true, false);
        if (!capture.captured()) throw std::runtime_error("viewport click must recapture movement");

        capture.handleEscape(true);
        if (capture.captured()) throw std::runtime_error("Escape must release movement");

        capture.setPlayVisible(false);
        capture.setPlayVisible(true);
        if (!capture.captured()) throw std::runtime_error("re-entering Play must recapture movement");
        capture.setPlayVisible(false);
        if (capture.captured()) throw std::runtime_error("leaving Play must release movement");

        std::cout << "play_input_capture_tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "play_input_capture_tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
