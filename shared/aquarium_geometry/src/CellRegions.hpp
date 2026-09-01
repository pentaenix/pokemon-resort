#pragma once

#include "aquarium_geometry/Types.hpp"

#include <vector>

namespace pr::aquarium::geometry::detail {

std::vector<std::vector<GridCell>> connectedCellRegions(
    const std::vector<GridCell>& cells);

std::vector<Vec2> cellRegionBoundaryLocalWorld(
    const std::vector<GridCell>& cells,
    const FootprintDesign& tank_footprint);

} // namespace pr::aquarium::geometry::detail
