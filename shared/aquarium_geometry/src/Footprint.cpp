#include "aquarium_geometry/Kernel.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace pr::aquarium::geometry {
namespace {

using IntPoint = std::pair<std::int32_t, std::int32_t>;

bool canonicalCellOccupied(const FootprintDesign& footprint, std::int32_t x, std::int32_t y) {
    if (x < 0 || y < 0 || x >= footprint.width_cells || y >= footprint.depth_cells) return false;
    if (std::any_of(footprint.subtracted_cells.begin(), footprint.subtracted_cells.end(),
            [&](GridCell cell) { return cell.column == x && cell.row == y; })) {
        return false;
    }
    if (footprint.shape == FootprintShape::Rectangle) return true;
    if (footprint.shape == FootprintShape::L) {
        return x < footprint.width_cells - footprint.notch_width_cells ||
            y < footprint.depth_cells - footprint.notch_depth_cells;
    }
    const std::int32_t left_arm =
        (footprint.width_cells - footprint.notch_width_cells) / 2;
    const bool in_opening = x >= left_arm &&
        x < left_arm + footprint.notch_width_cells &&
        y >= footprint.depth_cells - footprint.notch_depth_cells;
    return !in_opening;
}

IntPoint rotateCell(
    std::int32_t x, std::int32_t y,
    std::int32_t width, std::int32_t depth,
    std::int32_t rotation) {
    switch ((rotation % 4 + 4) % 4) {
    case 1: return {depth - 1 - y, x};
    case 2: return {width - 1 - x, depth - 1 - y};
    case 3: return {y, width - 1 - x};
    default: return {x, y};
    }
}

float cross(Vec2 a, Vec2 b, Vec2 c) {
    return (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
}

std::vector<IntPoint> simplifiedBoundary(const FootprintDesign& footprint) {
    const auto cells = footprintCells(footprint);
    std::set<IntPoint> occupied;
    for (const GridCell cell : cells) {
        occupied.emplace(
            cell.column - footprint.origin_cell.column,
            cell.row - footprint.origin_cell.row);
    }
    std::map<IntPoint, IntPoint> edges;
    const auto add = [&](IntPoint start, IntPoint end) { edges.emplace(start, end); };
    for (const auto [x, y] : occupied) {
        if (!occupied.count({x, y - 1})) add({x, y}, {x + 1, y});
        if (!occupied.count({x + 1, y})) add({x + 1, y}, {x + 1, y + 1});
        if (!occupied.count({x, y + 1})) add({x + 1, y + 1}, {x, y + 1});
        if (!occupied.count({x - 1, y})) add({x, y + 1}, {x, y});
    }
    if (edges.empty()) return {};
    const IntPoint start = edges.begin()->first;
    std::vector<IntPoint> raw{start};
    IntPoint current = start;
    do {
        const auto found = edges.find(current);
        if (found == edges.end()) return {};
        current = found->second;
        if (current != start) raw.push_back(current);
    } while (current != start && raw.size() <= edges.size());

    std::vector<IntPoint> simplified;
    for (std::size_t index = 0; index < raw.size(); ++index) {
        const IntPoint previous = raw[(index + raw.size() - 1) % raw.size()];
        const IntPoint point = raw[index];
        const IntPoint next = raw[(index + 1) % raw.size()];
        const bool collinear = (previous.first == point.first && point.first == next.first) ||
            (previous.second == point.second && point.second == next.second);
        if (!collinear) simplified.push_back(point);
    }
    return simplified;
}

} // namespace

std::int32_t occupiedWidthCells(const FootprintDesign& footprint) {
    return (footprint.rotation_quarter_turns & 1) != 0
        ? footprint.depth_cells : footprint.width_cells;
}

std::int32_t occupiedDepthCells(const FootprintDesign& footprint) {
    return (footprint.rotation_quarter_turns & 1) != 0
        ? footprint.width_cells : footprint.depth_cells;
}

std::vector<GridCell> footprintCells(const FootprintDesign& footprint) {
    std::vector<GridCell> cells;
    if (footprint.width_cells <= 0 || footprint.depth_cells <= 0) return cells;
    const std::int64_t area = static_cast<std::int64_t>(footprint.width_cells) *
        static_cast<std::int64_t>(footprint.depth_cells);
    if (area > kMaxFootprintCells) return cells;
    cells.reserve(static_cast<std::size_t>(footprint.width_cells) *
        static_cast<std::size_t>(footprint.depth_cells));
    for (std::int32_t y = 0; y < footprint.depth_cells; ++y) {
        for (std::int32_t x = 0; x < footprint.width_cells; ++x) {
            if (!canonicalCellOccupied(footprint, x, y)) continue;
            const auto [rotated_x, rotated_y] = rotateCell(
                x, y, footprint.width_cells, footprint.depth_cells,
                footprint.rotation_quarter_turns);
            cells.push_back({
                footprint.origin_cell.column + rotated_x,
                footprint.origin_cell.row + rotated_y,
            });
        }
    }
    std::sort(cells.begin(), cells.end(), [](GridCell lhs, GridCell rhs) {
        return lhs.row < rhs.row || (lhs.row == rhs.row && lhs.column < rhs.column);
    });
    return cells;
}

std::int32_t fittedCornerRadiusSteps(
    const FootprintDesign& footprint,
    std::int32_t requested_steps) {
    if (requested_steps <= 0) return 0;
    const auto boundary = simplifiedBoundary(footprint);
    if (boundary.size() < 4) return 0;
    std::int32_t shortest_cells = std::max(occupiedWidthCells(footprint), occupiedDepthCells(footprint));
    for (std::size_t index = 0; index < boundary.size(); ++index) {
        const auto a = boundary[index];
        const auto b = boundary[(index + 1) % boundary.size()];
        shortest_cells = std::min(shortest_cells,
            static_cast<std::int32_t>(std::abs(a.first - b.first) + std::abs(a.second - b.second)));
    }
    const std::int32_t maximum_world = shortest_cells * kWorldUnitsPerCell / 2;
    return std::min(requested_steps, maximum_world / kRadiusStepWorldUnits);
}

std::vector<Vec2> footprintBoundaryLocalWorld(
    const FootprintDesign& footprint,
    std::int32_t radius_steps) {
    const auto boundary = simplifiedBoundary(footprint);
    if (boundary.size() < 4) return {};
    const float offset_x = static_cast<float>(occupiedWidthCells(footprint) * kWorldUnitsPerCell) * 0.5F;
    const float offset_z = static_cast<float>(occupiedDepthCells(footprint) * kWorldUnitsPerCell) * 0.5F;
    std::vector<Vec2> points;
    points.reserve(boundary.size());
    for (const auto [x, y] : boundary) {
        points.push_back({
            static_cast<float>(x * kWorldUnitsPerCell) - offset_x,
            static_cast<float>(y * kWorldUnitsPerCell) - offset_z,
        });
    }
    const std::int32_t fitted_steps = fittedCornerRadiusSteps(footprint, radius_steps);
    if (fitted_steps <= 0) return points;
    const float radius = static_cast<float>(fitted_steps * kRadiusStepWorldUnits);
    constexpr int kArcSegments = 4;
    std::vector<Vec2> rounded;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const Vec2 previous = points[(index + points.size() - 1) % points.size()];
        const Vec2 point = points[index];
        const Vec2 next = points[(index + 1) % points.size()];
        if (cross(previous, point, next) <= 0.0F) {
            rounded.push_back(point);
            continue;
        }
        const float incoming_x = point.x - previous.x;
        const float incoming_z = point.y - previous.y;
        const float outgoing_x = next.x - point.x;
        const float outgoing_z = next.y - point.y;
        const float incoming_length = std::hypot(incoming_x, incoming_z);
        const float outgoing_length = std::hypot(outgoing_x, outgoing_z);
        const float in_x = incoming_x / incoming_length;
        const float in_z = incoming_z / incoming_length;
        const float out_x = outgoing_x / outgoing_length;
        const float out_z = outgoing_z / outgoing_length;
        const Vec2 center{
            point.x - in_x * radius + out_x * radius,
            point.y - in_z * radius + out_z * radius,
        };
        const Vec2 first{point.x - in_x * radius, point.y - in_z * radius};
        const float first_x = first.x - center.x;
        const float first_z = first.y - center.y;
        constexpr float kCosines[kArcSegments + 1]{
            1.0F, 0.9238795325F, 0.7071067812F, 0.3826834324F, 0.0F,
        };
        constexpr float kSines[kArcSegments + 1]{
            0.0F, 0.3826834324F, 0.7071067812F, 0.9238795325F, 1.0F,
        };
        for (int segment = 0; segment <= kArcSegments; ++segment) {
            const float cosine = kCosines[segment];
            const float sine = kSines[segment];
            rounded.push_back({
                center.x + first_x * cosine - first_z * sine,
                center.y + first_x * sine + first_z * cosine,
            });
        }
    }
    return rounded;
}

} // namespace pr::aquarium::geometry
