#include "gameplay/world3d/camera/Gen4CameraPreset.hpp"

#include <cstring>

namespace pr::gameplay::world3d::camera {

Gen4CameraPreset loadGen4PresetById(const char* preset_id) {
    Gen4CameraPreset preset;
    if (!preset_id) {
        return preset;
    }
    if (std::strcmp(preset_id, "gen4_platinum_default_exterior") == 0) {
        return preset;
    }
    return preset;
}

} // namespace pr::gameplay::world3d::camera
