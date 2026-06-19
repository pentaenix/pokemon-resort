#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/camera/Gen4CameraPreset.hpp"
#include "gameplay/world3d/rendering/BillboardPlacement.hpp"

namespace pr::gameplay::world3d::rendering {

// Shared pixel-art ruler: map tiles and sprite billboards use the same world-units-per-pixel.
float worldUnitsPerPixel(const SceneConfig& scene);

float authoredPixelsWorldUnits(const SceneConfig& scene, float pixels, float scale_multiplier = 1.0f);

float authoredSpriteWorldHeight(const SceneConfig& scene, float frame_height_px, float scale_multiplier = 1.0f);

float clampedPixelZoom(const PixelScaleConfig& config);

void applyPixelScaleToCameraPreset(camera::Gen4CameraPreset& preset, const PixelScaleConfig& config);

void applySceneCameraScaleToCameraPreset(camera::Gen4CameraPreset& preset, const SceneConfig& scene);

int worldViewportBaseWidth(const SceneConfig& scene);
int worldViewportBaseHeight(const SceneConfig& scene);
int worldViewportInternalScale(const SceneConfig& scene);
int worldViewportRenderWidth(const SceneConfig& scene);
int worldViewportRenderHeight(const SceneConfig& scene);

// Project a grounded billboard placement to integer screen pixels (SDL fallback path).
bool projectBillboardScreenRect(
    const camera::Gen4FollowCamera& camera,
    const BillboardPlacement& placement,
    int viewport_w,
    int viewport_h,
    int& out_x,
    int& out_y,
    int& out_w,
    int& out_h);

// Project the production depth-tested character quad to a screen-space bounds rect.
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
    int& out_h);

} // namespace pr::gameplay::world3d::rendering
