#include "gameplay/world3d/characters/CharacterController.hpp"

#include <algorithm>

namespace pr::gameplay::world3d::characters {

namespace {

bool isSlopeSpecial(int special) {
    return special >= 2 && special <= 13;
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
    tile_x_ = std::clamp(scene.player.spawn_tile_x, 0, grid_width_ - 1);
    tile_y_ = std::clamp(scene.player.spawn_tile_y, 0, grid_height_ - 1);
    pos_.x = (static_cast<float>(tile_x_) + 0.5f) * tile_size_;
    pos_.z = (static_cast<float>(tile_y_) + 0.5f) * tile_size_;
    pos_.y = tileWorldHeight(tile_x_, tile_y_);
    move_start_ = pos_;
    move_target_ = pos_;
    facing_ = scene.player.facing;
}

float CharacterController::tileWorldHeight(int tx, int ty) const {
    const int h = tileBaseHeightUnits(tx, ty);
    float y = static_cast<float>(h) * tile_size_;
    const int special = tileSpecial(tx, ty);
    if (isSlopeSpecial(special)) {
        // Ramp tile occupant stands in the middle of the incline.
        y += tile_size_ * 0.5f;
    }
    return y;
}

float CharacterController::worldHeightAtPosition(float world_x, float world_z, int fallback_tx, int fallback_ty) const {
    const float safe_tile = std::max(0.001f, tile_size_);
    int tx = static_cast<int>(std::floor(world_x / safe_tile));
    int ty = static_cast<int>(std::floor(world_z / safe_tile));
    if (tx < 0 || tx >= grid_width_ || ty < 0 || ty >= grid_height_) {
        tx = fallback_tx;
        ty = fallback_ty;
    }

    const int h = tileBaseHeightUnits(tx, ty);
    const float base = static_cast<float>(h) * tile_size_;
    const int special = tileSpecial(tx, ty);
    const float local_x = (world_x - (static_cast<float>(tx) * tile_size_)) / safe_tile;
    const float local_z = (world_z - (static_cast<float>(ty) * tile_size_)) / safe_tile;
    const float u = std::clamp(local_x, 0.0f, 1.0f);
    const float v = std::clamp(local_z, 0.0f, 1.0f);

    if (special < 2 || special > 13) {
        return tileWorldHeight(tx, ty);
    }

    if (special >= 6 && special <= 13) {
        // Match renderer corner order:
        // c0=(x0,z0), c1=(x1,z0), c2=(x1,z1), c3=(x0,z1)
        float c0 = base;
        float c1 = base;
        float c2 = base;
        float c3 = base;
        const float high = base + tile_size_;
        switch (special) {
            case 6: c2 = high; break;                     // convex NE
            case 7: c1 = high; break;                     // convex SE (project orientation)
            case 8: c0 = high; break;                     // convex SW
            case 9: c3 = high; break;                     // convex NW
            case 10: c0 = high; c1 = high; c3 = high; break; // concave NE
            case 11: c0 = high; c3 = high; break;           // concave SE
            case 12: c2 = high; break;                      // concave SW
            case 13: c1 = high; c2 = high; break;           // concave NW
            default: break;
        }
        const float north = c0 + ((c1 - c0) * u);
        const float south = c3 + ((c2 - c3) * u);
        return north + ((south - north) * v);
    }

    if (special == 2) return base + ((1.0f - v) * tile_size_); // north ascends toward smaller tile y
    if (special == 3) return base + (u * tile_size_);           // east
    if (special == 4) return base + (v * tile_size_);           // south
    return base + ((1.0f - u) * tile_size_);                    // west
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
    if (dh == 0) {
        const bool from_is_ramp = isSlopeSpecial(tileSpecial(from_x, from_y));
        const bool to_is_ramp = isSlopeSpecial(tileSpecial(to_x, to_y));
        // Smoothly blend onto/off ramp midpoint even when base tile heights match.
        smooth_ramp = from_is_ramp || to_is_ramp;
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
    const bool valid = ramp_allows(from_x, from_y) || ramp_allows(to_x, to_y);
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
            if (interpolate_y_during_step_) {
                pos_.y = worldHeightAtPosition(pos_.x, pos_.z, tile_x_, tile_y_);
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
        // Preserve walking state for the frame that completed a step so animation
        // does not reset between chained grid steps while input is held.
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

    const int next_x = tile_x_ + dx;
    const int next_y = tile_y_ + dy;
    if (next_x < 0 || next_x >= grid_width_ || next_y < 0 || next_y >= grid_height_) {
        moving_ = false;
        return;
    }
    if (tileBlocked(next_x, next_y)) {
        moving_ = false;
        return;
    }
    bool smooth_ramp = false;
    if (!canTraverseHeightDelta(tile_x_, tile_y_, next_x, next_y, dx, dy, smooth_ramp)) {
        moving_ = false;
        return;
    }
    tile_x_ = next_x;
    tile_y_ = next_y;

    move_start_ = pos_;
    move_target_.x = (static_cast<float>(tile_x_) + 0.5f) * tile_size_;
    move_target_.z = (static_cast<float>(tile_y_) + 0.5f) * tile_size_;
    move_target_.y = tileWorldHeight(tile_x_, tile_y_);
    interpolate_y_during_step_ = smooth_ramp;
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
