#include "gameplay/world3d/rendering/PixelScale.hpp"

#include <algorithm>
#include <cmath>

namespace pr::gameplay::world3d::rendering {

namespace {

camera::Vec3 horizontalCameraRight(const camera::Gen4FollowCamera& camera) {
    const auto pose = camera.pose();
    camera::Vec3 right{pose.right.x, 0.0f, pose.right.z};
    const float len = std::sqrt((right.x * right.x) + (right.z * right.z));
    if (len <= 0.0001f) {
        return camera::Vec3{1.0f, 0.0f, 0.0f};
    }
    return camera::Vec3{right.x / len, 0.0f, right.z / len};
}

} // namespace

float worldUnitsPerPixel(const SceneConfig& scene) {
    if (scene.pixel_scale.world_units_per_pixel > 0.0f) {
        return scene.pixel_scale.world_units_per_pixel;
    }
    const int map_pixels_per_tile = std::max(1, scene.pixel_scale.map_pixels_per_tile);
    return std::max(0.001f, scene.grid.tile_size) / static_cast<float>(map_pixels_per_tile);
}

float authoredPixelsWorldUnits(const SceneConfig& scene, float pixels, float scale_multiplier) {
    const float authored_pixels = std::max(1.0f, pixels);
    return authored_pixels * worldUnitsPerPixel(scene) * std::max(0.1f, scale_multiplier);
}

float authoredSpriteWorldHeight(const SceneConfig& scene, float frame_height_px, float scale_multiplier) {
    return authoredPixelsWorldUnits(scene, frame_height_px, scale_multiplier);
}

float clampedPixelZoom(const PixelScaleConfig& config) {
    const float min_zoom = std::max(0.01f, config.zoom_min);
    const float max_zoom = std::max(min_zoom, config.zoom_max);
    return std::clamp(config.zoom, min_zoom, max_zoom);
}

void applyPixelScaleToCameraPreset(camera::Gen4CameraPreset& preset, const PixelScaleConfig& config) {
    const float zoom = clampedPixelZoom(config);
    if (preset.distance > 0.0f && zoom > 0.0f) {
        preset.distance /= zoom;
    }
}

void applySceneCameraScaleToCameraPreset(camera::Gen4CameraPreset& preset, const SceneConfig& scene) {
    const float min_scale = std::max(0.01f, scene.scene_camera.distance_scale_min);
    const float max_scale = std::max(min_scale, scene.scene_camera.distance_scale_max);
    const float distance_scale = std::clamp(scene.scene_camera.distance_scale, min_scale, max_scale);
    if (preset.distance > 0.0f && distance_scale > 0.0f) {
        preset.distance /= distance_scale;
    }
}

int worldViewportBaseWidth(const SceneConfig& scene) {
    return std::clamp(scene.world_viewport.base_width, 160, 1920);
}

int worldViewportBaseHeight(const SceneConfig& scene) {
    return std::clamp(scene.world_viewport.base_height, 120, 1080);
}

int worldViewportInternalScale(const SceneConfig& scene) {
    return std::clamp(scene.world_viewport.internal_scale, 1, 4);
}

int worldViewportRenderWidth(const SceneConfig& scene) {
    return worldViewportBaseWidth(scene) * worldViewportInternalScale(scene);
}

int worldViewportRenderHeight(const SceneConfig& scene) {
    return worldViewportBaseHeight(scene) * worldViewportInternalScale(scene);
}

bool projectBillboardScreenRect(
    const camera::Gen4FollowCamera& camera,
    const BillboardPlacement& placement,
    int viewport_w,
    int viewport_h,
    int& out_x,
    int& out_y,
    int& out_w,
    int& out_h) {
    if (!placement.visible) {
        return false;
    }

    float feet_x = 0.0f;
    float feet_y = 0.0f;
    float feet_depth = 0.0f;
    if (!camera.worldToScreen(placement.feet, viewport_w, viewport_h, feet_x, feet_y, feet_depth)) {
        return false;
    }

    const camera::Vec3 top_world{
        placement.feet.x,
        placement.feet.y + placement.world_h,
        placement.feet.z};
    float top_x = 0.0f;
    float top_y = 0.0f;
    float top_depth = 0.0f;
    if (!camera.worldToScreen(top_world, viewport_w, viewport_h, top_x, top_y, top_depth)) {
        return false;
    }

    out_h = std::max(1, static_cast<int>(std::round(std::abs(top_y - feet_y))));

    const camera::Vec3 flat_right = horizontalCameraRight(camera);
    const float half_w = placement.world_w * 0.5f;
    const camera::Vec3 left_world{
        placement.feet.x - (flat_right.x * half_w),
        placement.feet.y,
        placement.feet.z - (flat_right.z * half_w)};
    const camera::Vec3 right_world{
        placement.feet.x + (flat_right.x * half_w),
        placement.feet.y,
        placement.feet.z + (flat_right.z * half_w)};

    float left_x = 0.0f;
    float left_y = 0.0f;
    float left_depth = 0.0f;
    float right_x = 0.0f;
    float right_y = 0.0f;
    float right_depth = 0.0f;
    if (!camera.worldToScreen(left_world, viewport_w, viewport_h, left_x, left_y, left_depth) ||
        !camera.worldToScreen(right_world, viewport_w, viewport_h, right_x, right_y, right_depth)) {
        return false;
    }

    out_w = std::max(1, static_cast<int>(std::round(std::hypot(right_x - left_x, right_y - left_y))));
    out_x = static_cast<int>(std::round(feet_x)) - (out_w / 2);
    out_y = static_cast<int>(std::round(feet_y)) - out_h;
    return true;
}

} // namespace pr::gameplay::world3d::rendering
