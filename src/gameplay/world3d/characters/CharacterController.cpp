#include "gameplay/world3d/characters/CharacterController.hpp"

#include "gameplay/world3d/terrain/ActorTerrainBinding.hpp"
#include "gameplay/world3d/terrain/GridStepMotor.hpp"
#include "gameplay/world3d/terrain/TerrainSurface.hpp"

#include <algorithm>

namespace pr::gameplay::world3d::characters {

namespace {

bool isSlopeSpecial(int special) {
    return special >= 2 && special <= 13;
}

bool isCardinalRampSpecial(int special) {
    return special >= 2 && special <= 5;
}

} // namespace

CharacterController::CharacterController(
    const SceneConfig& scene,
    float move_speed_units_per_second,
    float turn_step_delay_seconds) {
    move_speed_units_per_second_ = std::max(1.0f, move_speed_units_per_second);
    turn_step_delay_seconds_ = std::clamp(turn_step_delay_seconds, 0.0f, 0.25f);
    tile_size_ = scene.grid.tile_size;
    grid_width_ = std::max(1, scene.grid.width);
    grid_height_ = std::max(1, scene.grid.height);
    base_spawn_height_ = scene.player.spawn_height;
    terrain_scene_.grid = scene.grid;
    terrain_scene_.terrain = scene.terrain;
    terrain_query_ = makeLocalCharacterTerrainQuery(scene);
    tile_x_ = std::clamp(scene.player.spawn_tile_x, 0, grid_width_ - 1);
    tile_y_ = std::clamp(scene.player.spawn_tile_y, 0, grid_height_ - 1);
    step_start_x_ = tile_x_;
    step_start_y_ = tile_y_;
    step_dest_x_ = tile_x_;
    step_dest_y_ = tile_y_;
    pos_.x = (static_cast<float>(tile_x_) + 0.5f) * tile_size_;
    pos_.z = (static_cast<float>(tile_y_) + 0.5f) * tile_size_;
    pos_.y = tileWorldHeight(tile_x_, tile_y_);
    move_start_ = pos_;
    move_target_ = pos_;
    facing_ = scene.player.facing;
}

void CharacterController::setMoveSpeedUnitsPerSecond(float speed) {
    move_speed_units_per_second_ = std::max(1.0f, speed);
}

void CharacterController::setTerrainQuery(std::shared_ptr<CharacterTerrainQuery> terrain_query) {
    if (terrain_query) {
        terrain_query_ = std::move(terrain_query);
        tile_size_ = terrain_query_->tileSize();
        pos_.y = terrainBinding().simulation_y;
        move_start_ = pos_;
        move_target_ = pos_;
    }
}

CharacterController::MovementSegment CharacterController::movementSegment() const {
    return MovementSegment{
        moving_,
        step_start_x_,
        step_start_y_,
        step_dest_x_,
        step_dest_y_,
        move_t_};
}

float CharacterController::tileWorldHeight(int tx, int ty) const {
    return terrain_query_ ? terrain_query_->tileWorldHeight(tx, ty) : 0.0f;
}

terrain::ActorTerrainBinding CharacterController::terrainBinding() const {
    if (moving_ && step_motor_.interpolate_y) {
        const terrain::TileCoord sample = step_motor_.activeSampleTile(move_t_);
        terrain::ActorTerrainBinding binding{};
        binding.logical_tx = tile_x_;
        binding.logical_ty = tile_y_;
        binding.height_sample_tx = sample.x;
        binding.height_sample_ty = sample.y;
        binding.simulation_y = pos_.y;
        return binding;
    }
    return terrain_query_
        ? terrain_query_->bindActorStanding(tile_x_, tile_y_, pos_.x, pos_.z)
        : terrain::ActorTerrainBinding{};
}

int CharacterController::tileBaseHeightUnits(int tx, int ty) const {
    return terrain_query_
        ? terrain_query_->tileBaseHeightUnits(tx, ty)
        : static_cast<int>(std::round(base_spawn_height_ / std::max(0.001f, tile_size_)));
}

int CharacterController::tileSpecial(int tx, int ty) const {
    return terrain_query_ ? terrain_query_->tileSpecial(tx, ty) : 0;
}

int CharacterController::rampDirection(int tx, int ty) const {
    const int special = tileSpecial(tx, ty);
    if (special >= 2 && special <= 5) return special;
    return 0;
}

void CharacterController::rampAscendVector(int direction, int& out_dx, int& out_dy) {
    out_dx = 0;
    out_dy = 0;
    if (direction == 2) out_dy = -1;      // up/north
    else if (direction == 3) out_dx = 1;  // right/east
    else if (direction == 4) out_dy = 1;  // down/south
    else if (direction == 5) out_dx = -1; // left/west
}

bool CharacterController::canTraverseHeightDelta(
    int from_x,
    int from_y,
    int to_x,
    int to_y,
    int dx,
    int dy,
    bool& smooth_ramp) const {
    smooth_ramp = false;
    const int from_h = tileBaseHeightUnits(from_x, from_y);
    const int to_h = tileBaseHeightUnits(to_x, to_y);
    const int dh = to_h - from_h;
    if (terrain_query_ &&
        terrain_query_->canTraverseTerrainEdge(from_x, from_y, to_x, to_y, dx, dy)) {
        const bool from_slope = isSlopeSpecial(tileSpecial(from_x, from_y));
        const bool to_slope = isSlopeSpecial(tileSpecial(to_x, to_y));
        smooth_ramp = from_slope || to_slope || tileWorldHeight(from_x, from_y) != tileWorldHeight(to_x, to_y);
        return true;
    }
    const auto ramp_step_uses_tile = [&](int ramp_tile_x, int ramp_tile_y) -> bool {
        const int dir = rampDirection(ramp_tile_x, ramp_tile_y);
        if (dir == 0) return false;
        int ax = 0;
        int ay = 0;
        rampAscendVector(dir, ax, ay);
        return (dx == ax && dy == ay) || (dx == -ax && dy == -ay);
    };

    if (dh == 0) {
        const int from_special = tileSpecial(from_x, from_y);
        const int to_special = tileSpecial(to_x, to_y);
        const bool from_cardinal = isCardinalRampSpecial(from_special);
        const bool to_cardinal = isCardinalRampSpecial(to_special);
        const bool from_uses_ramp = from_cardinal && ramp_step_uses_tile(from_x, from_y);
        const bool to_uses_ramp = to_cardinal && ramp_step_uses_tile(to_x, to_y);
        const bool lateral_between_matching_ramps =
            from_cardinal && to_cardinal && from_special == to_special;
        if (!lateral_between_matching_ramps &&
            ((from_cardinal && !from_uses_ramp) || (to_cardinal && !to_uses_ramp))) {
            return false;
        }
        const bool from_corner_slope = isSlopeSpecial(from_special) && !from_cardinal;
        const bool to_corner_slope = isSlopeSpecial(to_special) && !to_cardinal;
        smooth_ramp = from_uses_ramp || to_uses_ramp || from_corner_slope || to_corner_slope;
        return true;
    }
    if (std::abs(dh) > 1) return false;
    const auto ramp_allows = [&](int ramp_tile_x, int ramp_tile_y) -> bool {
        const int dir = rampDirection(ramp_tile_x, ramp_tile_y);
        if (dir == 0) return false;
        int ax = 0;
        int ay = 0;
        rampAscendVector(dir, ax, ay);
        if (dh > 0) {
            return dx == ax && dy == ay;
        }
        return dx == -ax && dy == -ay;
    };
    const bool valid = dh > 0 ? ramp_allows(from_x, from_y) : ramp_allows(to_x, to_y);
    smooth_ramp = valid;
    return valid;
}

bool CharacterController::tileBlocked(int tx, int ty) const {
    return terrain_query_ ? terrain_query_->tileBlocked(tx, ty) : false;
}

CharacterController::MoveInputResult CharacterController::moveInput(
    int dx,
    int dy,
    double dt,
    const std::function<bool(int from_tx, int from_ty, int to_tx, int to_ty)>& can_enter_tile) {
    MoveInputResult result{};
    if (!moving_) {
        pos_.y = terrainBinding().simulation_y;
    }

    bool finished_step_this_tick = false;
    if (moving_) {
        const float step_time = std::max(0.001f, tile_size_ / std::max(1.0f, move_speed_units_per_second_));
        move_t_ += static_cast<float>(dt) / step_time;
        if (move_t_ >= 1.0f) {
            move_t_ = 1.0f;
            pos_ = move_target_;
            moving_ = false;
            finished_step_this_tick = true;
        } else {
            pos_.x = move_start_.x + ((move_target_.x - move_start_.x) * move_t_);
            pos_.z = move_start_.z + ((move_target_.z - move_start_.z) * move_t_);
            if (step_motor_.interpolate_y && terrain_query_) {
                pos_.y = terrain_query_->actorHeightDuringStep(pos_.x, pos_.z, step_motor_, move_t_);
            } else {
                pos_.y = move_start_.y;
            }
        }
        if (moving_) {
            return result;
        }
    }

    if (dx == 0 && dy == 0) {
        pending_turn_active_ = false;
        pending_turn_dx_ = 0;
        pending_turn_dy_ = 0;
        pending_turn_elapsed_s_ = 0.0f;
        if (!finished_step_this_tick) {
            moving_ = false;
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
        moving_ = false;
        return result;
    }

    if (pending_turn_active_ &&
        pending_turn_facing_ == desired_facing &&
        pending_turn_dx_ == dx &&
        pending_turn_dy_ == dy) {
        pending_turn_elapsed_s_ += static_cast<float>(dt);
        if (pending_turn_elapsed_s_ < turn_step_delay_seconds_) {
            moving_ = false;
            return result;
        }
    }
    pending_turn_active_ = false;
    pending_turn_dx_ = 0;
    pending_turn_dy_ = 0;
    pending_turn_elapsed_s_ = 0.0f;

    const int dest_x = tile_x_ + dx;
    const int dest_y = tile_y_ + dy;
    result.attempted_step = true;
    if (!terrain_query_ || !terrain_query_->containsTile(dest_x, dest_y)) {
        moving_ = false;
        result.blocked = true;
        return result;
    }
    if (tileBlocked(dest_x, dest_y)) {
        moving_ = false;
        result.blocked = true;
        return result;
    }
    bool smooth_ramp = false;
    if (!canTraverseHeightDelta(tile_x_, tile_y_, dest_x, dest_y, dx, dy, smooth_ramp)) {
        moving_ = false;
        result.blocked = true;
        return result;
    }
    if (can_enter_tile && !can_enter_tile(tile_x_, tile_y_, dest_x, dest_y)) {
        moving_ = false;
        result.blocked = true;
        return result;
    }

    step_start_x_ = tile_x_;
    step_start_y_ = tile_y_;
    step_dest_x_ = dest_x;
    step_dest_y_ = dest_y;
    step_motor_ = terrain_query_->beginStep(
        tile_x_,
        tile_y_,
        dest_x,
        dest_y,
        dx,
        dy,
        tileBaseHeightUnits(tile_x_, tile_y_),
        tileBaseHeightUnits(dest_x, dest_y));
    tile_x_ = dest_x;
    tile_y_ = dest_y;
    move_start_ = pos_;
    move_target_.x = (static_cast<float>(step_dest_x_) + 0.5f) * tile_size_;
    move_target_.z = (static_cast<float>(step_dest_y_) + 0.5f) * tile_size_;
    move_target_.y = terrain_query_->bindActorStanding(
        step_dest_x_, step_dest_y_, move_target_.x, move_target_.z).simulation_y;
    move_t_ = 0.0f;
    moving_ = true;
    return result;
}

void CharacterController::stop() {
    pending_turn_active_ = false;
    pending_turn_dx_ = 0;
    pending_turn_dy_ = 0;
    pending_turn_elapsed_s_ = 0.0f;
    if (!moving_) {
        moving_ = false;
    }
}

} // namespace pr::gameplay::world3d::characters
