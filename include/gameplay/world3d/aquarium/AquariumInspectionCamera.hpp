#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/aquarium/AquariumSimulation.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"

#include <vector>

namespace pr::gameplay::world3d::aquarium {

// Owns the temporary camera pose used when the player inspects a tank. It does
// not own input or the follow camera, so maps without aquariums pay no update cost.
class AquariumInspectionCamera {
public:
    explicit AquariumInspectionCamera(std::vector<AquariumTankRuntime> tanks = {});

    bool tryBegin(
        camera::Vec3 player_position,
        FacingDirection facing,
        float tile_size,
        const camera::Gen4FollowCamera& current_camera);
    void update(double dt_seconds, camera::Gen4FollowCamera& camera);
    void close();

    bool active() const { return active_; }
    const std::string& activePlacementId() const { return active_placement_id_; }

private:
    std::vector<AquariumTankRuntime> tanks_;
    bool active_ = false;
    std::string active_placement_id_;
    float smooth_ = 0.0f;
    camera::Vec3 current_position_{};
    camera::Vec3 current_look_at_{};
    camera::Vec3 desired_position_{};
    camera::Vec3 desired_look_at_{};
};

} // namespace pr::gameplay::world3d::aquarium
