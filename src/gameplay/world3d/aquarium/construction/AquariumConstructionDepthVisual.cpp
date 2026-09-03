#include "gameplay/world3d/aquarium/construction/AquariumConstructionVisual.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace pr::gameplay::world3d::aquarium::construction {
namespace {

constexpr int kMaximumDepthSteps = 12;
constexpr float kStepSpacing = 2.0f;

constexpr std::uint32_t colorAbgr(
    std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha) {
    return static_cast<std::uint32_t>(red) |
        (static_cast<std::uint32_t>(green) << 8U) |
        (static_cast<std::uint32_t>(blue) << 16U) |
        (static_cast<std::uint32_t>(alpha) << 24U);
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
    mesh.indices.insert(mesh.indices.end(), {
        first, static_cast<std::uint16_t>(first + 1U),
        static_cast<std::uint16_t>(first + 2U), first,
        static_cast<std::uint16_t>(first + 2U),
        static_cast<std::uint16_t>(first + 3U)});
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
    mesh.indices.insert(mesh.indices.end(), {
        first, static_cast<std::uint16_t>(first + 1U),
        static_cast<std::uint16_t>(first + 2U), first,
        static_cast<std::uint16_t>(first + 2U),
        static_cast<std::uint16_t>(first + 3U)});
}

} // namespace

ConstructionDepthGizmoLayout aquariumConstructionDepthGizmoLayout(
    const pr::aquarium::geometry::TankDesign& tank,
    float tank_center_x,
    float tank_center_z,
    float floor_y) {
    const int depth = std::clamp(tank.depth_steps, 0, kMaximumDepthSteps);
    ConstructionDepthGizmoLayout layout;
    layout.center_x = tank_center_x + 9.5f;
    layout.top_z = tank_center_z - kMaximumDepthSteps * kStepSpacing * 0.5f;
    layout.bottom_z = layout.top_z + kMaximumDepthSteps * kStepSpacing;
    layout.handle_z = layout.top_z + static_cast<float>(depth) * kStepSpacing;
    // The flat sand surface is at floor + 3.016 world units. Keeping the
    // entire ladder above it makes the depth proxy readable without exposing
    // or regenerating the actual below-floor basin during pointer movement.
    layout.y = floor_y + 4.32f;
    return layout;
}

void appendAquariumConstructionDepthGizmo(
    ConstructionVisualMesh& mesh,
    const ConstructionDepthGizmoLayout& layout,
    int depth_steps) {
    constexpr std::uint32_t kShadow = colorAbgr(23, 38, 58, 135);
    constexpr std::uint32_t kInactive = colorAbgr(122, 151, 178, 125);
    constexpr std::uint32_t kActiveFill = colorAbgr(246, 199, 62, 115);
    constexpr std::uint32_t kActiveRung = colorAbgr(255, 220, 85, 255);
    constexpr std::uint32_t kHandleOuter = colorAbgr(255, 250, 184, 255);
    constexpr std::uint32_t kHandleInner = colorAbgr(65, 222, 255, 255);
    const int depth = std::clamp(depth_steps, 0, kMaximumDepthSteps);
    constexpr float kHalfWidth = 3.3f;

    appendQuad(mesh,
        layout.center_x - kHalfWidth - 0.8f, layout.top_z - 1.1f,
        layout.center_x + kHalfWidth + 0.8f, layout.bottom_z + 2.7f,
        layout.y - 0.06f, kShadow);
    appendQuad(mesh,
        layout.center_x - kHalfWidth - 0.2f, layout.top_z - 0.55f,
        layout.center_x + kHalfWidth + 0.2f, layout.top_z + 0.45f,
        layout.y, kHandleOuter);

    for (int step = 0; step < kMaximumDepthSteps; ++step) {
        const float from_z = layout.top_z + static_cast<float>(step) * kStepSpacing;
        const float to_z = from_z + kStepSpacing;
        const bool active = step < depth;
        if (active) {
            appendQuad(mesh,
                layout.center_x - kHalfWidth + 0.45f, from_z + 0.22f,
                layout.center_x + kHalfWidth - 0.45f, to_z - 0.22f,
                layout.y + 0.01f, kActiveFill);
        }
        const float rung_half_width = active ? kHalfWidth : kHalfWidth - 1.05f;
        const float rung_half_thickness = active ? 0.34f : 0.18f;
        appendQuad(mesh,
            layout.center_x - rung_half_width, to_z - rung_half_thickness,
            layout.center_x + rung_half_width, to_z + rung_half_thickness,
            layout.y + 0.03f, active ? kActiveRung : kInactive);
    }

    appendDiamond(mesh, layout.center_x, layout.handle_z,
        layout.y + 0.08f, 2.35f, kHandleOuter);
    appendDiamond(mesh, layout.center_x, layout.handle_z,
        layout.y + 0.11f, 1.22f, kHandleInner);

    const float arrow_z = layout.bottom_z + 1.55f;
    appendQuad(mesh, layout.center_x - 0.55f, layout.bottom_z + 0.25f,
        layout.center_x + 0.55f, arrow_z, layout.y + 0.02f, kInactive);
    appendDiamond(mesh, layout.center_x, arrow_z,
        layout.y + 0.04f, 1.35f, kInactive);
}

} // namespace pr::gameplay::world3d::aquarium::construction
