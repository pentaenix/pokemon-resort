#include "gameplay/world3d/aquarium/construction/AquariumConstructionVisual.hpp"

#include "aquarium_geometry/Kernel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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

geo::GridCell visualTankCentreCell(const geo::TankDesign& tank) {
    return {
        tank.footprint.origin_cell.column +
            (geo::occupiedWidthCells(tank.footprint) - 1) / 2,
        tank.footprint.origin_cell.row +
            (geo::occupiedDepthCells(tank.footprint) - 1) / 2,
    };
}

float floorForCell(const AquariumConstructionVisual& visual, geo::GridCell cell) {
    const auto found = std::find_if(visual.cells.begin(), visual.cells.end(),
        [&](const auto& surface) { return sameCell(surface.cell, cell); });
    return found == visual.cells.end() ? 0.0f : found->floor_y;
}

void appendQuad(
    ConstructionVisualMesh& mesh,
    float x0, float z0, float x1, float z1, float y,
    std::uint32_t color) {
    if (mesh.vertices.size() > std::numeric_limits<std::uint16_t>::max() - 4U) return;
    const auto first = static_cast<std::uint16_t>(mesh.vertices.size());
    mesh.vertices.push_back({x0, y, z0, color});
    mesh.vertices.push_back({x1, y, z0, color});
    mesh.vertices.push_back({x1, y, z1, color});
    mesh.vertices.push_back({x0, y, z1, color});
    const std::uint16_t indices[]{
        first, static_cast<std::uint16_t>(first + 1U), static_cast<std::uint16_t>(first + 2U),
        first, static_cast<std::uint16_t>(first + 2U), static_cast<std::uint16_t>(first + 3U),
    };
    mesh.indices.insert(mesh.indices.end(), std::begin(indices), std::end(indices));
}

void appendBorder(
    ConstructionVisualMesh& mesh,
    float x0, float z0, float x1, float z1, float y,
    float thickness, std::uint32_t color) {
    appendQuad(mesh, x0, z0, x1, z0 + thickness, y, color);
    appendQuad(mesh, x0, z1 - thickness, x1, z1, y, color);
    appendQuad(mesh, x0, z0 + thickness, x0 + thickness, z1 - thickness, y, color);
    appendQuad(mesh, x1 - thickness, z0 + thickness, x1, z1 - thickness, y, color);
}

void appendDiamond(
    ConstructionVisualMesh& mesh,
    float center_x, float center_z, float y, float radius,
    std::uint32_t color) {
    if (mesh.vertices.size() > std::numeric_limits<std::uint16_t>::max() - 4U) return;
    const auto first = static_cast<std::uint16_t>(mesh.vertices.size());
    mesh.vertices.push_back({center_x, y, center_z - radius, color});
    mesh.vertices.push_back({center_x + radius, y, center_z, color});
    mesh.vertices.push_back({center_x, y, center_z + radius, color});
    mesh.vertices.push_back({center_x - radius, y, center_z, color});
    const std::uint16_t indices[]{
        first, static_cast<std::uint16_t>(first + 1U), static_cast<std::uint16_t>(first + 2U),
        first, static_cast<std::uint16_t>(first + 2U), static_cast<std::uint16_t>(first + 3U),
    };
    mesh.indices.insert(mesh.indices.end(), std::begin(indices), std::end(indices));
}

void appendLine(
    ConstructionVisualMesh& mesh,
    float x0, float z0, float x1, float z1, float y,
    float thickness, std::uint32_t color) {
    const float dx = x1 - x0;
    const float dz = z1 - z0;
    const float length = std::hypot(dx, dz);
    if (length <= 0.001f) return;
    const float nx = -dz / length * thickness * 0.5f;
    const float nz = dx / length * thickness * 0.5f;
    if (mesh.vertices.size() > std::numeric_limits<std::uint16_t>::max() - 4U) return;
    const auto first = static_cast<std::uint16_t>(mesh.vertices.size());
    mesh.vertices.push_back({x0 + nx, y, z0 + nz, color});
    mesh.vertices.push_back({x1 + nx, y, z1 + nz, color});
    mesh.vertices.push_back({x1 - nx, y, z1 - nz, color});
    mesh.vertices.push_back({x0 - nx, y, z0 - nz, color});
    mesh.indices.insert(mesh.indices.end(), {
        first, static_cast<std::uint16_t>(first + 1U), static_cast<std::uint16_t>(first + 2U),
        first, static_cast<std::uint16_t>(first + 2U), static_cast<std::uint16_t>(first + 3U),
    });
}

