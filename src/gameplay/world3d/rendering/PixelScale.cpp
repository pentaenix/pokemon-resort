#include "gameplay/world3d/rendering/PixelScale.hpp"

#include "gameplay/world3d/rendering/WorldBillboardCompositor.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

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

float worldUnitsPerScreenPixel(
    const camera::Gen4FollowCamera::Pose& pose,
    float depth,
    int viewport_h) {
    constexpr float kPi = 3.1415926535f;
    const float fov_y = pose.preset.fov_y_deg * (kPi / 180.0f);
    const float f = 1.0f / std::tan(std::max(0.001f, fov_y * 0.5f));
    return std::max(pose.preset.near_clip, depth) /
        (f * static_cast<float>(std::max(1, viewport_h)) * 0.5f);
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

bool projectDepthBillboardScreenRect(
    const SceneConfig& scene,
    const camera::Gen4FollowCamera& camera,
    const BillboardPlacement& placement,
    const SDL_Rect& source_rect,
    int viewport_w,
    int viewport_h,
    int screen_offset_x_px,
    int& out_x,
    int& out_y,
    int& out_w,
    int& out_h) {
    if (!placement.visible) {
        return false;
    }

    const auto pose = camera.pose();
    const float horizontal_pixel_offset =
        authoredPixelsWorldUnits(scene, static_cast<float>(screen_offset_x_px), 1.0f);
    const camera::Vec3 offset{
        pose.right.x * horizontal_pixel_offset,
        pose.right.y * horizontal_pixel_offset,
        pose.right.z * horizontal_pixel_offset};
    camera::Vec3 bottom_center{
        placement.feet.x + offset.x,
        placement.feet.y + offset.y,
        placement.feet.z + offset.z};
    const float actor_depth_bias =
        authoredPixelsWorldUnits(scene, scene.pixel_compositor.actor_depth_bias_px, 1.0f);
    bottom_center.x += pose.forward.x * -actor_depth_bias;
    bottom_center.y += pose.forward.y * -actor_depth_bias;
    bottom_center.z += pose.forward.z * -actor_depth_bias;
    float bottom_x = 0.0f;
    float bottom_y = 0.0f;
    float bottom_depth = 0.0f;
    if (!camera.worldToScreen(bottom_center, viewport_w, viewport_h, bottom_x, bottom_y, bottom_depth)) {
        return false;
    }
    const QuantizedBillboardRect target = projectWorldBillboardRect(
        scene,
        camera,
        placement,
        source_rect,
        viewport_w,
        viewport_h,
        1,
        screen_offset_x_px);
    if (!target.visible) {
        return false;
    }
    const float world_per_px = worldUnitsPerScreenPixel(pose, bottom_depth, viewport_h);
    const float half_w = static_cast<float>(std::max(1, target.base_w)) * world_per_px * 0.5f;
    const float world_h = static_cast<float>(std::max(1, target.base_h)) * world_per_px;
    const camera::Vec3 right{pose.right.x * half_w, pose.right.y * half_w, pose.right.z * half_w};
    const camera::Vec3 up{pose.up.x * world_h, pose.up.y * world_h, pose.up.z * world_h};

    const camera::Vec3 points[4] = {
        {bottom_center.x - right.x + up.x,
         bottom_center.y - right.y + up.y,
         bottom_center.z - right.z + up.z},
        {bottom_center.x + right.x + up.x,
         bottom_center.y + right.y + up.y,
         bottom_center.z + right.z + up.z},
        {bottom_center.x + right.x,
         bottom_center.y + right.y,
         bottom_center.z + right.z},
        {bottom_center.x - right.x,
         bottom_center.y - right.y,
         bottom_center.z - right.z},
    };

    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_y = std::numeric_limits<float>::lowest();
    for (const camera::Vec3& point : points) {
        float sx = 0.0f;
        float sy = 0.0f;
        float depth = 0.0f;
        if (!camera.worldToScreen(point, viewport_w, viewport_h, sx, sy, depth)) {
            return false;
        }
        min_x = std::min(min_x, sx);
        min_y = std::min(min_y, sy);
        max_x = std::max(max_x, sx);
        max_y = std::max(max_y, sy);
    }

    out_x = static_cast<int>(std::round(min_x));
    out_y = static_cast<int>(std::round(min_y));
    out_w = std::max(1, static_cast<int>(std::round(max_x - min_x)));
    out_h = std::max(1, static_cast<int>(std::round(max_y - min_y)));
    return true;
}

} // namespace pr::gameplay::world3d::rendering
