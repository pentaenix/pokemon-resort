#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"

#include <cstdint>

namespace pr::gameplay::world3d::rendering::bgfx_backend::ramp_terrain_render {

std::uint32_t packTerrainColor(const TerrainColor& color);
std::uint32_t rampProgressColor(
    std::uint32_t base_color,
    float progress,
    float low_shade,
    float high_shade,
    float band_count,
    float band_strength,
    float band_softness);

} // namespace pr::gameplay::world3d::rendering::bgfx_backend::ramp_terrain_render
