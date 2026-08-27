#include "mapmaker/preview/ModelTopDownProjection.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pr::mapmaker {
namespace {

float cross(const ModelTopDownPoint& origin, const ModelTopDownPoint& a,
    const ModelTopDownPoint& b) {
    return (a.x - origin.x) * (b.z - origin.z) -
        (a.z - origin.z) * (b.x - origin.x);
}

} // namespace

ModelTopDownProjection buildModelTopDownProjection(
    std::span<const ModelTopDownPoint> input) {
    ModelTopDownProjection result;
    if (input.empty()) return result;
    std::vector<ModelTopDownPoint> points(input.begin(), input.end());
    std::sort(points.begin(), points.end(), [](const auto& a, const auto& b) {
        return a.x == b.x ? a.z < b.z : a.x < b.x;
    });
    points.erase(std::unique(points.begin(), points.end(), [](const auto& a, const auto& b) {
        return a.x == b.x && a.z == b.z;
    }), points.end());
    if (points.empty()) return result;

    float min_x = points.front().x;
    float max_x = points.front().x;
    float min_z = points.front().z;
    float max_z = points.front().z;
    for (const auto& point : points) {
        min_x = std::min(min_x, point.x);
        max_x = std::max(max_x, point.x);
        min_z = std::min(min_z, point.z);
        max_z = std::max(max_z, point.z);
    }
    result.width = max_x - min_x;
    result.depth = max_z - min_z;
    if (points.size() <= 2U) {
        result.outline = std::move(points);
        result.valid = true;
        return result;
    }

    std::vector<ModelTopDownPoint> hull(points.size() * 2U);
    std::size_t count = 0;
    for (const auto& point : points) {
        while (count >= 2U && cross(hull[count - 2U], hull[count - 1U], point) <= 0.0f) --count;
        hull[count++] = point;
    }
    const std::size_t lower_count = count;
    for (auto point = points.rbegin() + 1; point != points.rend(); ++point) {
        while (count > lower_count && cross(hull[count - 2U], hull[count - 1U], *point) <= 0.0f) --count;
        hull[count++] = *point;
    }
    if (count > 1U) --count;
    hull.resize(count);
    result.outline = std::move(hull);
    result.valid = !result.outline.empty();
    return result;
}

PlacedModelTopDownProjection placeModelTopDownProjection(
    const ModelTopDownProjection& projection,
    float yaw_degrees,
    float scale,
    float tile_size) {
    PlacedModelTopDownProjection result;
    if (!projection.valid || projection.outline.empty() || tile_size <= 0.0f) return result;
    constexpr float kPi = 3.14159265358979323846f;
    const float yaw = yaw_degrees * (kPi / 180.0f);
    const float cosine = std::cos(yaw);
    const float sine = std::sin(yaw);
    const float unit_scale = scale / tile_size;
    float min_x = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float min_z = std::numeric_limits<float>::max();
    float max_z = std::numeric_limits<float>::lowest();
    result.outline_tiles.reserve(projection.outline.size());
    for (const auto& point : projection.outline) {
        const ModelTopDownPoint transformed{
            (cosine * point.x + sine * point.z) * unit_scale,
            (-sine * point.x + cosine * point.z) * unit_scale,
        };
        result.outline_tiles.push_back(transformed);
        min_x = std::min(min_x, transformed.x);
        max_x = std::max(max_x, transformed.x);
        min_z = std::min(min_z, transformed.z);
        max_z = std::max(max_z, transformed.z);
    }
    result.width_tiles = max_x - min_x;
    result.depth_tiles = max_z - min_z;
    return result;
}

} // namespace pr::mapmaker
