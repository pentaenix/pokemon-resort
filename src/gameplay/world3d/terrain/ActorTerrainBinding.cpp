#include "gameplay/world3d/terrain/ActorTerrainBinding.hpp"

#include <algorithm>

namespace pr::gameplay::world3d::terrain {

ActorTerrainBinding bindActorStanding(
    const SceneConfig& scene,
    int logical_tx,
    int logical_ty,
    float world_x,
    float world_z) {
    ActorTerrainBinding binding{};
    binding.logical_tx = logical_tx;
    binding.logical_ty = logical_ty;
    binding.height_sample_tx = logical_tx;
    binding.height_sample_ty = logical_ty;
    binding.simulation_y = heightAtActorFeet(scene, world_x, world_z, logical_tx, logical_ty);
    return binding;
}

float actorHeightDuringStep(
    const SceneConfig& scene,
    float world_x,
    float world_z,
    const GridStepMotor& motor,
    float move_t) {
    if (motor.center_lerp_y) {
        return motor.lerp_start_y + ((motor.lerp_end_y - motor.lerp_start_y) * move_t);
    }
    if (motor.handoff) {
        const float start_y = heightAtActorFeet(scene, world_x, world_z, motor.sample_x, motor.sample_y);
        const float end_y = heightAtActorFeet(scene, world_x, world_z, motor.sample_end_x, motor.sample_end_y);
        const float handoff_t = std::clamp((move_t - 0.5f) * 2.0f, 0.0f, 1.0f);
        return start_y + ((end_y - start_y) * handoff_t);
    }
    const TileCoord sample = motor.activeSampleTile(move_t);
    return heightAtActorFeet(scene, world_x, world_z, sample.x, sample.y);
}

} // namespace pr::gameplay::world3d::terrain
