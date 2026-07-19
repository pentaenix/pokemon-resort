#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"
#include "gameplay/world3d/characters/CharacterTerrainQuery.hpp"
#include "gameplay/world3d/characters/GridActorMotor.hpp"
#include "gameplay/world3d/terrain/ActorTerrainBinding.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace pr::gameplay::world3d::characters {

class CharacterController {
public:
    struct MovementSegment {
        bool active = false;
        int from_x = 0;
        int from_y = 0;
        int to_x = 0;
        int to_y = 0;
        float t = 1.0f;
    };

    struct MoveInputResult {
        bool attempted_step = false;
        bool blocked = false;
    };

    explicit CharacterController(
        const SceneConfig& scene,
        float move_speed_units_per_second = 64.0f,
        float turn_step_delay_seconds = 0.016f);

    MoveInputResult moveInput(
        int dx,
        int dy,
        double dt,
        const std::function<bool(int from_tx, int from_ty, int to_tx, int to_ty)>& can_enter_tile = {});
    void stop();
    void setMoveSpeedUnitsPerSecond(float speed);
    void setTerrainQuery(std::shared_ptr<CharacterTerrainQuery> terrain_query);

    FacingDirection facing() const { return facing_; }
    bool moving() const { return motor_.moving(); }
    camera::Vec3 position() const { return motor_.position(); }
    int tileX() const { return motor_.tileX(); }
    int tileY() const { return motor_.tileY(); }
    MovementSegment movementSegment() const;
    terrain::ActorTerrainBinding terrainBinding() const;
    bool onActualWater() const { return motor_.onActualWater(); }

private:
    float move_speed_units_per_second_ = 64.0f;
    float turn_step_delay_seconds_ = 0.016f;
    GridActorMotor motor_{};
    FacingDirection facing_ = FacingDirection::South;
    FacingDirection pending_turn_facing_ = FacingDirection::South;
    int pending_turn_dx_ = 0;
    int pending_turn_dy_ = 0;
    float pending_turn_elapsed_s_ = 0.0f;
    bool pending_turn_active_ = false;

};

} // namespace pr::gameplay::world3d::characters
