#pragma once

namespace pr::gameplay::world3d::camera {

struct Gen4CameraPreset {
    float distance = 666.922119f;
    float pitch_deg = -59.051514f;
    float yaw_deg = 0.0f;
    float roll_deg = 0.0f;
    float near_clip = 150.0f;
    float far_clip = 900.0f;
    float fov_y_deg = 30.0f;
    float aspect_width = 4.0f;
    float aspect_height = 3.0f;
};

Gen4CameraPreset loadGen4PresetById(const char* preset_id);

} // namespace pr::gameplay::world3d::camera
