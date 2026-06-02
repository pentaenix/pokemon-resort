#pragma once

#include "gameplay/world3d/camera/Gen4CameraPreset.hpp"

#include <SDL.h>

namespace pr::gameplay::world3d::camera {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

class Gen4FollowCamera {
public:
    explicit Gen4FollowCamera(Gen4CameraPreset preset);

    void setTarget(Vec3 target);
    void setManualPose(Vec3 position, float yaw_deg, float pitch_deg);
    bool worldToScreen(const Vec3& world, int viewport_w, int viewport_h, float& out_x, float& out_y, float& out_depth) const;
    float perspectiveScale(float depth) const;

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
