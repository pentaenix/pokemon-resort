#pragma once

#include "gameplay/world3d/camera/Gen4CameraPreset.hpp"

namespace pr::gameplay::world3d::camera {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

class Gen4FollowCamera {
public:
    struct Pose {
        Vec3 position{};
        Vec3 forward{};
        Vec3 right{};
        Vec3 up{};
        Gen4CameraPreset preset{};
    };

    explicit Gen4FollowCamera(Gen4CameraPreset preset);

    void setTarget(Vec3 target);
    void setManualPose(Vec3 position, float yaw_deg, float pitch_deg);
    void setNearClip(float near_clip);
    bool worldToScreen(const Vec3& world, int viewport_w, int viewport_h, float& out_x, float& out_y, float& out_depth) const;
    float perspectiveScale(float depth) const;
    // SDL-style screen Y offset (negative = move sprite up on screen) converted to world units along up.
    Vec3 screenOffsetToWorldOffset(int offset_y_px, float depth, int viewport_h) const;
    Pose pose() const;

private:
    Gen4CameraPreset preset_;
    bool manual_mode_ = false;
    Vec3 target_{};
    Vec3 position_{};
    Vec3 forward_{};
    Vec3 right_{};
    Vec3 up_{};

    void rebuildBasis();
};

} // namespace pr::gameplay::world3d::camera
