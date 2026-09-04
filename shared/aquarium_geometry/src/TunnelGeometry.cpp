#include "TunnelGeometry.hpp"

#include "TunnelFloorGeometry.hpp"

#include "aquarium_geometry/Kernel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <utility>
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
using CellKey = std::pair<std::int32_t, std::int32_t>;

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

void addTriangle(SemanticMesh& mesh, const std::array<Vec3, 3>& points) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    const Vec3 normal = normalized(cross(
        subtract(points[1], points[0]), subtract(points[2], points[0])));
    constexpr std::array<Vec2, 3> uv{{{0, 0}, {1, 0}, {0, 1}}};
    for (std::size_t index = 0; index < points.size(); ++index) {
        mesh.vertices.push_back({points[index], normal, uv[index]});
    }
    mesh.indices.insert(mesh.indices.end(), {base, base + 1U, base + 2U});
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

std::set<CellKey> sharedJunctionCells(const std::vector<ResolvedTunnel>& tunnels) {
    std::map<CellKey, std::size_t> route_counts;
    for (const ResolvedTunnel& tunnel : tunnels) {
        std::set<CellKey> route_cells;
        for (const GridCell cell : tunnel.cells) {
            route_cells.emplace(cell.column, cell.row);
        }
        for (const CellKey cell : route_cells) ++route_counts[cell];
    }
    std::set<CellKey> junctions;
    for (const auto& [cell, count] : route_counts) {
        if (count > 1U) junctions.insert(cell);
    }
    return junctions;
}

void appendTunnelRunsOutsideJunctions(
    SemanticMesh& glass,
    const FootprintDesign& footprint,
    const ResolvedTunnel& tunnel,
    const std::set<CellKey>& junctions,
    const std::vector<ProfilePoint>& inner,
    const std::vector<ProfilePoint>& outer) {
    const bool touches_junction = std::any_of(tunnel.cells.begin(), tunnel.cells.end(),
        [&](GridCell cell) { return junctions.count({cell.column, cell.row}) != 0U; });
    if (!touches_junction) {
        appendProfileSweep(glass, tunnel.route_local_world, inner, outer);
        return;
    }
    std::vector<Vec2> run;
    const auto flush = [&]() {
        if (run.size() >= 2U) appendProfileSweep(glass, run, inner, outer);
        run.clear();
    };
    for (const GridCell cell : tunnel.cells) {
        if (junctions.count({cell.column, cell.row}) != 0U) {
            flush();
        } else {
            run.push_back(localWalkingCellCentre(footprint, cell));
        }
    }
    flush();
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
        const bool a_at_top = std::abs(bottom_a.y - glass_top) <= 0.0001F;
        const bool b_at_top = std::abs(bottom_b.y - glass_top) <= 0.0001F;
        if (a_at_top && b_at_top) continue;
        if (a_at_top) {
            addTriangle(glass, {{bottom_b, bottom_a, top_b}});
        } else if (b_at_top) {
            addTriangle(glass, {{bottom_b, bottom_a, top_a}});
        } else {
            addQuad(glass, {{bottom_b, bottom_a, top_a, top_b}});
        }
    }
}

void appendJunctionChamber(
    SemanticMesh& glass,
    const FootprintDesign& footprint,
    GridCell junction,
    const std::vector<ResolvedTunnel>& tunnels,
    const std::vector<ProfilePoint>& inner,
    const std::vector<ProfilePoint>& outer,
    float inner_crown,
    float outer_crown) {
    const Vec2 center = localWalkingCellCentre(footprint, junction);
    std::set<CellKey> directions;
    for (const ResolvedTunnel& tunnel : tunnels) {
        for (std::size_t index = 0; index < tunnel.cells.size(); ++index) {
            if (tunnel.cells[index].column != junction.column ||
                tunnel.cells[index].row != junction.row) continue;
            if (index > 0U) {
                directions.emplace(
                    tunnel.cells[index - 1U].column - junction.column,
                    tunnel.cells[index - 1U].row - junction.row);
            }
            if (index + 1U < tunnel.cells.size()) {
                directions.emplace(
                    tunnel.cells[index + 1U].column - junction.column,
                    tunnel.cells[index + 1U].row - junction.row);
            }
        }
    }

    const float half = kTunnelOuterHalfWidth;
    addQuad(glass, {{{center.x - half, outer_crown, center.y - half},
        {center.x + half, outer_crown, center.y - half},
        {center.x + half, outer_crown, center.y + half},
        {center.x - half, outer_crown, center.y + half}}});
    addQuad(glass, {{{center.x - half + kGlassThickness, inner_crown, center.y - half + kGlassThickness},
        {center.x - half + kGlassThickness, inner_crown, center.y + half - kGlassThickness},
        {center.x + half - kGlassThickness, inner_crown, center.y + half - kGlassThickness},
        {center.x + half - kGlassThickness, inner_crown, center.y - half + kGlassThickness}}});

    constexpr std::array<GridCell, 4> kSides{{
        {0, -1}, {1, 0}, {0, 1}, {-1, 0},
    }};
    for (const GridCell direction : kSides) {
        const Vec2 edge_center{
            center.x + static_cast<float>(direction.column) * half,
            center.y + static_cast<float>(direction.row) * half};
        if (directions.count({direction.column, direction.row}) != 0U) {
            appendPortalGlassCap(glass, edge_center, direction, outer, outer_crown);
            appendPortalGlassCap(glass, edge_center, direction, inner, inner_crown);
            continue;
        }
        const Vec2 tangent{
            static_cast<float>(-direction.row),
            static_cast<float>(direction.column)};
        const Vec3 outer_left{edge_center.x - tangent.x * half, 0.0F,
            edge_center.y - tangent.y * half};
        const Vec3 outer_right{edge_center.x + tangent.x * half, 0.0F,
            edge_center.y + tangent.y * half};
        addQuad(glass, {{outer_right, outer_left,
            {outer_left.x, outer_crown, outer_left.z},
            {outer_right.x, outer_crown, outer_right.z}}});
    }
}

