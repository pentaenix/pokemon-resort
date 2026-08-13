#include "mapmaker/app/FrameMetrics.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        pr::mapmaker::FrameMetrics metrics;
        for (int index = 0; index < 100; ++index) metrics.recordFrame(index == 99 ? 50.0 : 10.0);
        if (std::abs(metrics.fps() - (100000.0 / 1040.0)) > 0.01) {
            throw std::runtime_error("FPS should use the bounded frame history");
        }
        if (std::abs(metrics.frameMillisecondsP95() - 10.0) > 0.01) {
            throw std::runtime_error("p95 should ignore a single p99 spike");
        }
        metrics.recordInputLatency(8.0);
        metrics.recordInputLatency(18.0);
        if (metrics.inputLatencyMilliseconds() <= 8.0 || metrics.inputLatencyMilliseconds() >= 18.0) {
            throw std::runtime_error("input latency should be smoothed");
        }
        std::cout << "frame_metrics_tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "frame_metrics_tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
