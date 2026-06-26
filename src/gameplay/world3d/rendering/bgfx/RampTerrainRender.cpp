#include "gameplay/world3d/rendering/bgfx/RampTerrainRender.hpp"

#include <algorithm>

namespace pr::gameplay::world3d::rendering::bgfx_backend::ramp_terrain_render {

namespace {

std::uint32_t packAbgr(float r, float g, float b, float a = 1.0f) {
    const auto c = [](float v) -> std::uint32_t {
        return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return (c(a) << 24U) | (c(b) << 16U) | (c(g) << 8U) | c(r);
}

float softBand(float x, float softness) {
    x = std::abs(x - std::floor(x + 0.5f));
    const float edge0 = softness;
    const float edge1 = 0.02f;
    const float t = std::clamp((x - edge1) / (edge0 - edge1), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

std::uint32_t rampHintPackedAbgr(
    std::uint32_t color,
    float progress,
    float low_shade,
    float high_shade,
    float band_count,
    float band_strength,
    float band_softness) {
    progress = std::clamp(progress, 0.0f, 1.0f);
    low_shade = std::clamp(low_shade, 0.0f, 4.0f);
    high_shade = std::clamp(high_shade, 0.0f, 4.0f);
    band_count = std::clamp(band_count, 0.0f, 64.0f);
    band_strength = std::clamp(band_strength, 0.0f, 1.0f);
    band_softness = std::clamp(band_softness, 0.03f, 0.49f);

    const float gradient = low_shade + (progress * (high_shade - low_shade));
    const float band = band_count <= 0.001f
        ? 1.0f
        : 1.0f - (softBand(progress * band_count, band_softness) * band_strength);
    const float shade = gradient * band;

    const float r = static_cast<float>(color & 0xffU) / 255.0f;
    const float g = static_cast<float>((color >> 8U) & 0xffU) / 255.0f;
    const float b = static_cast<float>((color >> 16U) & 0xffU) / 255.0f;
    const float a = static_cast<float>((color >> 24U) & 0xffU) / 255.0f;
    return packAbgr(r * shade, g * shade, b * shade, a);
}

} // namespace

std::uint32_t packTerrainColor(const TerrainColor& color) {
    return packAbgr(
        static_cast<float>(color.r) / 255.0f,
        static_cast<float>(color.g) / 255.0f,
        static_cast<float>(color.b) / 255.0f,
        static_cast<float>(color.a) / 255.0f);
}

std::uint32_t rampProgressColor(
    std::uint32_t base_color,
    float progress,
    float low_shade,
    float high_shade,
    float band_count,
    float band_strength,
    float band_softness) {
    return rampHintPackedAbgr(
        base_color,
        progress,
        low_shade,
        high_shade,
        band_count,
        band_strength,
        band_softness);
}

} // namespace pr::gameplay::world3d::rendering::bgfx_backend::ramp_terrain_render
