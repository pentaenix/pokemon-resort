#pragma once

#include <algorithm>

namespace pr {

inline double clampSimulationDeltaSeconds(double wall_clock_seconds) {
    constexpr double kMaximumSimulationDeltaSeconds = 0.1;
    return std::clamp(wall_clock_seconds, 0.0, kMaximumSimulationDeltaSeconds);
}

} // namespace pr