struct ScreenPoint {
    float x = 0.0f;
    float y = 0.0f;
};

float edge(ScreenPoint a, ScreenPoint b, ScreenPoint point) {
    return (point.x - a.x) * (b.y - a.y) - (point.y - a.y) * (b.x - a.x);
}

bool pointInTriangle(ScreenPoint point, ScreenPoint a, ScreenPoint b, ScreenPoint c) {
    const float e0 = edge(a, b, point);
    const float e1 = edge(b, c, point);
    const float e2 = edge(c, a, point);
    const bool negative = e0 < 0.0f || e1 < 0.0f || e2 < 0.0f;
    const bool positive = e0 > 0.0f || e1 > 0.0f || e2 > 0.0f;
    return !(negative && positive);
}

struct GizmoWorldPoint {
    ConstructionGizmoHit hit;
    gameplay::world3d::camera::Vec3 world;
};

std::vector<GizmoWorldPoint> gizmoWorldPoints(const AquariumConstructionVisual& visual) {
    const auto tank = visual.preview_tank ? visual.preview_tank : visual.selected_tank;
    if (!tank) return {};
    const auto& footprint = tank->footprint;
    const float tile = visual.tile_world_units;
    const float offset = visual.placement_offset_world_units;
    const float west = static_cast<float>(footprint.origin_cell.column) * tile + offset;
    const float north = static_cast<float>(footprint.origin_cell.row) * tile + offset;
    const float east = west + static_cast<float>(geo::occupiedWidthCells(footprint)) * tile;
    const float south = north + static_cast<float>(geo::occupiedDepthCells(footprint)) * tile;
    const float center_x = (west + east) * 0.5f;
    const float center_z = (north + south) * 0.5f;
    const float floor_y = floorForCell(visual, visualTankCentreCell(*tank));
    const float y = floor_y + 0.72f;
    std::vector<GizmoWorldPoint> points;
    if (visual.state != ConstructionState::Selected) return {};
    points = {
        {{ConstructionGizmoKind::Move, AquariumResizeHandle::SouthEast, std::nullopt}, {center_x, y, center_z}},
        {{ConstructionGizmoKind::Resize, AquariumResizeHandle::NorthWest, std::nullopt}, {west, y, north}},
        {{ConstructionGizmoKind::Resize, AquariumResizeHandle::North, std::nullopt}, {center_x, y, north}},
        {{ConstructionGizmoKind::Resize, AquariumResizeHandle::NorthEast, std::nullopt}, {east, y, north}},
        {{ConstructionGizmoKind::Resize, AquariumResizeHandle::East, std::nullopt}, {east, y, center_z}},
        {{ConstructionGizmoKind::Resize, AquariumResizeHandle::SouthEast, std::nullopt}, {east, y, south}},
        {{ConstructionGizmoKind::Resize, AquariumResizeHandle::South, std::nullopt}, {center_x, y, south}},
        {{ConstructionGizmoKind::Resize, AquariumResizeHandle::SouthWest, std::nullopt}, {west, y, south}},
        {{ConstructionGizmoKind::Resize, AquariumResizeHandle::West, std::nullopt}, {west, y, center_z}},
    };
    points.push_back({{ConstructionGizmoKind::Height, AquariumResizeHandle::SouthEast,
        std::nullopt}, {center_x, floor_y + tank->height_steps * geo::kVerticalStepWorldUnits,
        center_z}});
    for (const auto& corner : geo::footprintCorners(footprint)) {
        if (!corner.convex) continue;
        const float vertex_x = west + corner.vertex.column * tile;
        const float vertex_z = north + corner.vertex.row * tile;
        const float toward_center_x = center_x - vertex_x;
        const float toward_center_z = center_z - vertex_z;
        const float toward_center_length = std::max(0.001f,
            std::hypot(toward_center_x, toward_center_z));
        constexpr float kCornerKnobInset = 5.0f;
        points.push_back({{ConstructionGizmoKind::CornerRadius,
            AquariumResizeHandle::SouthEast, corner.vertex},
            {vertex_x + toward_center_x / toward_center_length * kCornerKnobInset,
             y + 0.18f,
             vertex_z + toward_center_z / toward_center_length * kCornerKnobInset}});
    }
    return points;
}

