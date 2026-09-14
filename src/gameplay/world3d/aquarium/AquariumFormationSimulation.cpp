#include "gameplay/world3d/aquarium/AquariumFormationSimulation.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace pr::gameplay::world3d::aquarium {
namespace {

constexpr float kPi = 3.14159265358979323846f;

using Basis = std::array<Point3, 3>; // right, up, forward

float dot(Point3 lhs, Point3 rhs) {
    return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2];
}

float length(Point3 value) {
    return std::sqrt(dot(value, value));
}

Point3 normalized(Point3 value, Point3 fallback = {0.0f, 0.0f, 1.0f}) {
    const float magnitude = length(value);
    if (magnitude <= 0.00001f) return fallback;
    for (float& component : value) component /= magnitude;
    return value;
}

Point3 add(Point3 lhs, Point3 rhs) {
    for (std::size_t axis = 0; axis < lhs.size(); ++axis) lhs[axis] += rhs[axis];
    return lhs;
}

Point3 subtract(Point3 lhs, Point3 rhs) {
    for (std::size_t axis = 0; axis < lhs.size(); ++axis) lhs[axis] -= rhs[axis];
    return lhs;
}

Point3 scaled(Point3 value, float scale) {
    for (float& component : value) component *= scale;
    return value;
}

Basis basisFor(const AquariumFormationBody& body) {
    const float yaw = body.yaw_degrees * kPi / 180.0f;
    const float pitch = (body.pitch_degrees - body.base_pitch_degrees) *
        kPi / 180.0f;
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    return Basis{
        Point3{cy, 0.0f, -sy},
        Point3{sy * sp, cp, cy * sp},
        Point3{sy * cp, -sp, cy * cp},
    };
}

Point3 bodyCenter(const AquariumFormationBody& body) {
    Point3 center = body.origin;
    center[1] += body.center_y_offset;
    return center;
}

Point3 roleAnchor(
    const AquariumFormationBody& leader,
    const AquariumFormationBody& follower,
    float role_phase,
    float elapsed,
    float follow_distance,
    float gap) {
    const Basis leader_basis = basisFor(leader);
    const float phase = role_phase + kPi * 0.25f;
    const float drift_phase = elapsed * 0.37f + phase * 1.71f;
    const float lateral_radius = leader.half_width + follower.half_width + gap;
    const float vertical_radius = leader.half_height + follower.half_height + gap;
    const float lateral = std::cos(phase) * lateral_radius +
        std::sin(drift_phase) * gap * 0.35f;
    const float vertical = std::sin(phase) * vertical_radius +
        std::cos(drift_phase * 0.79f) * gap * 0.22f;
    const float trailing = std::min(
        std::max(gap, follow_distance * 0.18f),
        std::max(gap, leader.half_length * 0.35f));
    Point3 anchor = bodyCenter(leader);
    anchor = add(anchor, scaled(leader_basis[0], lateral));
    anchor = add(anchor, scaled(leader_basis[1], vertical));
    anchor = add(anchor, scaled(leader_basis[2], -trailing));
    anchor[1] -= follower.center_y_offset;
    return anchor;
}

Point3 separationVelocity(
    const AquariumFormationInput& input,
    const Basis& follower_basis,
    float leader_speed) {
    Point3 separation{};
    const Point3 follower_center = bodyCenter(input.follower);
    constexpr float kPredictionSeconds = 0.12f;
    const Point3 predicted_follower = add(
        follower_center, scaled(input.follower.velocity, kPredictionSeconds));
    const std::array<float, 3> follower_extents{
        input.follower.half_width,
        input.follower.half_height,
        input.follower.half_length};

    if (!input.neighbours) return separation;
    for (const AquariumFormationBody& other : *input.neighbours) {
        if (other.id == input.follower.id ||
            other.tank_id != input.follower.tank_id) continue;
        const Basis other_basis = basisFor(other);
        const std::array<float, 3> other_extents{
            other.half_width, other.half_height, other.half_length};
        const Point3 predicted_other = add(
            bodyCenter(other), scaled(other.velocity, kPredictionSeconds));
        const Point3 delta = subtract(predicted_follower, predicted_other);
        std::array<float, 3> projected{};
        std::array<float, 3> clearance{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            projected[axis] = dot(delta, follower_basis[axis]);
            float projected_other_extent = 0.0f;
            for (std::size_t other_axis = 0; other_axis < 3; ++other_axis) {
                projected_other_extent += std::abs(dot(
                    follower_basis[axis], other_basis[other_axis])) *
                    other_extents[other_axis];
            }
            clearance[axis] = std::max(
                0.02f, follower_extents[axis] + projected_other_extent + input.body_gap);
        }
        float normalized_distance_squared = 0.0f;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const float component = projected[axis] / clearance[axis];
            normalized_distance_squared += component * component;
        }
        const float normalized_distance = std::sqrt(normalized_distance_squared);
        if (normalized_distance >= 1.2f) continue;

        Point3 direction{};
        if (normalized_distance <= 0.001f) {
            const float phase = input.role_phase_radians + kPi * 0.25f;
            direction = add(
                scaled(follower_basis[0], std::cos(phase)),
                scaled(follower_basis[1], std::sin(phase)));
        } else {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                direction = add(direction, scaled(
                    follower_basis[axis], projected[axis] / clearance[axis]));
            }
        }
        const float strength = (1.2f - normalized_distance) / 1.2f;
        separation = add(separation, scaled(
            normalized(direction), strength * (0.32f + leader_speed * 0.55f)));
    }
    return separation;
}

