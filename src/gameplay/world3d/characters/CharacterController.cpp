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

constexpr float kTurnOnlyTapWindowSeconds = 0.065f;

} // namespace

CharacterController::CharacterController(const SceneConfig& scene) {
    tile_size_ = scene.grid.tile_size;
    grid_width_ = std::max(1, scene.grid.width);
    grid_height_ = std::max(1, scene.grid.height);
    base_spawn_height_ = scene.player.spawn_height;
    terrain_heights_ = scene.terrain.heights;
    terrain_specials_ = scene.terrain.specials;
    collision_map_ = scene.terrain.collision;
    terrain_scene_.grid = scene.grid;
    terrain_scene_.terrain.heights = terrain_heights_;
    terrain_scene_.terrain.specials = terrain_specials_;
    tile_x_ = std::clamp(scene.player.spawn_tile_x, 0, grid_width_ - 1);
    tile_y_ = std::clamp(scene.player.spawn_tile_y, 0, grid_height_ - 1);
    step_dest_x_ = tile_x_;
    step_dest_y_ = tile_y_;
    pos_.x = (static_cast<float>(tile_x_) + 0.5f) * tile_size_;
    pos_.z = (static_cast<float>(tile_y_) + 0.5f) * tile_size_;
    pos_.y = tileWorldHeight(tile_x_, tile_y_);
    move_start_ = pos_;
    move_target_ = pos_;
    facing_ = scene.player.facing;
}

float CharacterController::tileWorldHeight(int tx, int ty) const {
    return terrain::heightAtTileCenter(terrain_scene_, tx, ty);
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
    return terrain::bindActorStanding(terrain_scene_, tile_x_, tile_y_, pos_.x, pos_.z);
}

int CharacterController::tileBaseHeightUnits(int tx, int ty) const {
    if (terrain_heights_.empty()) return static_cast<int>(std::round(base_spawn_height_ / std::max(0.001f, tile_size_)));
    if (ty < 0 || ty >= static_cast<int>(terrain_heights_.size())) return 0;
    const auto& row = terrain_heights_[static_cast<std::size_t>(ty)];
    if (tx < 0 || tx >= static_cast<int>(row.size())) return 0;
    return static_cast<int>(row[static_cast<std::size_t>(tx)]);
}

int CharacterController::tileSpecial(int tx, int ty) const {
    if (terrain_specials_.empty()) return 0;
    if (ty < 0 || ty >= static_cast<int>(terrain_specials_.size())) return 0;
    const auto& row = terrain_specials_[static_cast<std::size_t>(ty)];
    if (tx < 0 || tx >= static_cast<int>(row.size())) return 0;
    return static_cast<int>(row[static_cast<std::size_t>(tx)]);
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
    if (collision_map_.empty()) return false;
    if (ty < 0 || ty >= static_cast<int>(collision_map_.size())) return false;
    const auto& row = collision_map_[static_cast<std::size_t>(ty)];
    if (tx < 0 || tx >= static_cast<int>(row.size())) return false;
    return row[static_cast<std::size_t>(tx)] != 0;
}

void CharacterController::moveInput(int dx, int dy, double dt) {
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
            if (step_motor_.interpolate_y) {
                pos_.y = terrain::actorHeightDuringStep(terrain_scene_, pos_.x, pos_.z, step_motor_, move_t_);
            } else {
                pos_.y = move_start_.y;
            }
        }
        if (moving_) {
            return;
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
        return;
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
        return;
    }

    if (pending_turn_active_ &&
        pending_turn_facing_ == desired_facing &&
        pending_turn_dx_ == dx &&
        pending_turn_dy_ == dy) {
        pending_turn_elapsed_s_ += static_cast<float>(dt);
        if (pending_turn_elapsed_s_ < kTurnOnlyTapWindowSeconds) {
            moving_ = false;
            return;
        }
    }
    pending_turn_active_ = false;
    pending_turn_dx_ = 0;
    pending_turn_dy_ = 0;
    pending_turn_elapsed_s_ = 0.0f;

    const int dest_x = tile_x_ + dx;
    const int dest_y = tile_y_ + dy;
    if (dest_x < 0 || dest_x >= grid_width_ || dest_y < 0 || dest_y >= grid_height_) {
        moving_ = false;
        return;
    }
    if (tileBlocked(dest_x, dest_y)) {
        moving_ = false;
        return;
    }
    bool smooth_ramp = false;
    if (!canTraverseHeightDelta(tile_x_, tile_y_, dest_x, dest_y, dx, dy, smooth_ramp)) {
        moving_ = false;
        return;
    }

    step_dest_x_ = dest_x;
    step_dest_y_ = dest_y;
    step_motor_ = terrain::GridStepMotor::beginStep(
        terrain_scene_,
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
    move_target_.y = terrain::bindActorStanding(
        terrain_scene_, step_dest_x_, step_dest_y_, move_target_.x, move_target_.z)
                         .simulation_y;
    move_t_ = 0.0f;
    moving_ = true;
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
