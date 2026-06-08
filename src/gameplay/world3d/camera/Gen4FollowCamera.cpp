#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"

#include <algorithm>
#include <cmath>

namespace pr::gameplay::world3d::camera {

namespace {

constexpr float kPi = 3.1415926535f;

Vec3 add(const Vec3& a, const Vec3& b) { return Vec3{a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(const Vec3& a, const Vec3& b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 mul(const Vec3& v, float s) { return Vec3{v.x * s, v.y * s, v.z * s}; }
float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3 normalize(const Vec3& v) {
    const float mag = std::sqrt(std::max(0.000001f, dot(v, v)));
    return Vec3{v.x / mag, v.y / mag, v.z / mag};
}

} // namespace

Gen4FollowCamera::Gen4FollowCamera(Gen4CameraPreset preset) : preset_(preset) {
    rebuildBasis();
}

void Gen4FollowCamera::setTarget(Vec3 target) {
    manual_mode_ = false;
    target_ = target;
    rebuildBasis();
}

void Gen4FollowCamera::setManualPose(Vec3 position, float yaw_deg, float pitch_deg) {
    manual_mode_ = true;
    position_ = position;
    const float yaw = yaw_deg * (kPi / 180.0f);
    const float pitch = pitch_deg * (kPi / 180.0f);
    forward_ = normalize(Vec3{
        std::sin(yaw) * std::cos(pitch),
        std::sin(pitch),
        std::cos(yaw) * std::cos(pitch)});
    const Vec3 world_up{0.0f, 1.0f, 0.0f};
    // Match bx::mtxLookAt (left-handed default): right = cross(up, view), up = cross(view, right).
    right_ = normalize(cross(world_up, forward_));
    up_ = normalize(cross(forward_, right_));
}

void Gen4FollowCamera::rebuildBasis() {
    const float pitch = preset_.pitch_deg * (kPi / 180.0f);
    const float yaw = preset_.yaw_deg * (kPi / 180.0f);

    const Vec3 orbit{
        std::sin(yaw) * std::cos(pitch),
        -std::sin(pitch),
        std::cos(yaw) * std::cos(pitch)};

    position_ = add(target_, mul(orbit, preset_.distance));
    forward_ = normalize(sub(target_, position_));
    const Vec3 world_up{0.0f, 1.0f, 0.0f};
    right_ = normalize(cross(world_up, forward_));
    up_ = normalize(cross(forward_, right_));
}

bool Gen4FollowCamera::worldToScreen(
    const Vec3& world,
    int viewport_w,
    int viewport_h,
    float& out_x,
    float& out_y,
    float& out_depth) const {
    const Vec3 rel = sub(world, position_);
    const float cam_x = dot(rel, right_);
    const float cam_y = dot(rel, up_);
    const float cam_z = dot(rel, forward_);

    out_depth = cam_z;
    if (cam_z <= preset_.near_clip || cam_z >= preset_.far_clip) {
        return false;
    }

    const float fov_y = preset_.fov_y_deg * (kPi / 180.0f);
    const float f = 1.0f / std::tan(std::max(0.001f, fov_y * 0.5f));
    const float aspect =
        static_cast<float>(std::max(1, viewport_w)) / static_cast<float>(std::max(1, viewport_h));

    const float ndc_x = (cam_x * f / aspect) / cam_z;
    const float ndc_y = (cam_y * f) / cam_z;

    out_x = (ndc_x * 0.5f + 0.5f) * static_cast<float>(viewport_w);
    out_y = (0.5f - ndc_y * 0.5f) * static_cast<float>(viewport_h);
    return true;
}

float Gen4FollowCamera::perspectiveScale(float depth) const {
    return std::max(0.05f, preset_.distance / std::max(preset_.near_clip, depth));
}

Vec3 Gen4FollowCamera::screenOffsetToWorldOffset(int offset_y_px, float depth, int viewport_h) const {
    if (offset_y_px == 0) {
        return Vec3{};
    }
    const float fov_y = preset_.fov_y_deg * (kPi / 180.0f);
    const float f = 1.0f / std::tan(std::max(0.001f, fov_y * 0.5f));
    const float world = (-static_cast<float>(offset_y_px)) * depth /
        (f * static_cast<float>(std::max(1, viewport_h)) * 0.5f);
    return Vec3{up_.x * world, up_.y * world, up_.z * world};
}

Gen4FollowCamera::Pose Gen4FollowCamera::pose() const {
    return Pose{position_, forward_, right_, up_, preset_};
}

} // namespace pr::gameplay::world3d::camera
