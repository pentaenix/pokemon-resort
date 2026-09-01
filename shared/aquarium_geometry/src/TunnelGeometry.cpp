#include "TunnelGeometry.hpp"

#include "aquarium_geometry/Kernel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace pr::aquarium::geometry::detail {
namespace {

constexpr float kGlassThickness =
    static_cast<float>(kGlassThicknessMilliWorldUnits) / 1000.0F;
constexpr float kTunnelOuterHalfWidth = kWorldUnitsPerCell * 0.5F;
constexpr float kTunnelInnerHalfWidth = kTunnelOuterHalfWidth - kGlassThickness;
constexpr float kTunnelNominalCrown = kVerticalStepWorldUnits * 4.0F;
constexpr float kGlassTopInset = 0.5168F;
constexpr float kPi = 3.14159265358979323846F;

Vec3 subtract(Vec3 lhs, Vec3 rhs) {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Vec3 cross(Vec3 lhs, Vec3 rhs) {
    return {lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x};
}

Vec3 normalized(Vec3 value) {
    const float length = std::max(0.0001F,
        std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z));
    return {value.x / length, value.y / length, value.z / length};
}

void addQuad(SemanticMesh& mesh, const std::array<Vec3, 4>& points) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    const Vec3 normal = normalized(cross(
        subtract(points[1], points[0]), subtract(points[2], points[0])));
    constexpr std::array<Vec2, 4> uv{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
    for (std::size_t index = 0; index < points.size(); ++index) {
        mesh.vertices.push_back({points[index], normal, uv[index]});
    }
    mesh.indices.insert(mesh.indices.end(),
        {base, base + 1U, base + 2U, base, base + 2U, base + 3U});
}

struct ProfilePoint {
    float lateral = 0.0F;
    float y = 0.0F;
};

std::vector<ProfilePoint> archProfile(float half_width, float crown) {
    constexpr int kArcSegments = 8;
    const float wall = std::max(0.0F, crown - half_width);
    std::vector<ProfilePoint> profile;
    profile.reserve(kArcSegments + 3U);
    profile.push_back({-half_width, 0.0F});
    for (int segment = 0; segment <= kArcSegments; ++segment) {
        const float angle = kPi - kPi * static_cast<float>(segment) /
            static_cast<float>(kArcSegments);
        profile.push_back({std::cos(angle) * half_width,
            wall + std::sin(angle) * half_width});
    }
    profile.push_back({half_width, 0.0F});
    return profile;
}

Vec2 offsetRoutePoint(
    const std::vector<Vec2>& route, std::size_t index, float offset) {
    const Vec2 point = route[index];
    if (index == 0 || index + 1U == route.size()) {
        const Vec2 neighbour = route[index == 0 ? 1U : route.size() - 2U];
        float dx = index == 0 ? neighbour.x - point.x : point.x - neighbour.x;
        float dz = index == 0 ? neighbour.y - point.y : point.y - neighbour.y;
        const float length = std::max(0.0001F, std::hypot(dx, dz));
        dx /= length;
        dz /= length;
        return {point.x - dz * offset, point.y + dx * offset};
    }
    const Vec2 before = route[index - 1U];
    const Vec2 after = route[index + 1U];
    float in_x = point.x - before.x;
    float in_z = point.y - before.y;
    float out_x = after.x - point.x;
    float out_z = after.y - point.y;
    const float in_length = std::max(0.0001F, std::hypot(in_x, in_z));
    const float out_length = std::max(0.0001F, std::hypot(out_x, out_z));
    in_x /= in_length; in_z /= in_length;
    out_x /= out_length; out_z /= out_length;
    const Vec2 in_normal{-in_z, in_x};
    const Vec2 out_normal{-out_z, out_x};
    float miter_x = in_normal.x + out_normal.x;
    float miter_z = in_normal.y + out_normal.y;
    const float miter_length = std::hypot(miter_x, miter_z);
    if (miter_length <= 0.0001F) {
        return {point.x + out_normal.x * offset, point.y + out_normal.y * offset};
    }
    miter_x /= miter_length;
    miter_z /= miter_length;
    const float denominator = std::max(0.1F,
        std::abs(miter_x * out_normal.x + miter_z * out_normal.y));
    return {point.x + miter_x * offset / denominator,
        point.y + miter_z * offset / denominator};
}

Vec3 station(
    const std::vector<Vec2>& route, std::size_t route_index,
    ProfilePoint profile) {
    const Vec2 horizontal = offsetRoutePoint(route, route_index, profile.lateral);
    return {horizontal.x, profile.y, horizontal.y};
}

void appendProfileSweep(
    SemanticMesh& mesh,
    const std::vector<Vec2>& route,
    const std::vector<ProfilePoint>& inner,
    const std::vector<ProfilePoint>& outer) {
    for (std::size_t route_index = 0; route_index + 1U < route.size(); ++route_index) {
        for (std::size_t profile_index = 0; profile_index + 1U < inner.size(); ++profile_index) {
            addQuad(mesh, {{
                station(route, route_index, outer[profile_index]),
                station(route, route_index + 1U, outer[profile_index]),
                station(route, route_index + 1U, outer[profile_index + 1U]),
                station(route, route_index, outer[profile_index + 1U]),
            }});
            addQuad(mesh, {{
                station(route, route_index, inner[profile_index + 1U]),
                station(route, route_index + 1U, inner[profile_index + 1U]),
                station(route, route_index + 1U, inner[profile_index]),
                station(route, route_index, inner[profile_index]),
            }});
        }
    }
}

void appendFloorStrip(SemanticMesh& mesh, const std::vector<Vec2>& route) {
    constexpr float kFloorY = 0.06F;
    for (std::size_t index = 0; index + 1U < route.size(); ++index) {
        const Vec2 left_a = offsetRoutePoint(route, index, -kTunnelOuterHalfWidth);
        const Vec2 right_a = offsetRoutePoint(route, index, kTunnelOuterHalfWidth);
        const Vec2 left_b = offsetRoutePoint(route, index + 1U, -kTunnelOuterHalfWidth);
        const Vec2 right_b = offsetRoutePoint(route, index + 1U, kTunnelOuterHalfWidth);
        addQuad(mesh, {{{left_a.x, kFloorY, left_a.y},
            {left_b.x, kFloorY, left_b.y},
            {right_b.x, kFloorY, right_b.y},
            {right_a.x, kFloorY, right_a.y}}});
    }
}

float pointSegmentParameter(Vec2 point, Vec2 start, Vec2 end) {
    const float dx = end.x - start.x;
    const float dz = end.y - start.y;
    const float length_squared = dx * dx + dz * dz;
    return length_squared <= 0.0001F ? -1.0F :
        ((point.x - start.x) * dx + (point.y - start.y) * dz) / length_squared;
}

float pointSegmentDistance(Vec2 point, Vec2 start, Vec2 end, float parameter) {
    return std::hypot(point.x - (start.x + (end.x - start.x) * parameter),
        point.y - (start.y + (end.y - start.y) * parameter));
}

} // namespace

