#include "ui/Overworld3DTestScreen.hpp"

#include "gameplay/world3d/camera/FreeCameraCapture.hpp"

#include <SDL.h>

#include <cmath>
#include <iostream>
#include <limits>

namespace pr {

void Overworld3DTestScreen::captureFreeCameraPose() {
    if (!freecam_enabled_) return;

    std::string nearest_placement_id;
    float nearest_distance_squared = std::numeric_limits<float>::max();
    if (aquarium_simulation_) {
        for (const auto& tank : aquarium_simulation_->tanks()) {
            const float dx = tank.world_center[0] - player_.position().x;
            const float dy = tank.world_center[1] - player_.position().y;
            const float dz = tank.world_center[2] - player_.position().z;
            const float distance_squared = dx * dx + dy * dy + dz * dz;
            if (distance_squared < nearest_distance_squared) {
                nearest_distance_squared = distance_squared;
                nearest_placement_id = tank.placement_id;
            }
        }
    }

    const std::string record = gameplay::world3d::camera::formatFreeCameraCapture({
        active_world_map_id_,
        nearest_placement_id,
        freecam_pos_,
        freecam_yaw_deg_,
        freecam_pitch_deg_,
        player_.position(),
        player_.facing()});
    const int clipboard_result = SDL_SetClipboardText(record.c_str());
    std::cerr << "[CameraCapture] " << record;
    if (clipboard_result == 0) {
        std::cerr << " (copied to clipboard)";
    } else {
        std::cerr << " (clipboard failed: " << SDL_GetError() << ')';
    }
    std::cerr << '\n';
}

} // namespace pr
