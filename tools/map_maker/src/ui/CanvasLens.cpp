#include "mapmaker/ui/CanvasLens.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace pr::mapmaker {
namespace {

struct Stop { float position; int red; int green; int blue; };

constexpr std::array<Stop, 5> kHeightStops{{
    {0.00F, 48, 91, 210},
    {0.25F, 35, 184, 210},
    {0.50F, 83, 194, 112},
    {0.75F, 242, 181, 58},
    {1.00F, 224, 67, 58},
}};

std::uint32_t pack(int red, int green, int blue, std::uint8_t alpha) {
    return static_cast<std::uint32_t>(std::clamp(red, 0, 255)) |
        (static_cast<std::uint32_t>(std::clamp(green, 0, 255)) << 8U) |
        (static_cast<std::uint32_t>(std::clamp(blue, 0, 255)) << 16U) |
        (static_cast<std::uint32_t>(alpha) << 24U);
}

} // namespace

std::uint32_t heightHeatColor(std::uint8_t height, std::uint8_t alpha) {
    const float normalized = std::min(31.0F, static_cast<float>(height)) / 31.0F;
    std::size_t upper = 1;
    while (upper + 1 < kHeightStops.size() && normalized > kHeightStops[upper].position) ++upper;
    const Stop& left = kHeightStops[upper - 1];
    const Stop& right = kHeightStops[upper];
    const float distance = std::max(0.0001F, right.position - left.position);
    const float amount = std::clamp((normalized - left.position) / distance, 0.0F, 1.0F);
    const auto blend = [&](int a, int b) {
        return static_cast<int>(std::lround(a + (b - a) * amount));
    };
    return pack(blend(left.red, right.red), blend(left.green, right.green),
        blend(left.blue, right.blue), alpha);
}

std::uint32_t collisionLensColor(bool blocked, std::uint8_t alpha) {
    return blocked ? pack(232, 67, 75, alpha) : pack(54, 177, 121, alpha);
}

std::uint32_t heatMapTextColor(std::uint8_t height) {
    return height >= 20U ? pack(28, 24, 20, 255U) : pack(245, 249, 255, 255U);
}

} // namespace pr::mapmaker
