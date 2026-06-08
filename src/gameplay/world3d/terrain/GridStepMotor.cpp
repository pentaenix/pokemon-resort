#include "gameplay/world3d/terrain/GridStepMotor.hpp"

namespace pr::gameplay::world3d::terrain {

GridStepMotor GridStepMotor::beginStep(
    const SceneConfig& scene,
    int from_tx,
    int from_ty,
    int to_tx,
    int to_ty,
    int step_dx,
    int step_dy,
    int from_height_units,
    int to_height_units) {
    GridStepMotor motor{};
    motor.interpolate_y =
        isSmoothRampHeightStep(scene, from_tx, from_ty, to_tx, to_ty, step_dx, step_dy);
    if (!motor.interpolate_y) {
        motor.sample_x = from_tx;
        motor.sample_y = from_ty;
        motor.sample_end_x = from_tx;
        motor.sample_end_y = from_ty;
        return motor;
    }

    const int dh = to_height_units - from_height_units;
    if (dh == 0) {
        const float from_center_y = heightAtTileCenter(scene, from_tx, from_ty);
        const float to_center_y = heightAtTileCenter(scene, to_tx, to_ty);
        if (from_center_y != to_center_y) {
            motor.center_lerp_y = true;
            motor.lerp_start_y = from_center_y;
            motor.lerp_end_y = to_center_y;
        }
    }
    const TileCoord sample_start =
        resolveRampSampleTile(scene, from_tx, from_ty, to_tx, to_ty, step_dx, step_dy, dh);
    const TileCoord sample_end =
        resolveRampSampleTileEnd(scene, from_tx, from_ty, to_tx, to_ty, step_dx, step_dy, dh);
    motor.sample_x = sample_start.x;
    motor.sample_y = sample_start.y;
    motor.sample_end_x = sample_end.x;
    motor.sample_end_y = sample_end.y;
    motor.handoff = (sample_start.x != sample_end.x || sample_start.y != sample_end.y);
    return motor;
}

TileCoord GridStepMotor::activeSampleTile(float move_t) const {
    if (handoff && move_t >= 0.5f) {
        return TileCoord{sample_end_x, sample_end_y};
    }
    return TileCoord{sample_x, sample_y};
}

} // namespace pr::gameplay::world3d::terrain
