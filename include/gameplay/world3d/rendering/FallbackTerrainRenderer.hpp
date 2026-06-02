#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"

#include <SDL.h>

namespace pr::gameplay::world3d::rendering {

void renderFallbackTerrain(
    SDL_Renderer* renderer,
    const camera::Gen4FollowCamera& camera,
    const SceneConfig& scene,
    float tile_size,
    int viewport_w,
    int viewport_h);

} // namespace pr::gameplay::world3d::rendering
