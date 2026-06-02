#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"
#include <cstdint>
#include <vector>

namespace pr::gameplay::world3d::characters {

class CharacterController {
public:
    explicit CharacterController(const SceneConfig& scene);

    void moveInput(int dx, int dy, double dt);
    void stop();

    FacingDirection facing() const { return facing_; }
    bool moving() const { return moving_; }
    camera::Vec3 position() const { return pos_; }

private:
    float move_speed_units_per_second_ = 64.0f;
    float tile_size_ = 16.0f;
    int grid_width_ = 32;
    int grid_height_ = 32;
    float base_spawn_height_ = 0.0f;
    std::vector<std::vector<std::uint8_t>> terrain_heights_;
    std::vector<std::vector<std::uint8_t>> terrain_specials_;
    std::vector<std::vector<std::uint8_t>> collision_map_;
    camera::Vec3 pos_{};
    camera::Vec3 move_start_{};
    camera::Vec3 move_target_{};
    int tile_x_ = 0;
    int tile_y_ = 0;
    float move_t_ = 1.0f;
    FacingDirection facing_ = FacingDirection::South;
    bool moving_ = false;
    bool interpolate_y_during_step_ = false;
    FacingDirection pending_turn_facing_ = FacingDirection::South;
    int pending_turn_dx_ = 0;
    int pending_turn_dy_ = 0;
    float pending_turn_elapsed_s_ = 0.0f;
    bool pending_turn_active_ = false;

    int tileBaseHeightUnits(int tx, int ty) const;
    float tileWorldHeight(int tx, int ty) const;
    float worldHeightAtPosition(float world_x, float world_z, int fallback_tx, int fallback_ty) const;
    int tileSpecial(int tx, int ty) const;
    int rampDirection(int tx, int ty) const;
    static void rampAscendVector(int direction, int& out_dx, int& out_dy);
    bool canTraverseHeightDelta(int from_x, int from_y, int to_x, int to_y, int dx, int dy, bool& smooth_ramp) const;
    bool tileBlocked(int tx, int ty) const;
};

} // namespace pr::gameplay::world3d::characters
