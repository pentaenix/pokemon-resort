#include "gameplay/world3d/aquarium/AquariumInspectionCamera.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pr::gameplay::world3d::aquarium {
namespace {

constexpr float kPi = 3.14159265358979323846f;

camera::Vec3 facingVector(FacingDirection facing) {
    switch (facing) {
        case FacingDirection::North: return {0.0f, 0.0f, -1.0f};
        case FacingDirection::East: return {1.0f, 0.0f, 0.0f};
        case FacingDirection::South: return {0.0f, 0.0f, 1.0f};
        case FacingDirection::West: return {-1.0f, 0.0f, 0.0f};
    }
    return {0.0f, 0.0f, -1.0f};
}

camera::Vec3 add(camera::Vec3 a, camera::Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

camera::Vec3 subtract(camera::Vec3 a, camera::Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

camera::Vec3 scale(camera::Vec3 value, float amount) {
    return {value.x * amount, value.y * amount, value.z * amount};
}

float length(camera::Vec3 value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

camera::Vec3 moveToward(camera::Vec3 current, camera::Vec3 target, float maximum_delta) {
    const camera::Vec3 delta = subtract(target, current);
    const float distance = length(delta);
    if (distance <= maximum_delta || distance <= 0.0001f) return target;
    return add(current, scale(delta, maximum_delta / distance));
}

camera::Vec3 rotateLocal(float x, float y, float z, float yaw_degrees) {
    const float yaw = yaw_degrees * kPi / 180.0f;
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    return {x * c + z * s, y, -x * s + z * c};
}

bool rayHitsTank(
    camera::Vec3 origin,
    camera::Vec3 direction,
    const AquariumTankRuntime& tank,
    float reach,
    float* distance_out) {
    const camera::Vec3 local_origin = rotateLocal(
        origin.x - tank.world_center[0], 0.0f,
        origin.z - tank.world_center[2], -tank.yaw_degrees);
    const camera::Vec3 local_direction = rotateLocal(
        direction.x, 0.0f, direction.z, -tank.yaw_degrees);
    float near_t = 0.0f;
    float far_t = reach;
    const auto clip_axis = [&](float position, float direction_axis, float half_extent) {
        if (std::abs(direction_axis) < 0.0001f) return std::abs(position) <= half_extent;
        float a = (-half_extent - position) / direction_axis;
        float b = (half_extent - position) / direction_axis;
        if (a > b) std::swap(a, b);
        near_t = std::max(near_t, a);
        far_t = std::min(far_t, b);
        return near_t <= far_t;
    };
    if (!clip_axis(local_origin.x, local_direction.x, tank.half_width_world) ||
        !clip_axis(local_origin.z, local_direction.z, tank.half_depth_world) ||
        far_t < 0.0f || near_t > reach) return false;
    if (distance_out) *distance_out = std::max(0.0f, near_t);
    return true;
}

void applyManualLookAt(
    camera::Gen4FollowCamera& camera,
    camera::Vec3 position,
    camera::Vec3 target) {
    const camera::Vec3 delta = subtract(target, position);
    const float horizontal = std::sqrt(delta.x * delta.x + delta.z * delta.z);
    const float yaw = std::atan2(delta.x, delta.z) * 180.0f / kPi;
    const float pitch = std::atan2(delta.y, std::max(0.0001f, horizontal)) * 180.0f / kPi;
    camera.setManualPose(position, yaw, pitch);
}

} // namespace

AquariumInspectionCamera::AquariumInspectionCamera(std::vector<AquariumTankRuntime> tanks)
    : tanks_(std::move(tanks)) {}

bool AquariumInspectionCamera::tryBegin(
    camera::Vec3 player_position,
    FacingDirection facing,
    float tile_size,
    const camera::Gen4FollowCamera& current_camera) {
    const camera::Vec3 forward = facingVector(facing);
    const AquariumTankRuntime* selected = nullptr;
    float nearest = std::numeric_limits<float>::max();
    for (const AquariumTankRuntime& tank : tanks_) {
        if (!tank.inspection_camera.enabled) continue;
        float distance = 0.0f;
        if (rayHitsTank(
                player_position, forward, tank,
                tank.inspection_camera.interaction_reach_tiles * tile_size, &distance) &&
            distance < nearest) {
            nearest = distance;
            selected = &tank;
        }
    }
    if (!selected) return false;

    const auto& tuning = selected->inspection_camera;
    const auto pose = current_camera.pose();
    // Inspection is a refinement of the known-good follow camera. Rebuilding a
    // camera one tile behind the player places the entire scene inside this
    // preset's 150-unit near clip and renders black.
    desired_position_ = add(
        add(pose.position, scale(pose.right, tuning.side_tiles * tile_size)),
        add(
            scale(pose.forward, tuning.closer_tiles * tile_size),
            camera::Vec3{0.0f, -tuning.lower_tiles * tile_size, 0.0f}));
    desired_position_.y = std::max(
        desired_position_.y,
        selected->floor_y_world + tile_size);
    const camera::Vec3 look_offset = rotateLocal(
        tuning.look_at_x_meters * selected->units_per_meter_world,
        tuning.look_at_y_meters * selected->units_per_meter_world,
        tuning.look_at_z_meters * selected->units_per_meter_world,
        selected->yaw_degrees);
    desired_look_at_ = add(
        camera::Vec3{selected->world_center[0], selected->world_center[1], selected->world_center[2]},
        look_offset);
    const float look_margin = std::max(0.05f, selected->units_per_meter_world * 0.02f);
    desired_look_at_.y = std::clamp(
        desired_look_at_.y,
        selected->water_bottom_world + look_margin,
        selected->water_top_world - look_margin);
    smooth_ = tuning.smooth;
    active_placement_id_ = selected->placement_id;
    active_ = true;

    current_position_ = smooth_ <= 0.0f ? desired_position_ : pose.position;
    current_position_.y = std::max(current_position_.y, selected->floor_y_world + tile_size);
    current_look_at_ = smooth_ <= 0.0f
        ? desired_look_at_
        : add(pose.position, scale(pose.forward, std::max(1.0f, length(subtract(desired_look_at_, pose.position)))));
    return true;
}

void AquariumInspectionCamera::update(double dt_seconds, camera::Gen4FollowCamera& camera) {
    if (!active_) return;
    if (smooth_ > 0.0f) {
        const float step = smooth_ * static_cast<float>(std::clamp(dt_seconds, 0.0, 0.1));
        current_position_ = moveToward(current_position_, desired_position_, step);
        current_look_at_ = moveToward(current_look_at_, desired_look_at_, step);
    } else {
        current_position_ = desired_position_;
        current_look_at_ = desired_look_at_;
    }
    applyManualLookAt(camera, current_position_, current_look_at_);
}

void AquariumInspectionCamera::close() {
    active_ = false;
    active_placement_id_.clear();
}

} // namespace pr::gameplay::world3d::aquarium
