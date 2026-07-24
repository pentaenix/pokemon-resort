#include "gameplay/world3d/rendering/SpriteShadowDecal.hpp"

#include <algorithm>
#include <cmath>

namespace pr::gameplay::world3d::rendering {

namespace {

bool isMaskValid(const std::vector<std::string>& mask, int width, int height) {
    if (static_cast<int>(mask.size()) != height) {
        return false;
    }
    for (const std::string& row : mask) {
        if (static_cast<int>(row.size()) != width) {
            return false;
        }
    }
    return true;
}

SpriteShadowConfig defaultGen4ShadowConfig() {
    SpriteShadowConfig cfg{};
    cfg.texture_width_px = 32;
    cfg.texture_height_px = 16;
    cfg.opacity = 0.55f;
    cfg.color_r = 0;
    cfg.color_g = 0;
    cfg.color_b = 0;
    cfg.color_a = 255;
    return cfg;
}

} // namespace

std::vector<std::uint8_t> buildSpriteShadowRgba(
    const SpriteShadowConfig& shadow_config,
    int& out_width,
    int& out_height) {
    const int sw = std::max(8, shadow_config.texture_width_px);
    const int sh = std::max(8, shadow_config.texture_height_px);
    out_width = sw;
    out_height = sh;

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(sw * sh * 4), 0);
    const std::uint8_t peak_alpha = static_cast<std::uint8_t>(
        std::clamp(shadow_config.opacity, 0.0f, 1.0f) * static_cast<float>(shadow_config.color_a));

    const SpriteShadowConfig default_cfg = defaultGen4ShadowConfig();
    const std::vector<std::string>& mask = isMaskValid(shadow_config.mask_rows, sw, sh)
        ? shadow_config.mask_rows
        : default_cfg.mask_rows;

    if (isMaskValid(mask, sw, sh)) {
        for (int y = 0; y < sh; ++y) {
            for (int x = 0; x < sw; ++x) {
                const bool on = mask[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] == 'X';
                const std::uint8_t a = on ? peak_alpha : 0;
                const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(sw) + static_cast<std::size_t>(x)) * 4;
                pixels[i + 0] = shadow_config.color_r;
                pixels[i + 1] = shadow_config.color_g;
                pixels[i + 2] = shadow_config.color_b;
                pixels[i + 3] = a;
            }
        }
        return pixels;
    }

    const float cx = static_cast<float>(sw) * 0.5f;
    const float cy = static_cast<float>(sh) * 0.5f;
    const float rx = std::max(1.0f, static_cast<float>(sw) * 0.46f);
    const float ry = std::max(1.0f, static_cast<float>(sh) * 0.42f);
    for (int y = 0; y < sh; ++y) {
        for (int x = 0; x < sw; ++x) {
            const float dx = (static_cast<float>(x) + 0.5f - cx) / rx;
            const float dy = (static_cast<float>(y) + 0.5f - cy) / ry;
            const float d2 = dx * dx + dy * dy;
            const std::uint8_t a = (d2 <= 1.0f) ? peak_alpha : 0;
            const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(sw) + static_cast<std::size_t>(x)) * 4;
            pixels[i + 0] = shadow_config.color_r;
            pixels[i + 1] = shadow_config.color_g;
            pixels[i + 2] = shadow_config.color_b;
            pixels[i + 3] = a;
        }
    }
    return pixels;
}

} // namespace pr::gameplay::world3d::rendering
