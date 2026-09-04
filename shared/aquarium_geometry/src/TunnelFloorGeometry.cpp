#include "TunnelFloorGeometry.hpp"

#include "aquarium_geometry/Kernel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace pr::aquarium::geometry::detail {
namespace {

constexpr float kTunnelOuterHalfWidth =
    static_cast<float>(kTunnelOuterHalfWidthWorldUnits);
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
        {start_right.x, top, start_right.y}, {end_right.x, top, end_right.y},
        {end_left.x, top, end_left.y}}});
    addQuad(frame, {{{end_left.x, bottom, end_left.y},
        {end_right.x, bottom, end_right.y}, {start_right.x, bottom, start_right.y},
        {start_left.x, bottom, start_left.y}}});
    addQuad(frame, {{{start_left.x, bottom, start_left.y},
        {start_left.x, top, start_left.y}, {end_left.x, top, end_left.y},
        {end_left.x, bottom, end_left.y}}});
    addQuad(frame, {{{start_right.x, bottom, start_right.y},
        {end_right.x, bottom, end_right.y}, {end_right.x, top, end_right.y},
        {start_right.x, top, start_right.y}}});
    addQuad(frame, {{{start_left.x, bottom, start_left.y},
        {start_right.x, bottom, start_right.y}, {start_right.x, top, start_right.y},
        {start_left.x, top, start_left.y}}});
    addQuad(frame, {{{end_left.x, bottom, end_left.y},
        {end_left.x, top, end_left.y}, {end_right.x, top, end_right.y},
        {end_right.x, bottom, end_right.y}}});
}

void appendGlassFloorPanels(
    SemanticMesh& frame, SemanticMesh& glass,
    const FootprintDesign& footprint, const ResolvedTunnel& tunnel,
    const std::set<CellKey>& junctions) {
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
        const GridCell start_cell = tunnel.cells[index];
        const GridCell end_cell = tunnel.cells[index + 1U];
        if (junctions.count({start_cell.column, start_cell.row}) != 0U ||
            junctions.count({end_cell.column, end_cell.row}) != 0U) continue;
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

void appendJunctionFloor(
    SemanticMesh& frame, SemanticMesh& glass,
    const FootprintDesign& footprint, GridCell junction) {
    constexpr float kGlassFloorY = 0.03F;
    constexpr float kFrameBottomY = 0.015F;
    constexpr float kSeparatorTopY = 0.24F;
    constexpr float kSeparatorWidth = 0.8F;
    constexpr float kPanelInset = 0.46F;
    const Vec2 center = localWalkingCellCentre(footprint, junction);
    const float half = kTunnelOuterHalfWidth - kPanelInset;
    addQuad(glass, {{{center.x - half, kGlassFloorY, center.y - half},
        {center.x + half, kGlassFloorY, center.y - half},
        {center.x + half, kGlassFloorY, center.y + half},
        {center.x - half, kGlassFloorY, center.y + half}}});
    appendRailBox(frame,
        {center.x - kTunnelOuterHalfWidth, center.y},
        {center.x + kTunnelOuterHalfWidth, center.y},
        kSeparatorWidth, kFrameBottomY, kSeparatorTopY);
    appendRailBox(frame,
        {center.x, center.y - kTunnelOuterHalfWidth},
        {center.x, center.y + kTunnelOuterHalfWidth},
        kSeparatorWidth, kFrameBottomY, kSeparatorTopY);
}

} // namespace

void appendTunnelGlassFloors(
    SemanticMesh& frame, SemanticMesh& glass,
    const FootprintDesign& footprint,
    const std::vector<ResolvedTunnel>& tunnels) {
    const auto junctions = sharedJunctionCells(tunnels);
    for (const ResolvedTunnel& tunnel : tunnels) {
        appendGlassFloorPanels(frame, glass, footprint, tunnel, junctions);
    }
    for (const auto [column, row] : junctions) {
        appendJunctionFloor(frame, glass, footprint, {column, row});
    }
}

} // namespace pr::aquarium::geometry::detail
