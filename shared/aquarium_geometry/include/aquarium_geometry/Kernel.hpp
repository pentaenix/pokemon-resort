#pragma once

#include "aquarium_geometry/Types.hpp"

namespace pr::aquarium::geometry {

float cellCentreWorld(std::int32_t cell_index);
float footprintCentreWorld(std::int32_t origin_cell, std::int32_t size_cells);

ValidationReport validateAquarium(const AquariumBuildRequest& request);
AquariumBuildResult buildAquarium(const AquariumBuildRequest& request);

const char* meshMaterialName(MeshMaterial material);

} // namespace pr::aquarium::geometry
