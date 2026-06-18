#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"
#include "gameplay/world3d/rendering/BillboardPlacement.hpp"

#include <SDL.h>

namespace pr::gameplay::world3d::rendering {

struct QuantizedBillboardRect {
    int base_x = 0;
    int base_y = 0;
    int base_w = 0;
    int base_h = 0;
    int internal_x = 0;
    int internal_y = 0;
    int internal_w = 0;
    int internal_h = 0;
    float depth = 0.0f;
    bool visible = false;
};

struct QuantizedShadowRect {
    int base_x = 0;
    int base_y = 0;
    int base_w = 0;
    int base_h = 0;
    int internal_x = 0;
    int internal_y = 0;
    int internal_w = 0;
    int internal_h = 0;
    float depth = 0.0f;
    bool visible = false;
};

QuantizedBillboardRect projectWorldBillboardRect(
    const SceneConfig& scene,
    const camera::Gen4FollowCamera& camera,
    const BillboardPlacement& placement,
    const SDL_Rect& source_rect,
    int base_viewport_w,
    int base_viewport_h,
    int internal_scale,
    int screen_offset_x_px = 0);

QuantizedShadowRect projectWorldShadowRect(
    const SceneConfig& scene,
    const camera::Gen4FollowCamera& camera,
    const BillboardPlacement& placement,
    const SDL_Rect& source_rect,
    const SpriteShadowConfig& shadow,
    int base_viewport_w,
    int base_viewport_h,
    int internal_scale);

} // namespace pr::gameplay::world3d::rendering
