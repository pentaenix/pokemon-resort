#pragma once

#include <cstdint>

namespace pr::mapmaker {

// ImGui-compatible packed RGBA colors used by semantic map lenses. Height is
// intentionally normalized to a fixed authored range so the same value never
// changes color from one map to another.
std::uint32_t heightHeatColor(std::uint8_t height, std::uint8_t alpha = 180U);
std::uint32_t collisionLensColor(bool blocked, std::uint8_t alpha = 96U);
std::uint32_t heatMapTextColor(std::uint8_t height);

} // namespace pr::mapmaker
