#pragma once

#include "gameplay/world3d/followers/NatureIdlePlanner.hpp"

#include <optional>

namespace pr::gameplay::world3d::followers {

std::optional<GridPoint> selectIdleExitTarget(
    const SceneConfig& scene,
    const GridPoint& player_tile,
    const GridPoint& follower_tile,
    const GridPoint& idle_origin_tile,
    const GridPoint* occupied_tile);

} // namespace pr::gameplay::world3d::followers
