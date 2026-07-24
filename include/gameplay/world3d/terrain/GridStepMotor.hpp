#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/terrain/TerrainSurface.hpp"

namespace pr::gameplay::world3d::terrain {

// Shared ramp-aware height sampling state for grid tile steps (player + follower).
struct GridStepMotor {
    int sample_x = 0;
    int sample_y = 0;
    int sample_end_x = 0;
    int sample_end_y = 0;
    float lerp_start_y = 0.0f;
    float lerp_end_y = 0.0f;
    bool handoff = false;
    bool interpolate_y = false;
    bool center_lerp_y = false;
    bool surface_follow = false;

    static GridStepMotor beginStep(
        const SceneConfig& scene,
        int from_tx,
        int from_ty,
        int to_tx,
        int to_ty,
        int step_dx,
        int step_dy,
        int from_height_units,
        int to_height_units);

    TileCoord activeSampleTile(float move_t) const;
};

} // namespace pr::gameplay::world3d::terrain
