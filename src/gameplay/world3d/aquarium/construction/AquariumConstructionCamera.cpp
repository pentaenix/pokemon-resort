#include "gameplay/world3d/aquarium/construction/AquariumConstructionCamera.hpp"

#include <algorithm>
#include <cmath>

namespace pr::gameplay::world3d::aquarium::construction {

AquariumConstructionCameraOverview aquariumConstructionCameraOverview(
    int grid_width,
    int grid_height,
    float tile_world_units,
    float floor_y,
    float vertical_fov_degrees,
    float viewport_aspect,
    float maximum_tank_height_world) {
    constexpr float kRadians = 3.1415926535f / 180.0f;
    AquariumConstructionCameraOverview overview;
    const float tile = std::max(1.0f, tile_world_units);
    const float width = static_cast<float>(std::max(1, grid_width)) * tile;
    const float depth = static_cast<float>(std::max(1, grid_height)) * tile;
    const float padded_width = width + tile * 2.0f;
    const float padded_depth = depth + tile * 2.0f;
    const float pitch_radians = overview.pitch_degrees * kRadians;
    const float fov_radians = std::clamp(vertical_fov_degrees, 15.0f, 70.0f) * kRadians;
    const float tangent_vertical = std::tan(fov_radians * 0.5f);
    const float tangent_horizontal = tangent_vertical * std::max(0.5f, viewport_aspect);
    const float horizontal_distance = padded_width * 0.5f / tangent_horizontal;
    const float vertical_extent = padded_depth * 0.5f * std::abs(std::sin(pitch_radians)) +
        std::max(0.0f, maximum_tank_height_world) * std::abs(std::cos(pitch_radians));
    const float vertical_distance = vertical_extent / tangent_vertical;
    const float distance = std::max(320.0f,
        std::max(horizontal_distance, vertical_distance) * 1.32f);
    const float yaw_radians = overview.yaw_degrees * kRadians;
    const float center_x = width * 0.5f;
    const float center_z = depth * 0.5f;
    overview.position = {
        center_x,
        floor_y - std::sin(pitch_radians) * distance,
        center_z - std::cos(pitch_radians) * std::cos(yaw_radians) * distance,
    };
    return overview;
}

} // namespace pr::gameplay::world3d::aquarium::construction
