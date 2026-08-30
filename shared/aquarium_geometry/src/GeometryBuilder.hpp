#pragma once

#include "aquarium_geometry/Types.hpp"

namespace pr::aquarium::geometry::detail {

void populateAquariumGeometry(
    const AquariumBuildRequest& request,
    AquariumBuildResult& result);

} // namespace pr::aquarium::geometry::detail
