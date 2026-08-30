#pragma once

#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"

namespace pr::gameplay::world3d::aquarium::construction {

struct AquariumConstructionCameraOverview {
    gameplay::world3d::camera::Vec3 position;
    float yaw_degrees = 180.0f;
    float pitch_degrees = -68.0f;
};

AquariumConstructionCameraOverview aquariumConstructionCameraOverview(
    int grid_width,
    int grid_height,
    float tile_world_units,
    float floor_y,
    float vertical_fov_degrees,
    float viewport_aspect,
    float maximum_tank_height_world = 96.0f);

} // namespace pr::gameplay::world3d::aquarium::construction
