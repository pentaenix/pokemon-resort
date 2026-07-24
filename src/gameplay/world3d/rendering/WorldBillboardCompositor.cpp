#include "gameplay/world3d/rendering/WorldBillboardCompositor.hpp"

#include "gameplay/world3d/camera/Gen4CameraPreset.hpp"

#include <algorithm>
#include <cmath>

namespace pr::gameplay::world3d::rendering {

namespace {

float nominalCameraDistance(const SceneConfig& scene) {
    if (scene.camera_distance > 0.0f) {
        return scene.camera_distance;
    }
    const camera::Gen4CameraPreset preset = camera::loadGen4PresetById(scene.camera_preset.c_str());
    return std::max(1.0f, preset.distance);
}

} // namespace

QuantizedBillboardRect projectWorldBillboardRect(
    const SceneConfig& scene,
    const camera::Gen4FollowCamera& camera,
    const BillboardPlacement& placement,
    const SDL_Rect& source_rect,
    int base_viewport_w,
    int base_viewport_h,
    int internal_scale,
    int screen_offset_x_px) {
    QuantizedBillboardRect out{};
    if (!placement.visible) {
        return out;
    }

    const int base_w = std::max(1, base_viewport_w);
    const int base_h = std::max(1, base_viewport_h);
    const int scale = std::max(1, internal_scale);

    float feet_x = 0.0f;
    float feet_y = 0.0f;
    float depth = 0.0f;
    if (!camera.worldToScreen(placement.feet, base_w, base_h, feet_x, feet_y, depth)) {
        return out;
    }

    const float source_aspect =
        static_cast<float>(std::max(1, source_rect.w)) /
        static_cast<float>(std::max(1, source_rect.h));
    const float depth_scale = nominalCameraDistance(scene) / std::max(0.001f, depth);
    const int sprite_h = std::max(1, static_cast<int>(std::round(placement.world_h * depth_scale)));
    const int sprite_w =
        std::max(1, static_cast<int>(std::round(static_cast<float>(sprite_h) * source_aspect)));
    const int snapped_feet_x = static_cast<int>(std::round(feet_x));
    const int snapped_feet_y = static_cast<int>(std::round(feet_y));
    const float anchor_x = scene.pixel_compositor.snap_anchors ? static_cast<float>(snapped_feet_x) : feet_x;
    const float anchor_y = scene.pixel_compositor.snap_anchors ? static_cast<float>(snapped_feet_y) : feet_y;
    const int foot_x = static_cast<int>(std::round(anchor_x + static_cast<float>(screen_offset_x_px)));
    const int foot_y = static_cast<int>(std::round(anchor_y));

    out.base_w = sprite_w;
    out.base_h = sprite_h;
    out.base_x = foot_x - (sprite_w / 2);
    out.base_y = foot_y - sprite_h;
    out.internal_x = out.base_x * scale;
    out.internal_y = out.base_y * scale;
    out.internal_w = out.base_w * scale;
    out.internal_h = out.base_h * scale;
    out.depth = depth;
    out.visible = true;
    return out;
}

QuantizedShadowRect projectWorldShadowRect(
    const SceneConfig& scene,
    const camera::Gen4FollowCamera& camera,
    const BillboardPlacement& placement,
    const SDL_Rect& source_rect,
    const SpriteShadowConfig& shadow,
    int base_viewport_w,
    int base_viewport_h,
    int internal_scale) {
    QuantizedShadowRect out{};
    if (!shadow.enabled || !placement.visible) {
        return out;
    }

    const QuantizedBillboardRect sprite = projectWorldBillboardRect(
        scene,
        camera,
        placement,
        source_rect,
        base_viewport_w,
        base_viewport_h,
        internal_scale,
        0);
    if (!sprite.visible) {
        return out;
    }

    float shadow_x = 0.0f;
    float shadow_y = 0.0f;
    float shadow_depth = 0.0f;
    const int base_w = std::max(1, base_viewport_w);
    const int base_h = std::max(1, base_viewport_h);
    if (!camera.worldToScreen(placement.shadow_ground, base_w, base_h, shadow_x, shadow_y, shadow_depth)) {
        shadow_depth = sprite.depth;
    }

    const float sprite_scale =
        static_cast<float>(sprite.base_h) / static_cast<float>(std::max(1, source_rect.h));
    const int shadow_w = std::max(
        2,
        static_cast<int>(std::round(static_cast<float>(shadow.texture_width_px) * sprite_scale)));
    const int shadow_h = std::max(
        2,
        static_cast<int>(std::round(static_cast<float>(shadow.texture_height_px) * sprite_scale)));
    const int scale = std::max(1, internal_scale);
    const int foot_x = sprite.base_x + (sprite.base_w / 2) + shadow.screen_offset_x_px;
    const int foot_y = sprite.base_y + sprite.base_h;
    const int shadow_bottom = foot_y + shadow.feet_to_shadow_bottom_px + shadow.screen_offset_y_px;

    out.base_w = shadow_w;
    out.base_h = shadow_h;
    out.base_x = foot_x - ((shadow_w + 1) / 2);
    out.base_y = shadow_bottom - shadow_h;
    out.internal_x = out.base_x * scale;
    out.internal_y = out.base_y * scale;
    out.internal_w = out.base_w * scale;
    out.internal_h = out.base_h * scale;
    out.depth = shadow_depth;
    out.visible = true;
    return out;
}

} // namespace pr::gameplay::world3d::rendering
