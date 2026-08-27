#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"

#include "gameplay/world3d/interiors/DefaultRoomGeometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <vector>

namespace pr::gameplay::world3d::interiors {

inline bool floorCutoutCovers(const SceneConfig& scene, int tile_x, int tile_y) {
    for (const InteriorFloorCutoutConfig& cutout : scene.interior.floor_cutouts) {
        if (!cutout.local_polygon.empty()) continue;
        if (tile_x >= cutout.x && tile_x < cutout.x + cutout.width &&
            tile_y >= cutout.y && tile_y < cutout.y + cutout.height) return true;
    }
    return false;
}

struct FloorCutoutVertex {
    float x = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};

using FloorCutoutTriangle = std::array<FloorCutoutVertex, 3>;

namespace detail {

using Polygon = std::vector<FloorCutoutVertex>;
using Point = std::array<float, 2>;

inline float cross(Point a, Point b, Point p) {
    return (b[0] - a[0]) * (p[1] - a[1]) -
        (b[1] - a[1]) * (p[0] - a[0]);
}

inline FloorCutoutVertex interpolate(
    const FloorCutoutVertex& a,
    const FloorCutoutVertex& b,
    float amount) {
    return {
        a.x + (b.x - a.x) * amount,
        a.z + (b.z - a.z) * amount,
        a.u + (b.u - a.u) * amount,
        a.v + (b.v - a.v) * amount};
}

inline Polygon clipHalfPlane(
    const Polygon& input,
    Point a,
    Point b,
    float orientation,
    bool keep_inside) {
    Polygon output;
    if (input.empty()) return output;
    const auto signed_distance = [&](const FloorCutoutVertex& vertex) {
        return cross(a, b, Point{vertex.x, vertex.z}) * orientation;
    };
    const auto kept = [&](float distance) {
        return keep_inside ? distance >= -0.0001f : distance <= 0.0001f;
    };
    FloorCutoutVertex previous = input.back();
    float previous_distance = signed_distance(previous);
    bool previous_kept = kept(previous_distance);
    for (const FloorCutoutVertex& current : input) {
        const float current_distance = signed_distance(current);
        const bool current_kept = kept(current_distance);
        if (current_kept != previous_kept) {
            const float denominator = previous_distance - current_distance;
            const float amount = std::abs(denominator) <= 0.000001f
                ? 0.0f : previous_distance / denominator;
            output.push_back(interpolate(previous, current, std::clamp(amount, 0.0f, 1.0f)));
        }
        if (current_kept) output.push_back(current);
        previous = current;
        previous_distance = current_distance;
        previous_kept = current_kept;
    }
    return output;
}

inline float polygonArea(const std::vector<Point>& polygon) {
    float area = 0.0f;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const Point& a = polygon[i];
        const Point& b = polygon[(i + 1U) % polygon.size()];
        area += a[0] * b[1] - b[0] * a[1];
    }
    return area * 0.5f;
}

inline std::vector<Polygon> subtractConvex(const Polygon& source, const std::vector<Point>& hole) {
    if (source.size() < 3U || hole.size() < 3U) return {source};
    const float orientation = polygonArea(hole) >= 0.0f ? 1.0f : -1.0f;
    std::vector<Polygon> candidates{source};
    std::vector<Polygon> outside;
    for (std::size_t edge = 0; edge < hole.size() && !candidates.empty(); ++edge) {
        const Point a = hole[edge];
        const Point b = hole[(edge + 1U) % hole.size()];
        std::vector<Polygon> next;
        for (const Polygon& candidate : candidates) {
            Polygon exterior = clipHalfPlane(candidate, a, b, orientation, false);
            if (exterior.size() >= 3U) outside.push_back(std::move(exterior));
            Polygon interior = clipHalfPlane(candidate, a, b, orientation, true);
            if (interior.size() >= 3U) next.push_back(std::move(interior));
        }
        candidates = std::move(next);
    }
    return outside;
}

inline std::vector<Point> worldPolygon(
    const SceneConfig& scene,
    const InteriorFloorCutoutConfig& cutout,
    float tile_size) {
    if (!cutout.local_polygon.empty() && !cutout.placement_id.empty()) {
        const auto placement = std::find_if(scene.models.begin(), scene.models.end(), [&](const auto& model) {
            return model.id == cutout.placement_id;
        });
        if (placement != scene.models.end()) {
            const float radians = placement->yaw_deg * 3.14159265358979323846f / 180.0f;
            const float cosine = std::cos(radians);
            const float sine = std::sin(radians);
            std::vector<Point> result;
            result.reserve(cutout.local_polygon.size());
            for (const auto& local : cutout.local_polygon) {
                const float x = local[0] * placement->scale;
                const float z = local[1] * placement->scale;
                result.push_back({
                    placement->x + x * cosine + z * sine,
                    placement->z - x * sine + z * cosine});
            }
            return result;
        }
    }
    const float x0 = static_cast<float>(cutout.x) * tile_size;
    const float z0 = static_cast<float>(cutout.y) * tile_size;
    const float x1 = static_cast<float>(cutout.x + cutout.width) * tile_size;
    const float z1 = static_cast<float>(cutout.y + cutout.height) * tile_size;
    return {{x0, z0}, {x1, z0}, {x1, z1}, {x0, z1}};
}

inline void triangulate(const Polygon& polygon, std::vector<FloorCutoutTriangle>& output) {
    for (std::size_t i = 1; i + 1U < polygon.size(); ++i) {
        output.push_back({polygon[0], polygon[i], polygon[i + 1U]});
    }
}

} // namespace detail

// Returns the exact visible pieces of a cell after subtracting all authored
// convex installation footprints. UVs stay in the original 0..1 cell space.
inline std::vector<FloorCutoutTriangle> clipFloorCellAgainstCutouts(
    const SceneConfig& scene,
    int tile_x,
    int tile_y,
    float tile_size) {
    const DefaultRoomFloorClip clip = clipDefaultRoomFloorCell(scene, tile_x, tile_y, tile_size);
    const detail::Polygon first{{clip.x0, clip.z0, clip.u0, clip.v0},
        {clip.x1, clip.z0, clip.u1, clip.v0}, {clip.x1, clip.z1, clip.u1, clip.v1}};
    const detail::Polygon second{{clip.x0, clip.z0, clip.u0, clip.v0},
        {clip.x1, clip.z1, clip.u1, clip.v1}, {clip.x0, clip.z1, clip.u0, clip.v1}};
    std::vector<detail::Polygon> pieces{first, second};
    for (const InteriorFloorCutoutConfig& cutout : scene.interior.floor_cutouts) {
        const std::vector<detail::Point> hole = detail::worldPolygon(scene, cutout, tile_size);
        std::vector<detail::Polygon> next;
        for (const detail::Polygon& piece : pieces) {
            std::vector<detail::Polygon> remaining = detail::subtractConvex(piece, hole);
            next.insert(next.end(),
                std::make_move_iterator(remaining.begin()), std::make_move_iterator(remaining.end()));
        }
        pieces = std::move(next);
        if (pieces.empty()) break;
    }
    std::vector<FloorCutoutTriangle> triangles;
    for (const detail::Polygon& piece : pieces) detail::triangulate(piece, triangles);
    return triangles;
}

} // namespace pr::gameplay::world3d::interiors
