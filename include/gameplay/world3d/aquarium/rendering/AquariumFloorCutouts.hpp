#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumPlayerRuntime.hpp"

#include <vector>

namespace pr::gameplay::world3d::aquarium::rendering {

std::vector<InteriorFloorCutoutConfig> playerAquariumFloorCutouts(
    const std::vector<InteriorFloorCutoutConfig>& authored_cutouts,
    const std::vector<construction::PlayerTankRuntime>& tanks);

} // namespace pr::gameplay::world3d::aquarium::rendering
