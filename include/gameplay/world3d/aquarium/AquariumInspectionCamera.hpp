#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/aquarium/AquariumSimulation.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"

#include <cstddef>
#include <vector>

namespace pr::gameplay::world3d::aquarium {

// Owns the temporary camera pose used when the player inspects a tank. It does
// not own input or the follow camera, so maps without aquariums pay no update cost.
class AquariumInspectionCamera {
public:
    enum class Stage { Inactive, Inspecting, Focused, Pokemon, Returning };

    explicit AquariumInspectionCamera(std::vector<AquariumTankRuntime> tanks = {});

    bool tryBegin(
        camera::Vec3 player_position,
        FacingDirection facing,
        float tile_size,
        const camera::Gen4FollowCamera& current_camera);
    bool enterFocused(float tile_size, camera::Gen4FollowCamera& camera,float aspect=1.6f);
    bool focusPokemon(const std::string& id,camera::Vec3 position,float radius,camera::Gen4FollowCamera& camera);
    bool leavePokemon();
    void trackPokemon(camera::Vec3 position);
    void setPointerLook(float x,float y);
    const std::string& focusedPokemonId() const { return pokemon_id_; }
    std::vector<std::string> hiddenPlacementIds(const camera::Gen4FollowCamera::Pose& pose) const;
    void beginExit(camera::Vec3 player_position, camera::Gen4FollowCamera& camera);
    void updateReturnTarget(camera::Vec3 player_position);
    void update(double dt_seconds, camera::Gen4FollowCamera& camera);

    bool active() const { return stage_ != Stage::Inactive; }
    bool focused() const { return stage_ == Stage::Focused; }
    bool hidesOverworldActors() const { return hide_overworld_actors_; }
    float wallClipRadiusWorld(float tile_size) const;
    bool returning() const { return stage_ == Stage::Returning; }
    Stage stage() const { return stage_; }
    const std::string& activePlacementId() const { return active_placement_id_; }

private:
    void close();

    std::vector<AquariumTankRuntime> tanks_;
    Stage stage_ = Stage::Inactive;
    bool hide_overworld_actors_ = false;
    std::size_t active_tank_index_ = 0;
    FacingDirection approach_facing_ = FacingDirection::North;
    std::string active_placement_id_;
    float smooth_ = 0.0f;
    float return_smooth_ = 2400.0f;
    float original_near_clip_ = 150.0f;
    float original_fov_=30,current_fov_=30,desired_fov_=30,pokemon_radius_=3;
    camera::Vec3 return_target_{};
    camera::Vec3 return_position_{};
    camera::Vec3 current_position_{};
    camera::Vec3 current_look_at_{};
    camera::Vec3 desired_position_{};
    camera::Vec3 desired_look_at_{};
    camera::Vec3 overview_position_{},overview_target_{};
    std::string pokemon_id_;
    float pointer_x_=0,pointer_y_=0,look_x_=0,look_y_=0;
};

} // namespace pr::gameplay::world3d::aquarium
