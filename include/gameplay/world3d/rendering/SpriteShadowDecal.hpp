#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"

#include <cstdint>
#include <vector>

namespace pr::gameplay::world3d::rendering {

// Builds premultiplied-ready RGBA8888 pixels for a Gen4-style ground shadow mask.
std::vector<std::uint8_t> buildSpriteShadowRgba(
    const SpriteShadowConfig& shadow_config,
    int& out_width,
    int& out_height);

} // namespace pr::gameplay::world3d::rendering
