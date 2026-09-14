#include "gameplay/world3d/aquarium/construction/AquariumConstructionVisual.hpp"

#include "aquarium_geometry/Kernel.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace pr::gameplay::world3d::aquarium::construction {
namespace geo = pr::aquarium::geometry;
namespace {

struct ScreenPoint {
    float x = 0.0f;
    float y = 0.0f;
};

float signedArea(ScreenPoint a, ScreenPoint b, ScreenPoint c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool pointInTriangle(ScreenPoint point, ScreenPoint a, ScreenPoint b, ScreenPoint c) {
    const float ab = signedArea(a, b, point);
    const float bc = signedArea(b, c, point);
    const float ca = signedArea(c, a, point);
    return (ab >= 0.0f && bc >= 0.0f && ca >= 0.0f) ||
        (ab <= 0.0f && bc <= 0.0f && ca <= 0.0f);
}

float floorForCell(const AquariumConstructionVisual& visual, geo::GridCell cell) {
    const auto found = std::find_if(visual.cells.begin(), visual.cells.end(),
        [&](const auto& surface) {
            return surface.cell.column == cell.column && surface.cell.row == cell.row;
        });
    return found == visual.cells.end() ? 0.0f : found->floor_y;
}

} // namespace

std::optional<geo::GridCell> hitTestAquariumConstructionTankCell(
    const AquariumConstructionVisual& visual,
    const std::vector<geo::TankDesign>& tanks,
    const gameplay::world3d::camera::Gen4FollowCamera& camera,
    int screen_x, int screen_y, int viewport_width, int viewport_height) {
    if (!visual.visible || viewport_width <= 0 || viewport_height <= 0) return std::nullopt;
    const ScreenPoint point{static_cast<float>(screen_x), static_cast<float>(screen_y)};
    float nearest_depth = std::numeric_limits<float>::max();
    std::optional<geo::GridCell> nearest;
    constexpr std::array<std::array<int, 4>, 6> kFaces{{
        {{0, 1, 2, 3}}, {{4, 5, 6, 7}}, {{0, 1, 5, 4}},
        {{1, 2, 6, 5}}, {{2, 3, 7, 6}}, {{3, 0, 4, 7}},
    }};
    for (const geo::TankDesign& tank : tanks) {
        for (const geo::GridCell cell : tankFootprintCells(tank)) {
            const float x0 = static_cast<float>(cell.column) * visual.tile_world_units +
                visual.placement_offset_world_units;
            const float z0 = static_cast<float>(cell.row) * visual.tile_world_units +
                visual.placement_offset_world_units;
            const float floor = floorForCell(visual, cell);
            const float bottom = floor - static_cast<float>(tank.depth_steps) *
                geo::kVerticalStepWorldUnits;
            const float top = floor + static_cast<float>(tank.height_steps) *
                geo::kVerticalStepWorldUnits;
            const float tile = visual.tile_world_units;
            const std::array<gameplay::world3d::camera::Vec3, 8> corners{{
                {x0, bottom, z0}, {x0 + tile, bottom, z0},
                {x0 + tile, bottom, z0 + tile}, {x0, bottom, z0 + tile},
                {x0, top, z0}, {x0 + tile, top, z0},
                {x0 + tile, top, z0 + tile}, {x0, top, z0 + tile},
            }};
            std::array<ScreenPoint, 8> projected{};
            std::array<float, 8> depths{};
            bool visible = true;
            for (std::size_t index = 0; index < corners.size(); ++index) {
                visible = visible && camera.worldToScreen(
                    corners[index], viewport_width, viewport_height,
                    projected[index].x, projected[index].y, depths[index]);
            }
            if (!visible) continue;
            for (const auto& face : kFaces) {
                if (!pointInTriangle(point, projected[face[0]], projected[face[1]], projected[face[2]]) &&
                    !pointInTriangle(point, projected[face[0]], projected[face[2]], projected[face[3]])) {
                    continue;
                }
                const float face_depth = depths[face[0]] + depths[face[1]] +
                    depths[face[2]] + depths[face[3]];
                if (face_depth < nearest_depth) {
                    nearest_depth = face_depth;
                    nearest = cell;
                }
            }
        }
    }
    return nearest;
}

} // namespace pr::gameplay::world3d::aquarium::construction
