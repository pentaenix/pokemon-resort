#include "aquarium_geometry/Kernel.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace pr::aquarium::geometry;

namespace {

bool benchmark(const char* label, const AquariumBuildRequest& request) {
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
        if (!result.validation.valid()) return false;
        milliseconds.push_back(std::chrono::duration<double, std::milli>(stop - start).count());
    }
    std::sort(milliseconds.begin(), milliseconds.end());
    const auto percentile = [&milliseconds](double fraction) {
        const std::size_t index = static_cast<std::size_t>(fraction * static_cast<double>(milliseconds.size() - 1));
        return milliseconds[index];
    };
    std::cout << std::fixed << std::setprecision(4)
              << "aquarium_geometry_benchmark fixture=" << label
              << " samples=" << kSamples
              << " p50_ms=" << percentile(0.50)
              << " p95_ms=" << percentile(0.95)
              << " p99_ms=" << percentile(0.99)
              << " max_ms=" << milliseconds.back() << '\n';
    return true;
}

} // namespace

int main() {
    AquariumBuildRequest rectangle;
    rectangle.tank.id = "tank_benchmark_rectangle";
    rectangle.tank.footprint.origin_cell = {7, 6};
    rectangle.tank.footprint.width_cells = 6;
    rectangle.tank.footprint.depth_cells = 4;
    rectangle.tank.height_steps = 8;

    AquariumBuildRequest rounded_u;
    rounded_u.tank.id = "tank_benchmark_rounded_u";
    rounded_u.tank.footprint.shape = FootprintShape::U;
    rounded_u.tank.footprint.width_cells = 9;
    rounded_u.tank.footprint.depth_cells = 7;
    rounded_u.tank.footprint.notch_width_cells = 3;
    rounded_u.tank.footprint.notch_depth_cells = 4;
    rounded_u.tank.height_steps = 12;
    rounded_u.tank.corner_radius_steps = 4;
    return benchmark("rectangle", rectangle) && benchmark("rounded-u", rounded_u) ? 0 : 1;
}
