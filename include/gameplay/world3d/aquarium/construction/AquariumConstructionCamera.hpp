#pragma once

#include "aquarium_geometry/Types.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"

namespace pr::gameplay::world3d::aquarium::construction {

struct AquariumConstructionCameraOverview {
    gameplay::world3d::camera::Vec3 position;
    float yaw_degrees = 180.0f;
    float pitch_degrees = -61.0f;
};

struct AquariumConstructionCameraTrackingState {
    float center_x = 0.0f;
    float center_z = 0.0f;
    bool initialized = false;
    bool property_panel_visible = false;
};

void resetAquariumConstructionCamera(AquariumConstructionCameraTrackingState& state);

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
    double delta_seconds,
    float placement_offset_world_units = 0.0f);

} // namespace pr::gameplay::world3d::aquarium::construction
