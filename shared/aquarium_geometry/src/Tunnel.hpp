#pragma once

#include "aquarium_geometry/Types.hpp"

#include <optional>
#include <vector>

namespace pr::aquarium::geometry::detail {

struct ResolvedTunnel {
    const TunnelDesign* design = nullptr;
    std::vector<GridCell> cells;
    std::vector<Vec2> route_local_world;
    std::optional<GridCell> entry_outward;
    std::optional<GridCell> exit_outward;
};

struct TunnelHalfCellLayout {
    std::vector<GridCell> water;
    std::vector<GridCell> dry;
    std::vector<std::vector<GridCell>> dry_by_tunnel;
    std::size_t dry_count = 0;
};

std::vector<ValidationDiagnostic> validateAndResolveTunnels(
    const TankDesign& tank,
    std::vector<ResolvedTunnel>* resolved = nullptr);

TunnelHalfCellLayout buildTunnelHalfCellLayout(
    const FootprintDesign& footprint,
    const std::vector<ResolvedTunnel>& tunnels);

std::vector<Vec2> tunnelHalfCellBoundary(
    const std::vector<GridCell>& cells,
    const FootprintDesign& footprint);

} // namespace pr::aquarium::geometry::detail
