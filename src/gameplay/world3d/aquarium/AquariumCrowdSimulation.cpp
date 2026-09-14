#include "gameplay/world3d/aquarium/AquariumCrowdSimulation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace pr::gameplay::world3d::aquarium {
namespace {

float length(Point3 value) {
    return std::sqrt(
        value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
}

float dot(Point3 lhs, Point3 rhs) {
    return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2];
}

Point3 normalized(Point3 value, Point3 fallback = {}) {
    const float magnitude = length(value);
    if (magnitude <= 0.00001f) return fallback;
    for (float& component : value) component /= magnitude;
    return value;
}

Point3 centerAt(const AquariumCrowdBody& body, float seconds) {
    return {
        body.center[0] + body.velocity[0] * seconds,
        body.center[1] + body.velocity[1] * seconds,
        body.center[2] + body.velocity[2] * seconds};
}

float normalizedDistance(
    Point3 self_center,
    const AquariumCrowdBody& self,
    Point3 other_center,
    const AquariumCrowdBody& other,
    float gap,
    float scale_multiplier = 1.0f) {
    const float self_scale = self.body_scale * scale_multiplier;
    const float other_scale = other.body_scale * scale_multiplier;
    const float horizontal = std::max(
        0.01f, self.horizontal_radius * self_scale +
            other.horizontal_radius * other_scale + gap);
    const float vertical = std::max(
        0.01f, self.vertical_radius * self_scale +
            other.vertical_radius * other_scale + gap);
    const float dx = (self_center[0] - other_center[0]) / horizontal;
    const float dy = (self_center[1] - other_center[1]) / vertical;
    const float dz = (self_center[2] - other_center[2]) / horizontal;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

Point3 steerAquariumCrowd(
    const AquariumCrowdBody& self,
    const std::vector<AquariumCrowdBody>& neighbours,
    Point3 desired_direction,
    float body_gap_meters) {
    constexpr float kPredictionSeconds = 0.16f;
    constexpr float kInfluenceDistance = 1.30f;
    constexpr std::size_t kNearestNeighbourCount = 3U;
    desired_direction = normalized(desired_direction);
    Point3 repulsion{};
    const Point3 predicted_self = centerAt(self, kPredictionSeconds);
    struct Nearby {
        const AquariumCrowdBody* body = nullptr;
        Point3 predicted_center{};
        float distance = std::numeric_limits<float>::max();
    };
    std::array<Nearby, kNearestNeighbourCount> nearby{};
    for (const AquariumCrowdBody& other : neighbours) {
        const Point3 predicted_other = centerAt(other, kPredictionSeconds);
        const float distance = normalizedDistance(
            predicted_self, self, predicted_other, other, body_gap_meters);
        if (distance >= kInfluenceDistance) continue;
        Nearby candidate{&other, predicted_other, distance};
        for (Nearby& slot : nearby) {
            if (candidate.distance >= slot.distance) continue;
            std::swap(candidate, slot);
        }
    }
    for (const Nearby& neighbour : nearby) {
        if (!neighbour.body) continue;
        const AquariumCrowdBody& other = *neighbour.body;
        const float horizontal = std::max(0.01f,
            self.horizontal_radius * self.body_scale +
                other.horizontal_radius * other.body_scale + body_gap_meters);
        const float vertical = std::max(0.01f,
            self.vertical_radius * self.body_scale +
                other.vertical_radius * other.body_scale + body_gap_meters);
        Point3 away{
            (predicted_self[0] - neighbour.predicted_center[0]) /
                (horizontal * horizontal),
            (predicted_self[1] - neighbour.predicted_center[1]) /
                (vertical * vertical),
            (predicted_self[2] - neighbour.predicted_center[2]) /
                (horizontal * horizontal)};
        if (length(away) <= 0.00001f) {
            away = {-desired_direction[2], 0.35f, desired_direction[0]};
            if (length(away) <= 0.00001f) away = {1.0f, 0.0f, 0.0f};
        }
        const float strength = (kInfluenceDistance - neighbour.distance) /
            kInfluenceDistance;
        away = normalized(away);
        for (std::size_t axis = 0; axis < repulsion.size(); ++axis) {
            repulsion[axis] += away[axis] * strength * 0.42f;
        }
        // A direct head-on repulsion can merely cancel forward input and make
        // both actors wait. Add a small vertical sidestep so swimmers flow
        // around contact instead of jittering or deadlocking nose-to-nose.
        if (dot(away, desired_direction) < -0.55f) {
            Point3 tangent = normalized(
                {-desired_direction[2], 0.35f, desired_direction[0]},
                {0.0f, 1.0f, 0.0f});
            for (std::size_t axis = 0; axis < repulsion.size(); ++axis) {
                repulsion[axis] += tangent[axis] * strength * 0.34f;
            }
        }
    }
    const float repulsion_length = length(repulsion);
    if (repulsion_length > 0.36f) {
        for (float& component : repulsion) component *= 0.36f / repulsion_length;
    }
    Point3 steered{
        desired_direction[0] + repulsion[0],
        desired_direction[1] + repulsion[1],
        desired_direction[2] + repulsion[2]};
    return normalized(steered, desired_direction);
}

bool aquariumCrowdMoveAllowed(
    const AquariumCrowdBody& self,
    Point3 candidate_center,
    const std::vector<AquariumCrowdBody>& neighbours,
    float body_gap_meters) {
    constexpr float kCoreScale = 0.68f;
    constexpr float kDeepPenetration = 0.70f;
    for (const AquariumCrowdBody& other : neighbours) {
        const float current = normalizedDistance(
            self.center, self, other.center, other, body_gap_meters * 0.15f,
            kCoreScale);
        const float candidate = normalizedDistance(
            candidate_center, self, other.center, other, body_gap_meters * 0.15f,
            kCoreScale);
        if (candidate < kDeepPenetration &&
            candidate <= current + 0.00001f) return false;
    }
    return true;
}

AquariumCrowdCorrection separateAquariumCrowdCores(
    const AquariumCrowdBody& first,
    const AquariumCrowdBody& second) {
    constexpr float kCoreScale = 0.68f;
    AquariumCrowdCorrection correction;
    const float distance = normalizedDistance(
        first.center, first, second.center, second, 0.0f, kCoreScale);
    if (distance >= 1.0f) return correction;
    Point3 direction{
        first.center[0] - second.center[0],
        first.center[1] - second.center[1],
        first.center[2] - second.center[2]};
    direction = normalized(direction, {1.0f, 0.0f, 0.0f});
    const float horizontal = first.horizontal_radius * first.body_scale * kCoreScale +
        second.horizontal_radius * second.body_scale * kCoreScale;
    const float vertical = first.vertical_radius * first.body_scale * kCoreScale +
        second.vertical_radius * second.body_scale * kCoreScale;
    const float inverse_radial_distance = std::sqrt(
        direction[0] * direction[0] / (horizontal * horizontal) +
        direction[1] * direction[1] / (vertical * vertical) +
        direction[2] * direction[2] / (horizontal * horizontal));
    const float radial_distance = inverse_radial_distance > 0.0001f
        ? 1.0f / inverse_radial_distance : std::min(horizontal, vertical);
    const float push = (1.0f - distance) * radial_distance * 0.52f + 0.001f;
    for (std::size_t axis = 0; axis < direction.size(); ++axis) {
        correction.first_delta[axis] = direction[axis] * push;
        correction.second_delta[axis] = -direction[axis] * push;
    }
    correction.penetrating = true;
    return correction;
}

} // namespace pr::gameplay::world3d::aquarium
