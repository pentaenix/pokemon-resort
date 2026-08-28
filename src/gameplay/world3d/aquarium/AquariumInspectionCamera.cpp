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

camera::Vec3 facingRight(camera::Vec3 forward) {
    return {-forward.z, 0.0f, forward.x};
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

camera::Vec3 tankLookAtTarget(const AquariumTankRuntime& tank) {
    const auto& tuning = tank.inspection_camera;
    const camera::Vec3 look_offset = rotateLocal(
        tuning.look_at_x_meters * tank.units_per_meter_world,
        tuning.look_at_y_meters * tank.units_per_meter_world,
        tuning.look_at_z_meters * tank.units_per_meter_world,
        tank.yaw_degrees);
    camera::Vec3 target = add(
        camera::Vec3{tank.world_center[0], tank.world_center[1], tank.world_center[2]},
        look_offset);
    const float look_margin = std::max(0.05f, tank.units_per_meter_world * 0.02f);
    const float lower = tank.water_bottom_world + look_margin;
    const float upper = tank.water_top_world - look_margin;
    target.y = lower <= upper
        ? std::clamp(target.y, lower, upper)
        : (tank.water_bottom_world + tank.water_top_world) * 0.5f;
    return target;
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

float AquariumInspectionCamera::wallClipRadiusWorld(float tile_size) const {
    if (stage_ != Stage::Focused || active_tank_index_ >= tanks_.size()) return 0.0f;
    return std::max(0.0f,
        tanks_[active_tank_index_].inspection_camera.focused_wall_clip_radius_tiles) *
        std::max(0.0f, tile_size);
}

bool AquariumInspectionCamera::tryBegin(
    camera::Vec3 player_position,
    FacingDirection facing,
    float tile_size,
    const camera::Gen4FollowCamera& current_camera) {
    const camera::Vec3 forward = facingVector(facing);
    if (active()) return false;
    const AquariumTankRuntime* selected = nullptr;
    std::size_t selected_index = 0;
    float nearest = std::numeric_limits<float>::max();
    for (std::size_t index = 0; index < tanks_.size(); ++index) {
        const AquariumTankRuntime& tank = tanks_[index];
        if (!tank.inspection_camera.enabled) continue;
        float distance = 0.0f;
        if (rayHitsTank(
                player_position, forward, tank,
                tank.inspection_camera.interaction_reach_tiles * tile_size, &distance) &&
            distance < nearest) {
            nearest = distance;
            selected = &tank;
            selected_index = index;
        }
    }
    if (!selected) return false;

    const auto& tuning = selected->inspection_camera;
    const auto pose = current_camera.pose();
    const camera::Vec3 player_to_camera = subtract(pose.position, player_position);
    const float follow_horizontal_distance = std::sqrt(
        player_to_camera.x * player_to_camera.x + player_to_camera.z * player_to_camera.z);
    if (tuning.has_framed_inspection_view) {
        const camera::Vec3 local_forward = rotateLocal(
            forward.x, forward.y, forward.z, -selected->yaw_degrees);
        const bool front_face = std::abs(local_forward.z) >= std::abs(local_forward.x);
        desired_position_ = add(
            add(
                player_position,
                scale(forward, -tuning.inspection_behind_player_tiles * tile_size)),
            scale(facingRight(forward), tuning.side_tiles * tile_size));
        desired_position_.y = selected->floor_y_world +
            (front_face
                ? tuning.inspection_front_height_tiles
                : tuning.inspection_side_height_tiles) * tile_size;
    } else {
        // Compatibility for existing authored tanks: rotate the known-good
        // follow-camera distance behind the player's approach direction.
        const camera::Vec3 approach_base = add(
            player_position,
            scale(forward, -follow_horizontal_distance));
        desired_position_ = add(
            add(approach_base, scale(facingRight(forward), tuning.side_tiles * tile_size)),
            scale(forward, tuning.closer_tiles * tile_size));
        desired_position_.y = pose.position.y - tuning.lower_tiles * tile_size;
    }
    desired_position_.y = std::max(
        desired_position_.y,
        selected->floor_y_world + tile_size);
    desired_look_at_ = tankLookAtTarget(*selected);
    // A rotating smooth transition reads naturally from the conventional
    // north-facing camera. Other faces snap so the camera never sweeps through
    // walls or the floor while rotating around the room.
    approach_facing_ = facing;
    smooth_ = facing == FacingDirection::North ? tuning.smooth : 0.0f;
    return_smooth_ = facing == FacingDirection::North ? tuning.return_smooth : 0.0f;
    original_near_clip_ = pose.preset.near_clip;
    return_target_ = player_position;
    return_position_ = pose.position;
    active_tank_index_ = selected_index;
    active_placement_id_ = selected->placement_id;
    stage_ = Stage::Inspecting;

    current_position_ = smooth_ <= 0.0f ? desired_position_ : pose.position;
    current_position_.y = std::max(current_position_.y, selected->floor_y_world + tile_size);
    current_look_at_ = smooth_ <= 0.0f
        ? desired_look_at_
        : add(pose.position, scale(pose.forward, std::max(1.0f, length(subtract(desired_look_at_, pose.position)))));
    return true;
}

bool AquariumInspectionCamera::enterFocused(
    float tile_size,
    camera::Gen4FollowCamera& camera) {
    if (stage_ != Stage::Inspecting || active_tank_index_ >= tanks_.size()) return false;

    const AquariumTankRuntime& tank = tanks_[active_tank_index_];
    const AquariumInspectionCameraConfig& tuning = tank.inspection_camera;
    const camera::Vec3 forward = facingVector(approach_facing_);
    const camera::Vec3 local_forward = rotateLocal(
        forward.x, forward.y, forward.z, -tank.yaw_degrees);
    const bool front_face = std::abs(local_forward.z) >= std::abs(local_forward.x);
    const float face_half_extent =
        std::abs(local_forward.x) * tank.half_width_world +
        std::abs(local_forward.z) * tank.half_depth_world;
    const float horizontal_distance =
        face_half_extent + tuning.focused_standoff_tiles * tile_size;
    const camera::Vec3 center{
        tank.world_center[0], tank.world_center[1], tank.world_center[2]};
    desired_position_ = add(center, scale(forward, -horizontal_distance));
    desired_position_.y = tank.floor_y_world +
        (front_face ? tuning.focused_front_height_tiles : tuning.focused_side_height_tiles) *
            tile_size;

    const float water_height = std::max(0.0f,
        tank.water_top_world - tank.water_bottom_world);
    if (water_height <= tile_size) {
        // A shallow pool has no tall subject for the captured generic pitch to
        // frame. Aim at its real transformed center so the camera cannot look
        // over the water plane and out into the room.
        desired_look_at_ = tankLookAtTarget(tank);
    } else {
        const float yaw_degrees =
            std::atan2(forward.x, forward.z) * 180.0f / kPi;
        const float pitch_degrees = front_face
            ? tuning.focused_front_pitch_degrees
            : tuning.focused_side_pitch_degrees;
        const float yaw = yaw_degrees * kPi / 180.0f;
        const float pitch = pitch_degrees * kPi / 180.0f;
        const float view_distance = horizontal_distance /
            std::max(0.01f, std::cos(pitch));
        const camera::Vec3 focus_forward{
            std::sin(yaw) * std::cos(pitch),
            std::sin(pitch),
            std::cos(yaw) * std::cos(pitch)};
        desired_look_at_ = add(desired_position_, scale(focus_forward, view_distance));
    }

    const auto pose = camera.pose();
    current_position_ = pose.position;
    const float existing_view_distance = std::max(
        1.0f, length(subtract(current_look_at_, current_position_)));
    current_look_at_ = add(current_position_, scale(pose.forward, existing_view_distance));
    if (smooth_ <= 0.0f) {
        current_position_ = desired_position_;
        current_look_at_ = desired_look_at_;
    }
    camera.setNearClip(tuning.focused_near_clip);
    hide_overworld_actors_ = true;
    stage_ = Stage::Focused;
    return true;
}

void AquariumInspectionCamera::beginExit(
    camera::Vec3 player_position,
    camera::Gen4FollowCamera& camera) {
    if (!active() || stage_ == Stage::Returning) return;
    return_target_ = player_position;
    // Actors belong to normal world presentation, not to the camera's return
    // interpolation. Reveal them as soon as the player exits focused mode.
    hide_overworld_actors_ = false;
    if (smooth_ <= 0.0f) {
        camera.setNearClip(original_near_clip_);
        camera.setTarget(return_target_);
        close();
        return;
    }

    const auto pose = camera.pose();
    current_position_ = pose.position;
    const float existing_view_distance = std::max(
        1.0f, length(subtract(current_look_at_, current_position_)));
    current_look_at_ = add(current_position_, scale(pose.forward, existing_view_distance));
    desired_position_ = return_position_;
    desired_look_at_ = return_target_;
    smooth_ = return_smooth_;
    stage_ = Stage::Returning;
}

void AquariumInspectionCamera::updateReturnTarget(camera::Vec3 player_position) {
    if (stage_ != Stage::Returning) return;
    const camera::Vec3 player_delta = subtract(player_position, return_target_);
    return_target_ = player_position;
    return_position_ = add(return_position_, player_delta);
    desired_position_ = return_position_;
    desired_look_at_ = return_target_;
}

void AquariumInspectionCamera::update(double dt_seconds, camera::Gen4FollowCamera& camera) {
    if (!active()) return;
    if (smooth_ > 0.0f) {
        const float step = smooth_ * static_cast<float>(std::clamp(dt_seconds, 0.0, 0.1));
        current_position_ = moveToward(current_position_, desired_position_, step);
        current_look_at_ = moveToward(current_look_at_, desired_look_at_, step);
    } else {
        current_position_ = desired_position_;
        current_look_at_ = desired_look_at_;
    }
    if (stage_ == Stage::Returning &&
        length(subtract(current_position_, desired_position_)) <= 0.0001f &&
        length(subtract(current_look_at_, desired_look_at_)) <= 0.0001f) {
        camera.setNearClip(original_near_clip_);
        camera.setTarget(return_target_);
        close();
        return;
    }
    applyManualLookAt(camera, current_position_, current_look_at_);
}

void AquariumInspectionCamera::close() {
    stage_ = Stage::Inactive;
    hide_overworld_actors_ = false;
    active_placement_id_.clear();
}

} // namespace pr::gameplay::world3d::aquarium
