#include "gameplay/world3d/aquarium/rendering/AquariumFloorCutouts.hpp"

#include "aquarium_geometry/Kernel.hpp"

#include <utility>

namespace pr::gameplay::world3d::aquarium::rendering {

std::vector<InteriorFloorCutoutConfig> playerAquariumFloorCutouts(
    const std::vector<InteriorFloorCutoutConfig>& authored_cutouts,
    const std::vector<construction::PlayerTankRuntime>& tanks) {
    std::vector<InteriorFloorCutoutConfig> cutouts = authored_cutouts;
    for (const auto& tank : tanks) {
        if (tank.design.depth_steps <= 0) continue;
        const auto boundary = pr::aquarium::geometry::footprintBoundaryLocalWorld(
            tank.design.footprint, tank.design.corner_radius_steps,
            tank.design.corner_radii);
        InteriorFloorCutoutConfig cutout;
        cutout.world_polygon.reserve(boundary.size());
        for (const auto point : boundary) {
            cutout.world_polygon.push_back({
                tank.world_center_x + point.x,
                tank.world_center_z + point.y});
        }
        cutouts.push_back(std::move(cutout));
    }
    return cutouts;
}

} // namespace pr::gameplay::world3d::aquarium::rendering
