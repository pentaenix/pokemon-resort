#include "gameplay/world3d/characters/CharacterController.hpp"

#include <algorithm>
#include <utility>

namespace pr::gameplay::world3d::characters {

CharacterController::CharacterController(
    const SceneConfig& scene,
    float move_speed_units_per_second,
    float turn_step_delay_seconds) {
    move_speed_units_per_second_ = std::max(1.0f, move_speed_units_per_second);
    turn_step_delay_seconds_ = std::clamp(turn_step_delay_seconds, 0.0f, 0.25f);

    std::shared_ptr<CharacterTerrainQuery> terrain_query = makeLocalCharacterTerrainQuery(scene);
    const float tile_size = terrain_query->tileSize();
    const int grid_width = std::max(1, scene.grid.width);
    const int grid_height = std::max(1, scene.grid.height);
    const int tile_x = std::clamp(scene.player.spawn_tile_x, 0, grid_width - 1);
    const int tile_y = std::clamp(scene.player.spawn_tile_y, 0, grid_height - 1);

    camera::Vec3 pos{};
    pos.x = (static_cast<float>(tile_x) + 0.5f) * tile_size;
    pos.z = (static_cast<float>(tile_y) + 0.5f) * tile_size;
    pos.y = terrain_query->tileWorldHeight(tile_x, tile_y);

    motor_.setTerrainQuery(std::move(terrain_query));
    motor_.setMoveSpeedUnitsPerSecond(move_speed_units_per_second_);
    motor_.resetToTile(tile_x, tile_y, pos);
    facing_ = scene.player.facing;
}

void CharacterController::setMoveSpeedUnitsPerSecond(float speed) {
    move_speed_units_per_second_ = std::max(1.0f, speed);
    motor_.setMoveSpeedUnitsPerSecond(move_speed_units_per_second_);
}

void CharacterController::setTerrainQuery(std::shared_ptr<CharacterTerrainQuery> terrain_query) {
    motor_.setTerrainQuery(std::move(terrain_query));
}

CharacterController::MovementSegment CharacterController::movementSegment() const {
    return MovementSegment{
        motor_.moving(),
        motor_.stepStartX(),
        motor_.stepStartY(),
        motor_.stepDestX(),
        motor_.stepDestY(),
        motor_.moveT()};
}

terrain::ActorTerrainBinding CharacterController::terrainBinding() const {
    return motor_.terrainBinding();
}

CharacterController::MoveInputResult CharacterController::moveInput(
    int dx,
    int dy,
    double dt,
    const std::function<bool(int from_tx, int from_ty, int to_tx, int to_ty)>& can_enter_tile) {
    MoveInputResult result{};

    bool finished_step_this_tick = false;
    if (motor_.moving()) {
        finished_step_this_tick = motor_.update(dt);
        if (motor_.moving()) {
            return result;
        }
    } else {
        motor_.update(dt);
    }

    if (dx == 0 && dy == 0) {
        pending_turn_active_ = false;
        pending_turn_dx_ = 0;
        pending_turn_dy_ = 0;
        pending_turn_elapsed_s_ = 0.0f;
        if (!finished_step_this_tick) {
            motor_.stop();
        }
        return result;
    }

    if (std::abs(dx) >= std::abs(dy)) {
        dy = 0;
        dx = (dx > 0) ? 1 : -1;
    } else {
        dx = 0;
        dy = (dy > 0) ? 1 : -1;
    }

    FacingDirection desired_facing = facing_;
    if (dy < 0) desired_facing = FacingDirection::North;
    if (dy > 0) desired_facing = FacingDirection::South;
    if (dx < 0) desired_facing = FacingDirection::West;
    if (dx > 0) desired_facing = FacingDirection::East;

    if (desired_facing != facing_) {
        facing_ = desired_facing;
        pending_turn_facing_ = desired_facing;
        pending_turn_dx_ = dx;
        pending_turn_dy_ = dy;
        pending_turn_elapsed_s_ = 0.0f;
        pending_turn_active_ = true;
        motor_.stop();
        return result;
    }

    if (pending_turn_active_ &&
        pending_turn_facing_ == desired_facing &&
        pending_turn_dx_ == dx &&
        pending_turn_dy_ == dy) {
        pending_turn_elapsed_s_ += static_cast<float>(dt);
        if (pending_turn_elapsed_s_ < turn_step_delay_seconds_) {
            motor_.stop();
            return result;
        }
    }
    pending_turn_active_ = false;
    pending_turn_dx_ = 0;
    pending_turn_dy_ = 0;
    pending_turn_elapsed_s_ = 0.0f;

    const GridActorMotor::StepResult step = motor_.tryStartStep(dx, dy, can_enter_tile);
    result.attempted_step = step.attempted_step;
    result.blocked = step.blocked;
    return result;
}

void CharacterController::stop() {
    pending_turn_active_ = false;
    pending_turn_dx_ = 0;
    pending_turn_dy_ = 0;
    pending_turn_elapsed_s_ = 0.0f;
    motor_.stop();
}

} // namespace pr::gameplay::world3d::characters
