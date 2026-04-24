#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace pr::title_screen {

constexpr int kSectionTitleY = 125;
constexpr int kSectionBackButtonY = 708;
inline double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }
inline double lerp(double a, double b, double t) { return a + (b - a) * t; }
inline double easeOutCubic(double t) { t = clamp01(t); double inv = 1.0 - t; return 1.0 - inv * inv * inv; }
inline double easeInOutQuad(double t) { t = clamp01(t); return t < 0.5 ? 2.0 * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 2.0) / 2.0; }
inline double easeOutBack(double t, double overshoot) {
    t = clamp01(t);
    const double c1 = std::max(0.0, overshoot);
    const double c3 = c1 + 1.0;
    const double shifted = t - 1.0;
    return 1.0 + c3 * shifted * shifted * shifted + c1 * shifted * shifted;
}

inline unsigned char triangleBlinkAlpha(double elapsed, double cycle) {
    if (cycle <= 0.0) {
        return 255;
    }
    const double t = std::fmod(elapsed, cycle) / cycle;
    const double wave = 1.0 - std::fabs(2.0 * t - 1.0);
    return static_cast<unsigned char>(std::round(lerp(0.0, 255.0, wave)));
}

inline std::uint32_t packRGBA(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    return (static_cast<std::uint32_t>(r) << 24) |
           (static_cast<std::uint32_t>(g) << 16) |
           (static_cast<std::uint32_t>(b) << 8) |
           static_cast<std::uint32_t>(a);
}

inline int wrapIndex(int value, int size) {
    if (size <= 0) {
        return 0;
    }
    value %= size;
    return value < 0 ? value + size : value;
}

inline int clampVolume(int value) {
    return std::max(0, std::min(10, value));
}

} // namespace pr::title_screen