Vec3 portalProfilePoint(
    Vec2 portal, GridCell outward, ProfilePoint profile, float outward_offset) {
    const Vec2 tangent{
        static_cast<float>(-outward.row),
        static_cast<float>(outward.column)};
    return {
        portal.x + tangent.x * profile.lateral +
            static_cast<float>(outward.column) * outward_offset,
        profile.y,
        portal.y + tangent.y * profile.lateral +
            static_cast<float>(outward.row) * outward_offset,
    };
}

void appendPortalFrame(
    SemanticMesh& frame, Vec2 portal, GridCell outward,
    const std::vector<ProfilePoint>& opening) {
    constexpr float kPortalFrameWidth = 1.6F;
    constexpr float kPortalFaceOffset = 0.08F;
    const auto outside = archProfile(
        kTunnelOuterHalfWidth + kPortalFrameWidth,
        kTunnelNominalCrown + kGlassThickness + kPortalFrameWidth);
    for (std::size_t index = 0; index + 1U < opening.size(); ++index) {
        const Vec3 inner_a = portalProfilePoint(
            portal, outward, opening[index], kPortalFaceOffset);
        const Vec3 inner_b = portalProfilePoint(
            portal, outward, opening[index + 1U], kPortalFaceOffset);
        const Vec3 outer_a = portalProfilePoint(
            portal, outward, outside[index], kPortalFaceOffset);
        const Vec3 outer_b = portalProfilePoint(
            portal, outward, outside[index + 1U], kPortalFaceOffset);
        addQuad(frame, {{inner_a, inner_b, outer_b, outer_a}});
        addQuad(frame, {{outer_a, outer_b, inner_b, inner_a}});
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
        if (tunnel.entry_outward) portals.push_back(tunnel.route_local_world.front());
        if (tunnel.exit_outward) portals.push_back(tunnel.route_local_world.back());
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
    const auto junctions = sharedJunctionCells(tunnels);
    for (const ResolvedTunnel& tunnel : tunnels) {
        appendTunnelRunsOutsideJunctions(
            glass, tank.footprint, tunnel, junctions, inner, outer);
        if (tunnel.entry_outward) {
            appendPortalFrame(tunnel_frame, tunnel.route_local_world.front(),
                *tunnel.entry_outward, outer);
        }
        if (tunnel.exit_outward) {
            appendPortalFrame(tunnel_frame, tunnel.route_local_world.back(),
                *tunnel.exit_outward, outer);
        }
        if (tunnel.entry_outward) {
            appendPortalGlassCap(glass, tunnel.route_local_world.front(),
                *tunnel.entry_outward, outer, tank.height_steps * kVerticalStepWorldUnits -
                    kGlassTopInset);
        }
        if (tunnel.exit_outward) {
            appendPortalGlassCap(glass, tunnel.route_local_world.back(),
                *tunnel.exit_outward, outer, tank.height_steps * kVerticalStepWorldUnits -
                    kGlassTopInset);
        }
    }
    if (tank.depth_steps > 0) {
        appendTunnelGlassFloors(tunnel_frame, glass, tank.footprint, tunnels);
    }
    for (const auto [column, row] : junctions) {
        appendJunctionChamber(glass, tank.footprint, {column, row}, tunnels,
            inner, outer, inner_crown, outer_crown);
    }
}

} // namespace pr::aquarium::geometry::detail
