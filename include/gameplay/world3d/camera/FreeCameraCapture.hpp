#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"

#include <string>

namespace pr::gameplay::world3d::camera {

struct FreeCameraCapture {
    std::string map_id;
    std::string nearest_placement_id;
    Vec3 camera_position{};
    float yaw_degrees = 0.0f;
    float pitch_degrees = 0.0f;
    Vec3 player_position{};
    FacingDirection player_facing = FacingDirection::South;
};

// Compact JSON is suitable for both terminal logs and SDL's clipboard.
std::string formatFreeCameraCapture(const FreeCameraCapture& capture);

} // namespace pr::gameplay::world3d::camera