void appendCellCross(
    ConstructionVisualMesh& mesh, float x0, float z0, float tile, float y,
    std::uint32_t color) {
    appendQuad(mesh, x0 + 2.0f, z0 + tile * 0.43f,
        x0 + tile - 2.0f, z0 + tile * 0.57f, y, color);
    appendQuad(mesh, x0 + tile * 0.43f, z0 + 2.0f,
        x0 + tile * 0.57f, z0 + tile - 2.0f, y + 0.01f, color);
}

} // namespace

ConstructionVisualMesh buildAquariumConstructionWorldMesh(
    const AquariumConstructionVisual& visual) {
    ConstructionVisualMesh mesh;
    if (!visual.visible || visual.cells.empty()) return mesh;
    const float tile = visual.tile_world_units;
    constexpr std::uint32_t kAllowedFill = colorAbgr(244, 194, 55, 88);
    constexpr std::uint32_t kBlockedFill = colorAbgr(66, 64, 58, 150);
    constexpr std::uint32_t kValidFill = colorAbgr(62, 210, 140, 145);
    constexpr std::uint32_t kInvalidFill = colorAbgr(224, 67, 73, 170);
    constexpr std::uint32_t kLockedFill = colorAbgr(85, 68, 42, 190);
    constexpr std::uint32_t kLockedMark = colorAbgr(255, 177, 48, 255);
    constexpr std::uint32_t kSelected = colorAbgr(65, 222, 255, 255);
    constexpr std::uint32_t kOriginal = colorAbgr(76, 130, 255, 145);
    constexpr std::uint32_t kCursor = colorAbgr(255, 250, 184, 255);
    constexpr std::uint32_t kAnchor = colorAbgr(255, 197, 45, 255);
    constexpr std::uint32_t kHandle = colorAbgr(255, 255, 255, 255);
    constexpr std::uint32_t kCutFill = colorAbgr(71, 57, 104, 185);
    constexpr std::uint32_t kCutMark = colorAbgr(248, 214, 86, 255);

    const float offset = visual.placement_offset_world_units;

    for (const ConstructionCellSurface& surface : visual.cells) {
        const float x0 = static_cast<float>(surface.cell.column) * tile + offset;
        const float z0 = static_cast<float>(surface.cell.row) * tile + offset;
        const float y = surface.floor_y + 0.16f;
        appendQuad(mesh, x0 + 2.0f, z0 + 2.0f, x0 + tile - 2.0f, z0 + tile - 2.0f,
            y, surface.blocked ? kBlockedFill : kAllowedFill);
    }

    for (const geo::GridCell cell : visual.locked_cells) {
        const float x0 = static_cast<float>(cell.column) * tile + offset;
        const float z0 = static_cast<float>(cell.row) * tile + offset;
        const float y = floorForCell(visual, cell) + 0.27f;
        appendQuad(mesh, x0 + 1.0f, z0 + 1.0f, x0 + tile - 1.0f, z0 + tile - 1.0f,
            y, kLockedFill);
        appendCellCross(mesh, x0, z0, tile, y + 0.04f, kLockedMark);
    }

    for (const geo::GridCell cell : visual.original_cells) {
        const float x0 = static_cast<float>(cell.column) * tile + offset;
        const float z0 = static_cast<float>(cell.row) * tile + offset;
        appendBorder(mesh, x0 + 1.4f, z0 + 1.4f, x0 + tile - 1.4f, z0 + tile - 1.4f,
            floorForCell(visual, cell) + 0.31f, 0.55f, kOriginal);
    }

    for (const geo::GridCell cell : visual.selected_cells) {
        const float x0 = static_cast<float>(cell.column) * tile + offset;
        const float z0 = static_cast<float>(cell.row) * tile + offset;
        appendBorder(mesh, x0 + 0.8f, z0 + 0.8f, x0 + tile - 0.8f, z0 + tile - 0.8f,
            floorForCell(visual, cell) + 0.39f, 0.8f, kSelected);
    }

    for (const geo::GridCell cell : visual.draft_cells) {
        const float x0 = static_cast<float>(cell.column) * tile + offset;
        const float z0 = static_cast<float>(cell.row) * tile + offset;
        const float y = floorForCell(visual, cell) + 0.24f;
        appendQuad(mesh, x0 + 1.0f, z0 + 1.0f, x0 + tile - 1.0f, z0 + tile - 1.0f,
            y, visual.draft_valid ? kValidFill : kInvalidFill);
        if (!visual.draft_valid) {
            appendQuad(mesh, x0 + 2.0f, z0 + 6.9f, x0 + tile - 2.0f, z0 + 9.1f,
                y + 0.03f, kCursor);
            appendQuad(mesh, x0 + 6.9f, z0 + 2.0f, x0 + 9.1f, z0 + tile - 2.0f,
                y + 0.04f, kCursor);
        }
    }

    for (const geo::GridCell cell : visual.cut_cells) {
        const float x0 = static_cast<float>(cell.column) * tile + offset;
        const float z0 = static_cast<float>(cell.row) * tile + offset;
        const float y = floorForCell(visual, cell) + 0.32f;
        appendQuad(mesh, x0 + 2.2f, z0 + 2.2f, x0 + tile - 2.2f, z0 + tile - 2.2f,
            y, kCutFill);
        appendCellCross(mesh, x0, z0, tile, y + 0.03f, kCutMark);
    }

    const float cursor_x = static_cast<float>(visual.cursor.column) * tile + offset;
    const float cursor_z = static_cast<float>(visual.cursor.row) * tile + offset;
    const float cursor_y = floorForCell(visual, visual.cursor) + 0.35f;
    appendBorder(mesh, cursor_x + 1.0f, cursor_z + 1.0f,
        cursor_x + tile - 1.0f, cursor_z + tile - 1.0f, cursor_y, 0.9f, kCursor);

    if (visual.anchor) {
        const float center_x = (static_cast<float>(visual.anchor->column) + 0.5f) * tile + offset;
        const float center_z = (static_cast<float>(visual.anchor->row) + 0.5f) * tile + offset;
        appendDiamond(mesh, center_x, center_z,
            floorForCell(visual, *visual.anchor) + 0.48f, 3.1f, kAnchor);
    }
    if (visual.state == ConstructionState::ResizeFootprint ||
        visual.state == ConstructionState::MoveTank ||
        visual.state == ConstructionState::ResizeTank ||
        visual.state == ConstructionState::PaintFootprint ||
        visual.state == ConstructionState::DraftReview) {
        appendDiamond(mesh, cursor_x + tile * 0.5f, cursor_z + tile * 0.5f,
            cursor_y + 0.16f, 4.0f, kHandle);
        appendDiamond(mesh, cursor_x + tile * 0.5f, cursor_z + tile * 0.5f,
            cursor_y + 0.19f, 2.4f, visual.draft_valid ? kValidFill : kInvalidFill);
    }
    if ((visual.state == ConstructionState::Selected ||
         visual.state == ConstructionState::DraftReview) &&
        (visual.selected_tank || visual.preview_tank)) {
        for (const auto& gizmo : gizmoWorldPoints(visual)) {
            const bool move = gizmo.hit.kind == ConstructionGizmoKind::Move;
            const bool resize = gizmo.hit.kind == ConstructionGizmoKind::Resize;
            const bool height = gizmo.hit.kind == ConstructionGizmoKind::Height;
            const bool corner = gizmo.hit.kind == ConstructionGizmoKind::CornerRadius;
            const bool active_resize = resize && visual.active_resize_handle &&
                *visual.active_resize_handle == gizmo.hit.resize_handle;
            appendDiamond(mesh, gizmo.world.x, gizmo.world.z, gizmo.world.y,
                move ? 5.0f : (height ? 4.8f : (active_resize ? 5.2f : 3.6f)),
                height ? kSelected : (corner ? kAnchor :
                    (move ? kAnchor : (active_resize ? kSelected : kHandle))));
            appendDiamond(mesh, gizmo.world.x, gizmo.world.z, gizmo.world.y + 0.04f,
                move ? 2.8f : (height ? 2.6f : (active_resize ? 2.8f : 1.8f)),
                corner ? kSelected : (move ? kSelected :
                    (active_resize ? kHandle : kAnchor)));
        }
    }
    if (visual.preview_tank) {
        const auto& tank = *visual.preview_tank;
        const float center_x = geo::footprintCentreWorld(
            tank.footprint.origin_cell.column, geo::occupiedWidthCells(tank.footprint)) + offset;
        const float center_z = geo::footprintCentreWorld(
            tank.footprint.origin_cell.row, geo::occupiedDepthCells(tank.footprint)) + offset;
        const float floor_y = floorForCell(visual, visualTankCentreCell(tank));
        const auto boundary = geo::footprintBoundaryLocalWorld(
            tank.footprint, tank.corner_radius_steps, tank.corner_radii);
        for (std::size_t index = 0; index < boundary.size(); ++index) {
            const auto start = boundary[index];
            const auto end = boundary[(index + 1U) % boundary.size()];
            appendLine(mesh, center_x + start.x, center_z + start.y,
                center_x + end.x, center_z + end.y,
                floor_y + 0.62f, 0.9f, kAnchor);
        }
        const int tick_count = std::clamp(tank.height_steps - 3, 1, 9);
        const float tick_x = center_x + 2.0f;
        const float tick_z = center_z;
        for (int tick = 0; tick < tick_count; ++tick) {
            appendDiamond(mesh, tick_x, tick_z,
                floor_y + static_cast<float>((tick + 4) * geo::kVerticalStepWorldUnits),
                1.4f, tick + 1 == tick_count ? kCursor : kSelected);
        }
    }
    return mesh;
}

