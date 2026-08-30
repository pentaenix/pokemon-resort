#include "gameplay/world3d/aquarium/construction/AquariumConstructionCamera.hpp"

#include <algorithm>
#include <cmath>

namespace pr::gameplay::world3d::aquarium::construction {

void resetAquariumConstructionCamera(AquariumConstructionCameraTrackingState& state) {
    state = {};
}

AquariumConstructionCameraOverview trackAquariumConstructionCursor(
    AquariumConstructionCameraTrackingState& state,
    int grid_width,
    int grid_height,
    float tile_world_units,
    float floor_y,
    float vertical_fov_degrees,
    float viewport_aspect,
    float normal_pitch_degrees,
    ::pr::aquarium::geometry::GridCell focus,
    bool property_panel_visible,
    double delta_seconds) {
    constexpr float kRadians = 3.1415926535f / 180.0f;
    AquariumConstructionCameraOverview overview;
    overview.pitch_degrees = std::clamp(normal_pitch_degrees - 6.0f, -68.0f, -61.0f);
    const float tile = std::max(1.0f, tile_world_units);
    const float width = static_cast<float>(std::max(1, grid_width)) * tile;
    const float depth = static_cast<float>(std::max(1, grid_height)) * tile;
    const float pitch_radians = overview.pitch_degrees * kRadians;
    const float fov_radians = std::clamp(vertical_fov_degrees, 15.0f, 70.0f) * kRadians;
    const float tangent_vertical = std::tan(fov_radians * 0.5f);
    const float aspect = std::max(0.5f, viewport_aspect);
    // Keep roughly ten cells visible vertically. The normal camera's perspective
    // character is preserved while minimum footprints remain readable.
    const float distance = std::clamp(
        (tile * 5.0f) / std::max(0.1f, tangent_vertical), tile * 20.0f, tile * 28.0f);
    const float composition_x = property_panel_visible ? -tile * 0.75f : -tile * 0.35f;
    const float composition_z = property_panel_visible ? tile * 1.75f : 0.0f;
    const float focus_x = (static_cast<float>(focus.column) + 0.5f) * tile + composition_x;
    const float focus_z = (static_cast<float>(focus.row) + 0.5f) * tile + composition_z;
    if (!state.initialized) {
        state.center_x = focus_x;
        state.center_z = focus_z;
        state.property_panel_visible = property_panel_visible;
        state.initialized = true;
    }
    const bool composition_changed =
        state.property_panel_visible != property_panel_visible;
    state.property_panel_visible = property_panel_visible;

    const float dead_zone_x = tile * 3.0f;
    const float dead_zone_z = tile * 2.15f;
    float desired_x = state.center_x;
    float desired_z = state.center_z;
    if (composition_changed) {
        // Opening or closing the contextual tray changes the protected screen
        // area. Recompose deliberately instead of letting the dead zone absorb
        // the small offset and leave the tank beneath the tray.
        desired_x = focus_x;
        desired_z = focus_z;
    } else {
        if (focus_x < state.center_x - dead_zone_x) desired_x = focus_x + dead_zone_x;
        if (focus_x > state.center_x + dead_zone_x) desired_x = focus_x - dead_zone_x;
        if (focus_z < state.center_z - dead_zone_z) desired_z = focus_z + dead_zone_z;
        if (focus_z > state.center_z + dead_zone_z) desired_z = focus_z - dead_zone_z;
    }

    // Clamp by the approximate floor footprint of the view. A small allowance
    // keeps boundary cells readable without revealing large empty regions.
    const float view_half_width = distance * tangent_vertical * aspect;
    const float view_half_depth = distance * tangent_vertical /
        std::max(0.25f, std::abs(std::sin(pitch_radians)));
    const float padding = tile * 0.75f;
    const float min_x = std::min(width * 0.5f, std::max(0.0f, view_half_width - padding));
    const float max_x = std::max(width * 0.5f, width - min_x);
    const float min_z = std::min(depth * 0.5f, std::max(0.0f, view_half_depth - padding));
    const float max_z = std::max(depth * 0.5f, depth - min_z);
    desired_x = std::clamp(desired_x, min_x, max_x);
    desired_z = std::clamp(desired_z, min_z, max_z);

    const float blend = delta_seconds <= 0.0
        ? 1.0f
        : 1.0f - std::exp(-10.0f * static_cast<float>(delta_seconds));
    state.center_x += (desired_x - state.center_x) * blend;
    state.center_z += (desired_z - state.center_z) * blend;
    const float yaw_radians = overview.yaw_degrees * kRadians;
    overview.position = {
        state.center_x,
        floor_y - std::sin(pitch_radians) * distance,
        state.center_z - std::cos(pitch_radians) * std::cos(yaw_radians) * distance,
    };
    return overview;
}

} // namespace pr::gameplay::world3d::aquarium::construction
