#include "gameplay/world3d/terrain/ActorTerrainBinding.hpp"

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
    const TileCoord sample = motor.activeSampleTile(move_t);
    return heightAtActorFeet(scene, world_x, world_z, sample.x, sample.y);
}

} // namespace pr::gameplay::world3d::terrain
