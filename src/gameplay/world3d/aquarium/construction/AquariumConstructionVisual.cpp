#include "gameplay/world3d/aquarium/construction/AquariumConstructionVisual.hpp"

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

} // namespace

bool ConstructionHudRect::contains(int point_x, int point_y) const {
    return width > 0 && height > 0 && point_x >= x && point_y >= y &&
        point_x < x + width && point_y < y + height;
}

ConstructionVisualMesh buildAquariumConstructionWorldMesh(
    const AquariumConstructionVisual& visual) {
    ConstructionVisualMesh mesh;
    if (!visual.visible || visual.cells.empty()) return mesh;
    const float tile = visual.tile_world_units;
    constexpr std::uint32_t kAllowedFill = colorAbgr(244, 194, 55, 88);
    constexpr std::uint32_t kAllowedBorder = colorAbgr(255, 219, 79, 220);
    constexpr std::uint32_t kBlockedFill = colorAbgr(66, 64, 58, 150);
    constexpr std::uint32_t kBlockedBorder = colorAbgr(242, 155, 62, 220);
    constexpr std::uint32_t kValidFill = colorAbgr(62, 210, 140, 145);
    constexpr std::uint32_t kInvalidFill = colorAbgr(224, 67, 73, 170);
    constexpr std::uint32_t kCursor = colorAbgr(255, 250, 184, 255);
    constexpr std::uint32_t kAnchor = colorAbgr(255, 197, 45, 255);
    constexpr std::uint32_t kHandle = colorAbgr(255, 255, 255, 255);

    for (const ConstructionCellSurface& surface : visual.cells) {
        const float x0 = static_cast<float>(surface.cell.column) * tile;
        const float z0 = static_cast<float>(surface.cell.row) * tile;
        const float y = surface.floor_y + 0.16f;
        appendQuad(mesh, x0 + 0.7f, z0 + 0.7f, x0 + tile - 0.7f, z0 + tile - 0.7f,
            y, surface.blocked ? kBlockedFill : kAllowedFill);
        appendBorder(mesh, x0 + 0.35f, z0 + 0.35f, x0 + tile - 0.35f, z0 + tile - 0.35f,
            y + 0.03f, 0.38f, surface.blocked ? kBlockedBorder : kAllowedBorder);
    }

    for (const geo::GridCell cell : visual.draft_cells) {
        const float x0 = static_cast<float>(cell.column) * tile;
        const float z0 = static_cast<float>(cell.row) * tile;
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

    const float cursor_x = static_cast<float>(visual.cursor.column) * tile;
    const float cursor_z = static_cast<float>(visual.cursor.row) * tile;
    const float cursor_y = floorForCell(visual, visual.cursor) + 0.35f;
    appendBorder(mesh, cursor_x + 1.0f, cursor_z + 1.0f,
        cursor_x + tile - 1.0f, cursor_z + tile - 1.0f, cursor_y, 0.9f, kCursor);

    if (visual.anchor) {
        const float center_x = (static_cast<float>(visual.anchor->column) + 0.5f) * tile;
        const float center_z = (static_cast<float>(visual.anchor->row) + 0.5f) * tile;
        appendDiamond(mesh, center_x, center_z,
            floorForCell(visual, *visual.anchor) + 0.48f, 3.1f, kAnchor);
    }
    if (visual.state == ConstructionState::ResizeFootprint ||
        visual.state == ConstructionState::DraftReview) {
        appendDiamond(mesh, cursor_x + tile * 0.5f, cursor_z + tile * 0.5f,
            cursor_y + 0.16f, 4.0f, kHandle);
        appendDiamond(mesh, cursor_x + tile * 0.5f, cursor_z + tile * 0.5f,
            cursor_y + 0.19f, 2.4f, visual.draft_valid ? kValidFill : kInvalidFill);
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
        const float x0 = static_cast<float>(surface.cell.column) * tile;
        const float z0 = static_cast<float>(surface.cell.row) * tile;
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

ConstructionHudLayout aquariumConstructionHudLayout(
    int viewport_width, int viewport_height, ConstructionState state) {
    ConstructionHudLayout layout;
    const int size = std::clamp(std::min(viewport_width, viewport_height) / 10, 48, 76);
    const int gap = std::max(10, size / 5);
    const int bottom = std::max(14, viewport_height / 28);
    if (state == ConstructionState::Browse) {
        const int total = size * 2 + gap;
        const int start = (viewport_width - total) / 2;
        layout.place = {start, viewport_height - bottom - size, size, size};
        layout.exit = {start + size + gap, viewport_height - bottom - size, size, size};
    } else if (state == ConstructionState::DraftReview) {
        const int total = size * 3 + gap * 2;
        const int start = (viewport_width - total) / 2;
        layout.build = {start, viewport_height - bottom - size, size, size};
        layout.adjust = {start + size + gap, viewport_height - bottom - size, size, size};
        layout.cancel = {start + (size + gap) * 2, viewport_height - bottom - size, size, size};
    } else if (state == ConstructionState::ResizeFootprint) {
        const int total = size * 2 + gap;
        const int start = (viewport_width - total) / 2;
        layout.review = {start, viewport_height - bottom - size, size, size};
        layout.cancel = {start + size + gap, viewport_height - bottom - size, size, size};
    }
    return layout;
}

ConstructionHudAction hitTestAquariumConstructionHud(
    const ConstructionHudLayout& layout,
    int screen_x, int screen_y, ConstructionState state) {
    if (state == ConstructionState::Browse) {
        if (layout.place.contains(screen_x, screen_y)) return ConstructionHudAction::Place;
        if (layout.exit.contains(screen_x, screen_y)) return ConstructionHudAction::Exit;
    }
    if (state == ConstructionState::ResizeFootprint &&
        layout.review.contains(screen_x, screen_y)) return ConstructionHudAction::Review;
    if (state == ConstructionState::DraftReview) {
        if (layout.build.contains(screen_x, screen_y)) return ConstructionHudAction::Build;
        if (layout.adjust.contains(screen_x, screen_y)) return ConstructionHudAction::Adjust;
    }
    if (layout.cancel.contains(screen_x, screen_y)) return ConstructionHudAction::Cancel;
    return ConstructionHudAction::None;
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
