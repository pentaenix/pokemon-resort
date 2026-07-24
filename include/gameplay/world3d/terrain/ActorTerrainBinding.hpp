#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/terrain/GridStepMotor.hpp"

namespace pr::gameplay::world3d::terrain {

// Simulation grounding: logical grid cell + height sample tile + feet Y on terrain mesh.
struct ActorTerrainBinding {
    int logical_tx = 0;
    int logical_ty = 0;
    int height_sample_tx = 0;
    int height_sample_ty = 0;
    float simulation_y = 0.0f;
};

ActorTerrainBinding bindActorStanding(
    const SceneConfig& scene,
    int logical_tx,
    int logical_ty,
    float world_x,
    float world_z);

float actorHeightDuringStep(
    const SceneConfig& scene,
    float world_x,
    float world_z,
    const GridStepMotor& motor,
    float move_t);

} // namespace pr::gameplay::world3d::terrain
