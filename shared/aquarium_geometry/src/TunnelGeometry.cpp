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
constexpr float kTunnelOuterHalfWidth =
    static_cast<float>(kTunnelOuterHalfWidthWorldUnits);
constexpr float kTunnelInnerHalfWidth = kTunnelOuterHalfWidth - kGlassThickness;
constexpr float kTunnelNominalCrown = static_cast<float>(kTunnelCrownWorldUnits);
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

Vec2 localWalkingCellCentre(const FootprintDesign& footprint, GridCell cell) {
    return {
        cellCentreWorld(cell.column) -
            footprintCentreWorld(footprint.origin_cell.column, occupiedWidthCells(footprint)) -
            static_cast<float>(kPlacementOffsetWorldUnits),
        cellCentreWorld(cell.row) -
            footprintCentreWorld(footprint.origin_cell.row, occupiedDepthCells(footprint)) -
            static_cast<float>(kPlacementOffsetWorldUnits),
    };
}

void appendRailBox(
    SemanticMesh& frame, Vec2 start, Vec2 end, float width,
    float bottom, float top) {
    const float dx = end.x - start.x;
    const float dz = end.y - start.y;
    const float length = std::max(0.0001F, std::hypot(dx, dz));
    const Vec2 lateral{-dz / length * width * 0.5F,
        dx / length * width * 0.5F};
    const Vec2 start_left{start.x - lateral.x, start.y - lateral.y};
    const Vec2 start_right{start.x + lateral.x, start.y + lateral.y};
    const Vec2 end_right{end.x + lateral.x, end.y + lateral.y};
    const Vec2 end_left{end.x - lateral.x, end.y - lateral.y};
    addQuad(frame, {{{start_left.x, top, start_left.y},
        {start_right.x, top, start_right.y},
        {end_right.x, top, end_right.y},
        {end_left.x, top, end_left.y}}});
    addQuad(frame, {{{end_left.x, bottom, end_left.y},
        {end_right.x, bottom, end_right.y},
        {start_right.x, bottom, start_right.y},
        {start_left.x, bottom, start_left.y}}});
    addQuad(frame, {{{start_left.x, bottom, start_left.y},
        {start_left.x, top, start_left.y},
        {end_left.x, top, end_left.y},
        {end_left.x, bottom, end_left.y}}});
    addQuad(frame, {{{start_right.x, bottom, start_right.y},
        {end_right.x, bottom, end_right.y},
        {end_right.x, top, end_right.y},
        {start_right.x, top, start_right.y}}});
    addQuad(frame, {{{start_left.x, bottom, start_left.y},
        {start_right.x, bottom, start_right.y},
        {start_right.x, top, start_right.y},
        {start_left.x, top, start_left.y}}});
    addQuad(frame, {{{end_left.x, bottom, end_left.y},
        {end_left.x, top, end_left.y},
        {end_right.x, top, end_right.y},
        {end_right.x, bottom, end_right.y}}});
}