void clampMagnitude(Point3& value, float maximum) {
    const float magnitude = length(value);
    if (magnitude > maximum && magnitude > 0.00001f) {
        value = scaled(value, maximum / magnitude);
    }
}

} // namespace

AquariumFormationSteering steerAquariumFormation(
    const AquariumFormationInput& input) {
    AquariumFormationSteering output;
    const float dt = std::max(0.0001f, input.dt_seconds);
    const Point3 current_anchor = roleAnchor(
        input.leader, input.follower, input.role_phase_radians,
        input.elapsed_seconds, input.follow_distance, input.body_gap);
    const Point3 previous_anchor = roleAnchor(
        input.previous_leader, input.follower, input.role_phase_radians,
        std::max(0.0f, input.elapsed_seconds - dt),
        input.follow_distance, input.body_gap);
    output.anchor_velocity = scaled(subtract(current_anchor, previous_anchor), 1.0f / dt);
    output.anchor_origin = current_anchor;

    const Point3 error = subtract(current_anchor, input.follower.origin);
    output.formation_error = length(error);
    const float recovery_distance = std::max(
        0.65f, (input.leader.half_width + input.leader.half_height) * 0.9f);
    const float attach_distance = recovery_distance * 0.55f;
    output.mode = input.previous_mode;
    if (output.mode == AquariumFormationMode::Attached &&
        output.formation_error > recovery_distance) {
        output.mode = AquariumFormationMode::Recovering;
    } else if (output.mode == AquariumFormationMode::Recovering &&
        output.formation_error < attach_distance) {
        output.mode = AquariumFormationMode::Attached;
    }

    const float leader_speed = length(input.leader.velocity);
    Point3 leader_direction = normalized(
        input.leader.velocity, basisFor(input.leader)[2]);
    Point3 target_anchor = current_anchor;
    if (output.mode == AquariumFormationMode::Recovering) {
        const float look_ahead = std::clamp(
            output.formation_error / std::max(0.2f, leader_speed), 0.25f, 0.8f);
        target_anchor = add(target_anchor, scaled(input.leader.velocity, look_ahead));
    }

    const Point3 target_error = subtract(target_anchor, input.follower.origin);
    const float target_distance = length(target_error);
    const Point3 seek_direction = normalized(target_error, leader_direction);
    const float correction_limit = output.mode == AquariumFormationMode::Attached
        ? std::max(0.22f, leader_speed * 0.55f)
        : std::max(0.45f, leader_speed * 1.1f);
    Point3 correction = scaled(
        seek_direction, std::min(correction_limit, target_distance / 0.32f));
    const Point3 separation = separationVelocity(
        input, basisFor(input.follower), leader_speed);
    output.desired_velocity = add(add(output.anchor_velocity, correction), separation);

    // Attached fish never counter-swim when a rotating anchor briefly passes
    // behind them. They slow down and let the leader pull the envelope forward.
    if (leader_speed > 0.03f) {
        const float minimum_forward_speed = leader_speed *
            (output.mode == AquariumFormationMode::Attached ? 0.42f : 0.18f);
        const float forward_speed = dot(output.desired_velocity, leader_direction);
        if (forward_speed < minimum_forward_speed) {
            output.desired_velocity = add(output.desired_velocity,
                scaled(leader_direction, minimum_forward_speed - forward_speed));
        }
        const float leader_horizontal_speed = std::sqrt(
            input.leader.velocity[0] * input.leader.velocity[0] +
            input.leader.velocity[2] * input.leader.velocity[2]);
        if (leader_horizontal_speed > 0.02f) {
            const Point3 horizontal_direction{
                input.leader.velocity[0] / leader_horizontal_speed,
                0.0f,
                input.leader.velocity[2] / leader_horizontal_speed};
            const float horizontal_forward = dot(
                output.desired_velocity, horizontal_direction);
            const float minimum_horizontal_forward = leader_horizontal_speed *
                (output.mode == AquariumFormationMode::Attached ? 0.42f : 0.12f);
            if (horizontal_forward < minimum_horizontal_forward) {
                output.desired_velocity = add(output.desired_velocity, scaled(
                    horizontal_direction,
                    minimum_horizontal_forward - horizontal_forward));
            }
        }
    }
    const float maximum_speed = output.mode == AquariumFormationMode::Attached
        ? std::max(0.35f, leader_speed * 1.3f)
        : std::max(0.65f, leader_speed * 1.75f);
    clampMagnitude(output.desired_velocity, maximum_speed);
    return output;
}

} // namespace pr::gameplay::world3d::aquarium
