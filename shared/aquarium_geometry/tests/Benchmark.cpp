#include "aquarium_geometry/Kernel.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace pr::aquarium::geometry;

namespace {

bool benchmark(const char* label, const std::vector<AquariumBuildRequest>& requests) {
    constexpr std::size_t kWarmups = 100;
    constexpr std::size_t kSamples = 2000;
    for (std::size_t i = 0; i < kWarmups; ++i) {
        for (const AquariumBuildRequest& request : requests) (void)buildAquarium(request);
    }

    std::vector<double> milliseconds;
    milliseconds.reserve(kSamples);
    for (std::size_t i = 0; i < kSamples; ++i) {
        const auto start = std::chrono::steady_clock::now();
        bool valid = true;
        for (const AquariumBuildRequest& request : requests) {
            valid = valid && buildAquarium(request).validation.valid();
        }
        const auto stop = std::chrono::steady_clock::now();
        if (!valid) return false;
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
              << " tanks_per_sample=" << requests.size()
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

    AquariumBuildRequest tunnel_network;
    tunnel_network.tank.id = "tank_benchmark_tunnel_network";
    tunnel_network.tank.footprint.origin_cell = {10, 10};
    tunnel_network.tank.footprint.width_cells = 8;
    tunnel_network.tank.footprint.depth_cells = 5;
    tunnel_network.tank.height_steps = 12;
    tunnel_network.tank.depth_steps = 4;
    TunnelDesign trunk;
    trunk.id = "tunnel_trunk";
    for (int column = 10; column <= 18; ++column) {
        trunk.centreline_cells.push_back({column, 12});
    }
    TunnelDesign north_branch;
    north_branch.id = "tunnel_north";
    for (int row = 10; row <= 12; ++row) {
        north_branch.centreline_cells.push_back({14, row});
    }
    TunnelDesign south_branch;
    south_branch.id = "tunnel_south";
    for (int row = 15; row >= 12; --row) {
        south_branch.centreline_cells.push_back({14, row});
    }
    tunnel_network.tank.tunnels = {
        std::move(trunk), std::move(north_branch), std::move(south_branch)};

    std::vector<AquariumBuildRequest> eight_tanks(8, rounded_u);
    for (std::size_t index = 0; index < eight_tanks.size(); ++index) {
        eight_tanks[index].tank.id = "tank_benchmark_batch_" + std::to_string(index);
        eight_tanks[index].tank.footprint.origin_cell.column = static_cast<std::int32_t>(index * 10);
    }
    return benchmark("rectangle", {rectangle}) &&
        benchmark("rounded-u", {rounded_u}) &&
        benchmark("deep-four-exit-tunnel", {tunnel_network}) &&
        benchmark("eight-rounded-u", eight_tanks) ? 0 : 1;
}