void appendGlassFloorPanels(
    SemanticMesh& frame,
    SemanticMesh& glass,
    const FootprintDesign& footprint,
    const ResolvedTunnel& tunnel) {
    constexpr float kGlassFloorY = 0.03F;
    constexpr float kFrameBottomY = 0.015F;
    constexpr float kSideRailTopY = 1.36F;
    constexpr float kSeparatorTopY = 0.24F;
    constexpr float kSideRailWidth = 1.6F;
    constexpr float kSeparatorWidth = 0.8F;
    constexpr float kPanelGap = 0.92F;
    std::vector<Vec2> route;
    route.reserve(tunnel.cells.size());
    for (const GridCell cell : tunnel.cells) {
        route.push_back(localWalkingCellCentre(footprint, cell));
    }
    for (std::size_t index = 0; index + 1U < route.size(); ++index) {
        const Vec2 a = route[index];
        const Vec2 b = route[index + 1U];
        const float dx = b.x - a.x;
        const float dz = b.y - a.y;
        const float length = std::max(0.0001F, std::hypot(dx, dz));
        const Vec2 direction{dx / length, dz / length};
        const Vec2 lateral{-direction.y, direction.x};
        const Vec2 start{a.x + direction.x * kPanelGap * 0.5F,
            a.y + direction.y * kPanelGap * 0.5F};
        const Vec2 end{b.x - direction.x * kPanelGap * 0.5F,
            b.y - direction.y * kPanelGap * 0.5F};
        const float panel_half_width = kTunnelOuterHalfWidth - kSideRailWidth;
        const Vec2 left_start{start.x - lateral.x * panel_half_width,
            start.y - lateral.y * panel_half_width};
        const Vec2 right_start{start.x + lateral.x * panel_half_width,
            start.y + lateral.y * panel_half_width};
        const Vec2 left_end{end.x - lateral.x * panel_half_width,
            end.y - lateral.y * panel_half_width};
        const Vec2 right_end{end.x + lateral.x * panel_half_width,
            end.y + lateral.y * panel_half_width};
        addQuad(glass, {{{left_start.x, kGlassFloorY, left_start.y},
            {right_start.x, kGlassFloorY, right_start.y},
            {right_end.x, kGlassFloorY, right_end.y},
            {left_end.x, kGlassFloorY, left_end.y}}});

        const Vec2 left_rail_a{a.x - lateral.x * (kTunnelOuterHalfWidth - kSideRailWidth * 0.5F),
            a.y - lateral.y * (kTunnelOuterHalfWidth - kSideRailWidth * 0.5F)};
        const Vec2 left_rail_b{b.x - lateral.x * (kTunnelOuterHalfWidth - kSideRailWidth * 0.5F),
            b.y - lateral.y * (kTunnelOuterHalfWidth - kSideRailWidth * 0.5F)};
        const Vec2 right_rail_a{a.x + lateral.x * (kTunnelOuterHalfWidth - kSideRailWidth * 0.5F),
            a.y + lateral.y * (kTunnelOuterHalfWidth - kSideRailWidth * 0.5F)};
        const Vec2 right_rail_b{b.x + lateral.x * (kTunnelOuterHalfWidth - kSideRailWidth * 0.5F),
            b.y + lateral.y * (kTunnelOuterHalfWidth - kSideRailWidth * 0.5F)};
        appendRailBox(frame, left_rail_a, left_rail_b, kSideRailWidth,
            kFrameBottomY, kSideRailTopY);
        appendRailBox(frame, right_rail_a, right_rail_b, kSideRailWidth,
            kFrameBottomY, kSideRailTopY);

        const Vec2 cross_a{a.x - lateral.x * kTunnelOuterHalfWidth,
            a.y - lateral.y * kTunnelOuterHalfWidth};
        const Vec2 cross_b{a.x + lateral.x * kTunnelOuterHalfWidth,
            a.y + lateral.y * kTunnelOuterHalfWidth};
        appendRailBox(frame, cross_a, cross_b, kSeparatorWidth,
            kFrameBottomY, kSeparatorTopY);
        if (index + 2U == route.size()) {
            const Vec2 end_cross_a{b.x - lateral.x * kTunnelOuterHalfWidth,
                b.y - lateral.y * kTunnelOuterHalfWidth};
            const Vec2 end_cross_b{b.x + lateral.x * kTunnelOuterHalfWidth,
                b.y + lateral.y * kTunnelOuterHalfWidth};
            appendRailBox(frame, end_cross_a, end_cross_b, kSeparatorWidth,
                kFrameBottomY, kSeparatorTopY);
        }
    }
}

void appendPortalGlassCap(
    SemanticMesh& glass,
    Vec2 portal,
    GridCell outward,
    const std::vector<ProfilePoint>& outer,
    float glass_top) {
    const Vec2 tangent{
        static_cast<float>(-outward.row),
        static_cast<float>(outward.column)};
    for (std::size_t index = 0; index + 1U < outer.size(); ++index) {
        const ProfilePoint a = outer[index];
        const ProfilePoint b = outer[index + 1U];
        if (std::abs(a.lateral - b.lateral) <= 0.0001F) continue;
        const Vec3 bottom_a{
            portal.x + tangent.x * a.lateral, a.y,
            portal.y + tangent.y * a.lateral};
        const Vec3 bottom_b{
            portal.x + tangent.x * b.lateral, b.y,
            portal.y + tangent.y * b.lateral};
        const Vec3 top_a{bottom_a.x, glass_top, bottom_a.z};
        const Vec3 top_b{bottom_b.x, glass_top, bottom_b.z};
        addQuad(glass, {{bottom_b, bottom_a, top_a, top_b}});
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
    SemanticMesh& tunnel_frame, SemanticMesh& glass, const TankDesign& tank,
    const std::vector<ResolvedTunnel>& tunnels) {
    const float inner_crown = tunnelDryCeiling(tank);
    const float outer_crown = inner_crown + kGlassThickness;
    const auto inner = archProfile(kTunnelInnerHalfWidth, inner_crown);
    const auto outer = archProfile(kTunnelOuterHalfWidth, outer_crown);
    for (const ResolvedTunnel& tunnel : tunnels) {
        appendProfileSweep(glass, tunnel.route_local_world, inner, outer);
        if (tank.depth_steps > 0) {
            appendGlassFloorPanels(
                tunnel_frame, glass, tank.footprint, tunnel);
        }
        appendPortalGlassCap(glass, tunnel.route_local_world.front(),
            tunnel.entry_outward, outer, tank.height_steps * kVerticalStepWorldUnits -
                kGlassTopInset);
        appendPortalGlassCap(glass, tunnel.route_local_world.back(),
            tunnel.exit_outward, outer, tank.height_steps * kVerticalStepWorldUnits -
                kGlassTopInset);
    }
}

} // namespace pr::aquarium::geometry::detail
