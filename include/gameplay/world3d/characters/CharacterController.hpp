#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"
#include "gameplay/world3d/characters/CharacterTerrainQuery.hpp"
#include "gameplay/world3d/terrain/ActorTerrainBinding.hpp"
#include "gameplay/world3d/terrain/GridStepMotor.hpp"
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
    bool moving() const { return moving_; }
    camera::Vec3 position() const { return pos_; }
    int tileX() const { return tile_x_; }
    int tileY() const { return tile_y_; }
    MovementSegment movementSegment() const;
    terrain::ActorTerrainBinding terrainBinding() const;

private:
    float move_speed_units_per_second_ = 64.0f;
    float turn_step_delay_seconds_ = 0.016f;
    float tile_size_ = 16.0f;
    int grid_width_ = 32;
    int grid_height_ = 32;
    float base_spawn_height_ = 0.0f;
    std::shared_ptr<CharacterTerrainQuery> terrain_query_;
    camera::Vec3 pos_{};
    camera::Vec3 move_start_{};
    camera::Vec3 move_target_{};
    int tile_x_ = 0;
    int tile_y_ = 0;
    int step_start_x_ = 0;
    int step_start_y_ = 0;
    int step_dest_x_ = 0;
    int step_dest_y_ = 0;
    terrain::GridStepMotor step_motor_{};
    float move_t_ = 1.0f;
    SceneConfig terrain_scene_{};
    FacingDirection facing_ = FacingDirection::South;
    bool moving_ = false;
    FacingDirection pending_turn_facing_ = FacingDirection::South;
    int pending_turn_dx_ = 0;
    int pending_turn_dy_ = 0;
    float pending_turn_elapsed_s_ = 0.0f;
    bool pending_turn_active_ = false;

    int tileBaseHeightUnits(int tx, int ty) const;
    float tileWorldHeight(int tx, int ty) const;
    int tileSpecial(int tx, int ty) const;
    int rampDirection(int tx, int ty) const;
    static void rampAscendVector(int direction, int& out_dx, int& out_dy);
    bool canTraverseHeightDelta(int from_x, int from_y, int to_x, int to_y, int dx, int dy, bool& smooth_ramp) const;
    bool tileBlocked(int tx, int ty) const;
};

} // namespace pr::gameplay::world3d::characters
