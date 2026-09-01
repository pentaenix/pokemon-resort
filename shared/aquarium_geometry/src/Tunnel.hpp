#pragma once

#include "aquarium_geometry/Types.hpp"

#include <vector>

namespace pr::aquarium::geometry::detail {

struct ResolvedTunnel {
    const TunnelDesign* design = nullptr;
    std::vector<GridCell> cells;
    std::vector<Vec2> route_local_world;
    GridCell entry_outward;
    GridCell exit_outward;
};

std::vector<ValidationDiagnostic> validateAndResolveTunnels(
    const TankDesign& tank,
    std::vector<ResolvedTunnel>* resolved = nullptr);

} // namespace pr::aquarium::geometry::detail
