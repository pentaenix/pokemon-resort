#pragma once

#include "gameplay/world3d/aquarium/AquariumNavigation.hpp"
#include <functional>

namespace pr::gameplay::world3d::aquarium {

enum class AquariumLocomotion { Forward, Hover, Ground };
enum class AquariumMotionBlockage { None, Turning, Boundary, Crowd, NoRoute, SearchLimit, Yielding };

struct AquariumMotionIntent {
    Point3 direction{};
    float distance = 0.0f;
    float speed = 0.0f;
    AquariumLocomotion locomotion = AquariumLocomotion::Forward;
};

struct AquariumMotionPose {
    Point3 position{};
    float yaw = 0.0f;
    float pitch = 0.0f;
    // Locomotion pitch is independent of the model's artistic lean limit.
    float travel_pitch = 0.0f;
};

struct AquariumMotionLimits {
    float turn_speed = 90.0f;
    float pitch_speed = 30.0f;
    float base_pitch = 0.0f;
    float presentation_lean = 10.0f;
    bool floor = false;
    bool continuous_cruise = false;
};

struct AquariumMotionStepResult {
    AquariumMotionPose pose;
    Point3 displacement{};
    Point3 velocity{};
    AquariumMotionBlockage blockage = AquariumMotionBlockage::None;
    Point3 steering_direction{};
    float steering_speed_scale = 1.0f;
};

// Returns a proposal, never mutates the active pose. The caller validates the
// swept body and crowd before publishing it.
AquariumMotionStepResult proposeAquariumMotion(
    const AquariumMotionPose&, const AquariumMotionIntent&, const AquariumMotionLimits&, float dt);

AquariumMotionStepResult proposeAquariumCruise(
    const AquariumMotionPose&, const AquariumMotionIntent&, const AquariumMotionLimits&, float dt,
    const std::function<bool(Point3,Point3)>& segment_clear);

struct AquariumMotionProgress {
    Point3 anchor{};
    Point3 goal{};
    float seconds = 0.0f;
    bool initialized = false;
    // Rest/yield is deliberately not observed. Small steps accumulate relative
    // to an anchor instead of incorrectly counting every valid step as progress.
    bool observe(Point3 position, Point3 destination, float speed, float dt);
    void reset() { initialized=false; seconds=0.0f; }
};

// Presentation-only bank: brief steering corrections should not rock a cruiser.
struct AquariumCruiseBank {
    int turn_sign = 0;
    float sustained_seconds = 0.0f;
    float update(float current_roll, float turn_fraction, bool moving, float dt);
};

const char* aquariumMotionBlockageName(AquariumMotionBlockage);

} // namespace pr::gameplay::world3d::aquarium
