#pragma once

#include "Tunnel.hpp"

namespace pr::aquarium::geometry::detail {

void appendTunnelGlassFloors(
    SemanticMesh& frame,
    SemanticMesh& glass,
    const FootprintDesign& footprint,
    const std::vector<ResolvedTunnel>& tunnels);

} // namespace pr::aquarium::geometry::detail
