#include "aquarium_geometry/Kernel.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace pr::aquarium::geometry;

int main() {
    AquariumBuildRequest request;
    request.tank.id = "tank_benchmark_rectangle";
    request.tank.footprint.origin_cell = {7, 6};
    request.tank.footprint.width_cells = 6;
    request.tank.footprint.depth_cells = 4;
    request.tank.height_steps = 8;

    constexpr std::size_t kWarmups = 100;
    constexpr std::size_t kSamples = 2000;
    for (std::size_t i = 0; i < kWarmups; ++i) {
        (void)buildAquarium(request);
    }

    std::vector<double> milliseconds;
    milliseconds.reserve(kSamples);
    for (std::size_t i = 0; i < kSamples; ++i) {
        const auto start = std::chrono::steady_clock::now();
        const AquariumBuildResult result = buildAquarium(request);
        const auto stop = std::chrono::steady_clock::now();
        if (!result.validation.valid()) return 1;
        milliseconds.push_back(std::chrono::duration<double, std::milli>(stop - start).count());
    }
    std::sort(milliseconds.begin(), milliseconds.end());
    const auto percentile = [&milliseconds](double fraction) {
        const std::size_t index = static_cast<std::size_t>(fraction * static_cast<double>(milliseconds.size() - 1));
        return milliseconds[index];
    };
    std::cout << std::fixed << std::setprecision(4)
              << "aquarium_geometry_benchmark samples=" << kSamples
              << " p50_ms=" << percentile(0.50)
              << " p95_ms=" << percentile(0.95)
              << " p99_ms=" << percentile(0.99)
              << " max_ms=" << milliseconds.back() << '\n';
    return 0;
}
