#pragma once

#include "gameplay/world3d/aquarium/AquariumNavigation.hpp"

#include <string>
#include <vector>

namespace pr::gameplay::world3d::aquarium {

enum class AquariumFormationMode {
    Attached,
    Recovering,
};

struct AquariumFormationBody {
    std::string id;
    std::string tank_id;
    Point3 origin{};
    Point3 velocity{};
    float yaw_degrees = 0.0f;
    float pitch_degrees = 0.0f;
    float base_pitch_degrees = 0.0f;
    float center_y_offset = 0.0f;
    float half_width = 0.1f;
    float half_height = 0.1f;
    float half_length = 0.1f;
};

struct AquariumFormationInput {
    AquariumFormationBody follower;
    AquariumFormationBody leader;
    AquariumFormationBody previous_leader;
    const std::vector<AquariumFormationBody>* neighbours = nullptr;
    float role_phase_radians = 0.0f;
    float elapsed_seconds = 0.0f;
    float follow_distance = 0.5f;
    float body_gap = 0.05f;
    float dt_seconds = 1.0f / 60.0f;
    AquariumFormationMode previous_mode = AquariumFormationMode::Attached;
};

struct AquariumFormationSteering {
    Point3 anchor_origin{};
    Point3 anchor_velocity{};
    Point3 desired_velocity{};
    float formation_error = 0.0f;
    AquariumFormationMode mode = AquariumFormationMode::Attached;
};

// Pure deterministic leader-following steering. Navigation is deliberately
// handled by AquariumSimulation so this module can be tested without map data.
AquariumFormationSteering steerAquariumFormation(
    const AquariumFormationInput& input);

} // namespace pr::gameplay::world3d::aquarium