std::optional<geo::GridCell> hitTestAquariumConstructionCell(
    const AquariumConstructionVisual& visual,
    const gameplay::world3d::camera::Gen4FollowCamera& camera,
    int screen_x, int screen_y, int viewport_width, int viewport_height) {
    if (!visual.visible || viewport_width <= 0 || viewport_height <= 0) return std::nullopt;
    const float tile = visual.tile_world_units;
    const ScreenPoint point{static_cast<float>(screen_x), static_cast<float>(screen_y)};
    float nearest_depth = std::numeric_limits<float>::max();
    std::optional<geo::GridCell> nearest;
    for (const ConstructionCellSurface& surface : visual.cells) {
        const float x0 = static_cast<float>(surface.cell.column) * tile +
            visual.placement_offset_world_units;
        const float z0 = static_cast<float>(surface.cell.row) * tile +
            visual.placement_offset_world_units;
        const float y = surface.floor_y + 0.35f;
        const std::array<gameplay::world3d::camera::Vec3, 4> corners{{
            {x0, y, z0}, {x0 + tile, y, z0},
            {x0 + tile, y, z0 + tile}, {x0, y, z0 + tile},
        }};
        std::array<ScreenPoint, 4> projected{};
        float average_depth = 0.0f;
        bool visible = true;
        for (std::size_t index = 0; index < corners.size(); ++index) {
            float depth = 0.0f;
            visible = visible && camera.worldToScreen(
                corners[index], viewport_width, viewport_height,
                projected[index].x, projected[index].y, depth);
            average_depth += depth;
        }
        if (!visible) continue;
        if ((pointInTriangle(point, projected[0], projected[1], projected[2]) ||
             pointInTriangle(point, projected[0], projected[2], projected[3])) &&
            average_depth < nearest_depth) {
            nearest_depth = average_depth;
            nearest = surface.cell;
        }
    }
    return nearest;
}

