#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"

namespace pr::gameplay::world3d::npc {

// The character-testing and Resort population is an exterior-world population.
// Interior occupants must be authored on that interior instead of being copied
// from whichever exterior map the player just left.
inline bool allowsGlobalDefaultPopulation(const SceneConfig& scene) {
    return scene.map_type != "interior";
}

} // namespace pr::gameplay::world3d::npc
