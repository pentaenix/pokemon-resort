#pragma once

#include "Tunnel.hpp"

namespace pr::aquarium::geometry::detail {

float tunnelDryCeiling(const TankDesign& tank);

void appendTunnelPerimeterGlass(
    SemanticMesh& glass,
    const std::vector<Vec2>& tank_boundary,
    float bottom,
    float top,
    const std::vector<ResolvedTunnel>& tunnels);

void appendTunnelMeshes(
    SemanticMesh& structure,
    SemanticMesh& glass,
    const TankDesign& tank,
    const std::vector<ResolvedTunnel>& tunnels);

} // namespace pr::aquarium::geometry::detail