std::optional<ConstructionGizmoHit> hitTestAquariumConstructionGizmo(
    const AquariumConstructionVisual& visual,
    const gameplay::world3d::camera::Gen4FollowCamera& camera,
    int screen_x, int screen_y, int viewport_width, int viewport_height) {
    if (!visual.visible ||
        (visual.state != ConstructionState::Selected &&
         visual.state != ConstructionState::DraftReview) ||
        viewport_width <= 0 || viewport_height <= 0) return std::nullopt;
    constexpr float kHitRadiusSquared = 26.0f * 26.0f;
    float nearest_distance = kHitRadiusSquared;
    std::optional<ConstructionGizmoHit> nearest;
    for (const auto& gizmo : gizmoWorldPoints(visual)) {
        float projected_x = 0.0f;
        float projected_y = 0.0f;
        float depth = 0.0f;
        if (!camera.worldToScreen(gizmo.world, viewport_width, viewport_height,
                projected_x, projected_y, depth)) continue;
        const float dx = projected_x - static_cast<float>(screen_x);
        const float dy = projected_y - static_cast<float>(screen_y);
        const float distance = dx * dx + dy * dy;
        if (distance <= nearest_distance) {
            nearest_distance = distance;
            nearest = gizmo.hit;
        }
    }
    return nearest;
}

