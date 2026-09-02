#pragma once

#include "aquarium_geometry/Types.hpp"

namespace pr::aquarium::geometry {

float cellCentreWorld(std::int32_t cell_index);
float footprintCentreWorld(std::int32_t origin_cell, std::int32_t size_cells);
std::int32_t occupiedWidthCells(const FootprintDesign& footprint);
std::int32_t occupiedDepthCells(const FootprintDesign& footprint);
std::vector<GridCell> footprintCells(const FootprintDesign& footprint);
std::int32_t fittedCornerRadiusSteps(
    const FootprintDesign& footprint,
    std::int32_t requested_steps);
struct FootprintCorner {
    GridCell vertex;
    bool convex = false;
};
std::vector<FootprintCorner> footprintCorners(const FootprintDesign& footprint);
std::int32_t fittedCornerRadiusStepsAt(
    const FootprintDesign& footprint, GridCell vertex, std::int32_t requested_steps);
std::vector<Vec2> footprintBoundaryLocalWorld(
    const FootprintDesign& footprint,
    std::int32_t radius_steps);
std::vector<Vec2> footprintBoundaryLocalWorld(
    const FootprintDesign& footprint,
    std::int32_t default_radius_steps,
    const std::vector<CornerRadiusDesign>& corner_radii);
bool footprintHasMinimumThreeCellSections(const FootprintDesign& footprint);
bool tunnelPortalFitsBoundary(const TankDesign& tank, GridCell portal);

ValidationReport validateAquarium(const AquariumBuildRequest& request);
AquariumBuildResult buildAquarium(const AquariumBuildRequest& request);

const char* meshMaterialName(MeshMaterial material);

} // namespace pr::aquarium::geometry
