#include "gameplay/world3d/aquarium/construction/AquariumConstructionVisual.hpp"

#include "aquarium_geometry/Kernel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace pr::gameplay::world3d::aquarium::construction {
namespace geo = pr::aquarium::geometry;
namespace {

constexpr std::uint32_t colorAbgr(
    std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha) {
    return static_cast<std::uint32_t>(red) |
        (static_cast<std::uint32_t>(green) << 8U) |
        (static_cast<std::uint32_t>(blue) << 16U) |
        (static_cast<std::uint32_t>(alpha) << 24U);
}

bool sameCell(geo::GridCell lhs, geo::GridCell rhs) {
    return lhs.column == rhs.column && lhs.row == rhs.row;
}

float floorForTank(
    const AquariumConstructionVisual& visual,
    const geo::TankDesign& tank) {
    const geo::GridCell centre{
        tank.footprint.origin_cell.column +
            (geo::occupiedWidthCells(tank.footprint) - 1) / 2,
        tank.footprint.origin_cell.row +
            (geo::occupiedDepthCells(tank.footprint) - 1) / 2,
    };
    const auto found = std::find_if(visual.cells.begin(), visual.cells.end(),
        [&](const auto& surface) { return sameCell(surface.cell, centre); });
    return found == visual.cells.end() ? 0.0f : found->floor_y;
}

void appendDot(
    ConstructionVisualMesh& mesh, geo::Vec3 point,
    float radius, std::uint32_t color) {
    if (mesh.vertices.size() > std::numeric_limits<std::uint16_t>::max() - 4U) return;
    const auto first = static_cast<std::uint16_t>(mesh.vertices.size());
    mesh.vertices.push_back({point.x, point.y, point.z - radius, color});
    mesh.vertices.push_back({point.x + radius, point.y, point.z, color});
    mesh.vertices.push_back({point.x, point.y, point.z + radius, color});
    mesh.vertices.push_back({point.x - radius, point.y, point.z, color});
    mesh.indices.insert(mesh.indices.end(), {
        first, static_cast<std::uint16_t>(first + 1U),
        static_cast<std::uint16_t>(first + 2U), first,
        static_cast<std::uint16_t>(first + 2U),
        static_cast<std::uint16_t>(first + 3U)});
}

void appendDottedSegment(
    ConstructionVisualMesh& mesh, geo::Vec3 start, geo::Vec3 end,
    float spacing, float radius, std::uint32_t color) {
    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float dz = end.z - start.z;
    const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
    const int divisions = std::max(1, static_cast<int>(std::ceil(length / spacing)));
    for (int division = 0; division <= divisions; ++division) {
        const float amount = static_cast<float>(division) /
            static_cast<float>(divisions);
        appendDot(mesh, {
            start.x + dx * amount,
            start.y + dy * amount,
            start.z + dz * amount,
        }, radius, color);
    }
}

} // namespace

ConstructionVisualMesh buildAquariumConstructionDepthPreviewMesh(
    const AquariumConstructionVisual& visual) {
    ConstructionVisualMesh mesh;
    if (!visual.visible) return mesh;
    const auto tank = visual.preview_tank ? visual.preview_tank : visual.selected_tank;
    if (!tank || tank->depth_steps <= 0) return mesh;

    constexpr std::uint32_t kBottomOutline = colorAbgr(65, 222, 255, 220);
    constexpr std::uint32_t kVerticalGuide = colorAbgr(255, 220, 85, 185);
    constexpr float kDotSpacing = 4.0f;
    constexpr float kDotRadius = 0.72f;
    const float offset = visual.placement_offset_world_units;
    const float center_x = geo::footprintCentreWorld(
        tank->footprint.origin_cell.column,
        geo::occupiedWidthCells(tank->footprint)) + offset;
    const float center_z = geo::footprintCentreWorld(
        tank->footprint.origin_cell.row,
        geo::occupiedDepthCells(tank->footprint)) + offset;
    const float floor_y = floorForTank(visual, *tank);
    const float bottom_y = floor_y - static_cast<float>(
        tank->depth_steps * geo::kVerticalStepWorldUnits);
    const auto boundary = geo::footprintBoundaryLocalWorld(
        tank->footprint, tank->corner_radius_steps, tank->corner_radii);

    for (std::size_t index = 0; index < boundary.size(); ++index) {
        const auto start = boundary[index];
        const auto end = boundary[(index + 1U) % boundary.size()];
        appendDottedSegment(mesh,
            {center_x + start.x, bottom_y, center_z + start.y},
            {center_x + end.x, bottom_y, center_z + end.y},
            kDotSpacing, kDotRadius, kBottomOutline);
    }

    const float west = static_cast<float>(tank->footprint.origin_cell.column) *
        visual.tile_world_units + offset;
    const float north = static_cast<float>(tank->footprint.origin_cell.row) *
        visual.tile_world_units + offset;
    for (const auto& corner : geo::footprintCorners(tank->footprint)) {
        const float x = west + static_cast<float>(corner.vertex.column) *
            visual.tile_world_units;
        const float z = north + static_cast<float>(corner.vertex.row) *
            visual.tile_world_units;
        appendDottedSegment(mesh, {x, floor_y - 0.4f, z}, {x, bottom_y, z},
            kDotSpacing, kDotRadius, kVerticalGuide);
    }
    return mesh;
}

} // namespace pr::gameplay::world3d::aquarium::construction