std::vector<geo::GridCell> aquariumConstructionContextLockedCells(
    const std::vector<geo::GridCell>& allowed_cells,
    const std::vector<geo::GridCell>& authored_obstacles,
    int padding_cells) {
    if (allowed_cells.empty()) return {};
    int min_column = allowed_cells.front().column;
    int max_column = min_column;
    int min_row = allowed_cells.front().row;
    int max_row = min_row;
    for (const geo::GridCell cell : allowed_cells) {
        min_column = std::min(min_column, cell.column);
        max_column = std::max(max_column, cell.column);
        min_row = std::min(min_row, cell.row);
        max_row = std::max(max_row, cell.row);
    }
    const int padding = std::max(0, padding_cells);
    std::vector<geo::GridCell> locked;
    for (const geo::GridCell cell : authored_obstacles) {
        if (cell.column >= min_column - padding && cell.column <= max_column + padding &&
            cell.row >= min_row - padding && cell.row <= max_row + padding) {
            locked.push_back(cell);
        }
    }
    std::sort(locked.begin(), locked.end(), [](geo::GridCell lhs, geo::GridCell rhs) {
        return lhs.row < rhs.row || (lhs.row == rhs.row && lhs.column < rhs.column);
    });
    locked.erase(std::unique(locked.begin(), locked.end(), sameCell), locked.end());
    return locked;
}

std::string aquariumConstructionHintForValidation(std::string_view validation_message) {
    if (validation_message.empty()) return {};
    if (validation_message.find("at least") != std::string_view::npos) {
        return "MAKE TANK WIDER";
    }
    if (validation_message.find("construction area") != std::string_view::npos) {
        return "OUTSIDE BUILD AREA";
    }
    if (validation_message.find("overlap") != std::string_view::npos ||
        validation_message.find("obstacle") != std::string_view::npos) {
        return "SPACE IS BLOCKED";
    }
    if (validation_message.find("eight player tanks") != std::string_view::npos) {
        return "ROOM IS FULL";
    }
    return "CANNOT BUILD HERE";
}

} // namespace pr::gameplay::world3d::aquarium::construction