float tunnelDryCeiling(const TankDesign& tank) {
    const float tank_glass_top =
        static_cast<float>(tank.height_steps * kVerticalStepWorldUnits) - kGlassTopInset;
    return std::max(0.0F,
        std::min(kTunnelNominalCrown - kGlassThickness, tank_glass_top - kGlassThickness));
}

void appendTunnelPerimeterGlass(
    SemanticMesh& glass, const std::vector<Vec2>& tank_boundary,
    float bottom, float top, const std::vector<ResolvedTunnel>& tunnels) {
    std::vector<Vec2> portals;
    for (const ResolvedTunnel& tunnel : tunnels) {
        portals.push_back(tunnel.route_local_world.front());
        portals.push_back(tunnel.route_local_world.back());
    }
    for (std::size_t index = 0; index < tank_boundary.size(); ++index) {
        const Vec2 start = tank_boundary[index];
        const Vec2 end = tank_boundary[(index + 1U) % tank_boundary.size()];
        const float length = std::max(0.0001F, std::hypot(end.x - start.x, end.y - start.y));
        std::vector<std::pair<float, float>> cuts;
        for (const Vec2 portal : portals) {
            const float parameter = pointSegmentParameter(portal, start, end);
            if (parameter < -0.001F || parameter > 1.001F ||
                pointSegmentDistance(portal, start, end, parameter) > 0.01F) continue;
            const float half = kTunnelOuterHalfWidth / length;
            cuts.emplace_back(std::max(0.0F, parameter - half),
                std::min(1.0F, parameter + half));
        }
        std::sort(cuts.begin(), cuts.end());
        float cursor = 0.0F;
        const auto emit = [&](float from, float to) {
            if (to - from <= 0.0001F) return;
            const Vec2 a{start.x + (end.x - start.x) * from,
                start.y + (end.y - start.y) * from};
            const Vec2 b{start.x + (end.x - start.x) * to,
                start.y + (end.y - start.y) * to};
            addQuad(glass, {{{b.x, bottom, b.y}, {a.x, bottom, a.y},
                {a.x, top, a.y}, {b.x, top, b.y}}});
        };
        for (const auto cut : cuts) {
            emit(cursor, cut.first);
            cursor = std::max(cursor, cut.second);
        }
        emit(cursor, 1.0F);
    }
}

void appendTunnelMeshes(
    SemanticMesh& structure, SemanticMesh& glass, const TankDesign& tank,
    const std::vector<ResolvedTunnel>& tunnels) {
    const float inner_crown = tunnelDryCeiling(tank);
    const float outer_crown = inner_crown + kGlassThickness;
    const auto inner = archProfile(kTunnelInnerHalfWidth, inner_crown);
    const auto outer = archProfile(kTunnelOuterHalfWidth, outer_crown);
    for (const ResolvedTunnel& tunnel : tunnels) {
        appendProfileSweep(glass, tunnel.route_local_world, inner, outer);
        appendFloorStrip(structure, tunnel.route_local_world);
    }
}

} // namespace pr::aquarium::geometry::detail
