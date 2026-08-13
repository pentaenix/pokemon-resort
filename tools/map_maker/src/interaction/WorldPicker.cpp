#include "mapmaker/interaction/WorldPicker.hpp"

#include "gameplay/world3d/terrain/TerrainSurface.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pr::mapmaker {
namespace {

using Vec3 = gameplay::world3d::camera::Vec3;

constexpr float kPi = 3.14159265358979323846f;
constexpr float kEpsilon = 0.000001f;

Vec3 add(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 subtract(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 multiply(const Vec3& value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

float dot(const Vec3& a, const Vec3& b) {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {
        (a.y * b.z) - (a.z * b.y),
        (a.z * b.x) - (a.x * b.z),
        (a.x * b.y) - (a.y * b.x)};
}

Vec3 normalize(const Vec3& value) {
    const float length_squared = dot(value, value);
    if (length_squared <= kEpsilon) return {};
    return multiply(value, 1.0f / std::sqrt(length_squared));
}

std::optional<float> intersectTriangle(
    const WorldRay& ray,
    const Vec3& a,
    const Vec3& b,
    const Vec3& c) {
    const Vec3 edge_ab = subtract(b, a);
    const Vec3 edge_ac = subtract(c, a);
    const Vec3 p = cross(ray.direction, edge_ac);
    const float determinant = dot(edge_ab, p);
    if (std::abs(determinant) <= kEpsilon) return std::nullopt;

    const float inverse = 1.0f / determinant;
    const Vec3 offset = subtract(ray.origin, a);
    const float u = dot(offset, p) * inverse;
    if (u < 0.0f || u > 1.0f) return std::nullopt;

    const Vec3 q = cross(offset, edge_ab);
    const float v = dot(ray.direction, q) * inverse;
    if (v < 0.0f || (u + v) > 1.0f) return std::nullopt;

    const float distance = dot(edge_ac, q) * inverse;
    return distance >= 0.0f ? std::optional<float>{distance} : std::nullopt;
}

bool pickableCell(const gameplay::world3d::SceneConfig& scene, int x, int y) {
    const int width = std::max(0, scene.grid.width);
    const int height = std::max(0, scene.grid.height);
    const bool inside = x >= 0 && x < width && y >= 0 && y < height;
    const bool horizontal_halo = (x == -1 || x == width) && y >= 0 && y < height;
    const bool vertical_halo = (y == -1 || y == height) && x >= 0 && x < width;
    return inside || horizontal_halo || vertical_halo;
}

std::optional<TerrainPick> intersectCell(
    const gameplay::world3d::SceneConfig& scene,
    const WorldRay& ray,
    int x,
    int y) {
    if (!pickableCell(scene, x, y)) return std::nullopt;

    const int source_x = std::clamp(x, 0, scene.grid.width - 1);
    const int source_y = std::clamp(y, 0, scene.grid.height - 1);
    float corners[4]{};
    gameplay::world3d::terrain::fillTileCornerHeights(
        scene, source_x, source_y, corners);
    // Extend the nearest boundary edge into the halo. Repeating the whole
    // boundary tile would create a height discontinuity where the map ends.
    if (x < 0) {
        corners[1] = corners[0];
        corners[2] = corners[3];
    } else if (x >= scene.grid.width) {
        corners[0] = corners[1];
        corners[3] = corners[2];
    } else if (y < 0) {
        corners[3] = corners[0];
        corners[2] = corners[1];
    } else if (y >= scene.grid.height) {
        corners[0] = corners[3];
        corners[1] = corners[2];
    }
    const float tile_size = std::max(1.0f, scene.grid.tile_size);
    const float x0 = static_cast<float>(x) * tile_size;
    const float x1 = x0 + tile_size;
    const float z0 = static_cast<float>(y) * tile_size;
    const float z1 = z0 + tile_size;
    const Vec3 vertices[4] = {
        {x0, corners[0], z0},
        {x1, corners[1], z0},
        {x1, corners[2], z1},
        {x0, corners[3], z1}};
    const std::optional<float> first = intersectTriangle(
        ray, vertices[0], vertices[1], vertices[2]);
    const std::optional<float> second = intersectTriangle(
        ray, vertices[0], vertices[2], vertices[3]);
    const float distance = std::min(
        first.value_or(std::numeric_limits<float>::max()),
        second.value_or(std::numeric_limits<float>::max()));
    if (!std::isfinite(distance)) return std::nullopt;
    return TerrainPick{x, y, add(ray.origin, multiply(ray.direction, distance)), distance};
}

bool clipAxis(
    float origin,
    float direction,
    float minimum,
    float maximum,
    float& enter,
    float& exit) {
    if (std::abs(direction) <= kEpsilon) {
        return origin >= minimum && origin <= maximum;
    }
    float first = (minimum - origin) / direction;
    float second = (maximum - origin) / direction;
    if (first > second) std::swap(first, second);
    enter = std::max(enter, first);
    exit = std::min(exit, second);
    return exit >= enter;
}

int containingCell(float coordinate, float tile_size, int minimum, int maximum) {
    return std::clamp(
        static_cast<int>(std::floor(coordinate / tile_size)), minimum, maximum);
}

} // namespace

WorldRay screenRay(
    const gameplay::world3d::camera::Gen4FollowCamera& camera,
    float viewport_x,
    float viewport_y,
    int viewport_width,
    int viewport_height) {
    const auto pose = camera.pose();
    const float width = static_cast<float>(std::max(1, viewport_width));
    const float height = static_cast<float>(std::max(1, viewport_height));
    const float ndc_x = ((viewport_x / width) * 2.0f) - 1.0f;
    const float ndc_y = 1.0f - ((viewport_y / height) * 2.0f);
    const float aspect = width / height;
    const float half_fov = pose.preset.fov_y_deg * (kPi / 180.0f) * 0.5f;
    const float half_height = std::tan(std::max(0.001f, half_fov));
    const Vec3 direction = normalize(add(
        pose.forward,
        add(
            multiply(pose.right, ndc_x * half_height * aspect),
            multiply(pose.up, ndc_y * half_height))));
    return {pose.position, direction};
}

std::optional<TerrainPick> pickTerrain(
    const gameplay::world3d::SceneConfig& scene,
    const WorldRay& ray) {
    const int width = std::max(0, scene.grid.width);
    const int height = std::max(0, scene.grid.height);
    if (width == 0 || height == 0) return std::nullopt;
    const float tile_size = std::max(1.0f, scene.grid.tile_size);

    // Walk only the grid cells crossed by the ray's X/Z projection. This keeps
    // hover picking proportional to map diameter rather than total map area.
    // The cardinal one-cell halo is intentional: doors and anchors may live
    // immediately outside a map, while diagonal corner cells remain invalid.
    float enter = 0.0f;
    float exit = std::numeric_limits<float>::infinity();
    if (!clipAxis(ray.origin.x, ray.direction.x, -tile_size,
            static_cast<float>(width + 1) * tile_size, enter, exit) ||
        !clipAxis(ray.origin.z, ray.direction.z, -tile_size,
            static_cast<float>(height + 1) * tile_size, enter, exit)) {
        return std::nullopt;
    }

    const bool vertical = std::abs(ray.direction.x) <= kEpsilon &&
        std::abs(ray.direction.z) <= kEpsilon;
    if (vertical) {
        return intersectCell(
            scene,
            ray,
            containingCell(ray.origin.x, tile_size, -1, width),
            containingCell(ray.origin.z, tile_size, -1, height));
    }

    const float sample_time = enter + 0.0001f;
    int x = containingCell(
        ray.origin.x + ray.direction.x * sample_time, tile_size, -1, width);
    int y = containingCell(
        ray.origin.z + ray.direction.z * sample_time, tile_size, -1, height);
    const int step_x = ray.direction.x > kEpsilon ? 1 : ray.direction.x < -kEpsilon ? -1 : 0;
    const int step_y = ray.direction.z > kEpsilon ? 1 : ray.direction.z < -kEpsilon ? -1 : 0;
    const float delta_x = step_x == 0 ? std::numeric_limits<float>::infinity()
        : tile_size / std::abs(ray.direction.x);
    const float delta_y = step_y == 0 ? std::numeric_limits<float>::infinity()
        : tile_size / std::abs(ray.direction.z);
    const float boundary_x = static_cast<float>(step_x > 0 ? x + 1 : x) * tile_size;
    const float boundary_y = static_cast<float>(step_y > 0 ? y + 1 : y) * tile_size;
    float next_x = step_x == 0 ? std::numeric_limits<float>::infinity()
        : (boundary_x - ray.origin.x) / ray.direction.x;
    float next_y = step_y == 0 ? std::numeric_limits<float>::infinity()
        : (boundary_y - ray.origin.z) / ray.direction.z;

    std::optional<TerrainPick> nearest;
    const int maximum_visits = width + height + 8;
    for (int visit = 0; visit < maximum_visits; ++visit) {
        if (const auto candidate = intersectCell(scene, ray, x, y);
            candidate && (!nearest || candidate->distance < nearest->distance)) {
            nearest = candidate;
        }
        const float cell_exit = std::min(next_x, next_y);
        if (cell_exit > exit || !std::isfinite(cell_exit)) break;
        if (next_x <= next_y) {
            x += step_x;
            next_x += delta_x;
        }
        if (next_y <= cell_exit) {
            y += step_y;
            next_y += delta_y;
        }
        if (x < -1 || x > width || y < -1 || y > height) break;
    }
    return nearest;
}

std::optional<TerrainPick> pickTerrainFromScreen(
    const gameplay::world3d::SceneConfig& scene,
    const gameplay::world3d::camera::Gen4FollowCamera& camera,
    float viewport_x,
    float viewport_y,
    int viewport_width,
    int viewport_height) {
    return pickTerrain(scene, screenRay(
        camera, viewport_x, viewport_y, viewport_width, viewport_height));
}

} // namespace pr::mapmaker
