#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/camera/Gen4CameraPreset.hpp"
#include "gameplay/world3d/rendering/BillboardPlacement.hpp"

namespace pr::gameplay::world3d::rendering {

// Shared pixel-art ruler: map tiles and sprite billboards use the same world-units-per-pixel.
float worldUnitsPerPixel(const SceneConfig& scene);

float authoredSpriteWorldHeight(const SceneConfig& scene, float frame_height_px, float scale_multiplier = 1.0f);

float clampedPixelZoom(const PixelScaleConfig& config);

void applyPixelScaleToCameraPreset(camera::Gen4CameraPreset& preset, const PixelScaleConfig& config);

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

} // namespace pr::gameplay::world3d::rendering
