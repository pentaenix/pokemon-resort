#pragma once

#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"
#include "gameplay/world3d/characters/CharacterTerrainQuery.hpp"

#include <functional>
#include <memory>

namespace pr::gameplay::world3d::characters {

class GridActorMotor {
public:
    struct StepResult {
        bool attempted_step = false;
        bool started = false;
        bool blocked = false;
    };

    void setTerrainQuery(std::shared_ptr<CharacterTerrainQuery> terrain_query);
    void resetToTile(int tx, int ty, const camera::Vec3& position);
    bool teleportToTile(int tx, int ty, bool allow_outside = false);
    void offsetWorldPosition(float x, float z);
    void setMoveSpeedUnitsPerSecond(float speed);

    StepResult tryStartStep(
        int dx,
        int dy,
        const std::function<bool(int from_tx, int from_ty, int to_tx, int to_ty)>& can_enter_tile = {});

    bool update(double dt);
    void stop();

    terrain::ActorTerrainBinding terrainBinding() const;
    bool onActualWater() const;

    bool moving() const { return moving_; }
    const camera::Vec3& position() const { return pos_; }
    const camera::Vec3& moveTarget() const { return move_target_; }
    int tileX() const { return tile_x_; }
    int tileY() const { return tile_y_; }
    int stepStartX() const { return step_start_x_; }
    int stepStartY() const { return step_start_y_; }
    int stepDestX() const { return step_dest_x_; }
    int stepDestY() const { return step_dest_y_; }
    float moveT() const { return move_t_; }
    float tileSize() const { return tile_size_; }

private:
    bool canTraverseHeightDelta(int from_x, int from_y, int to_x, int to_y, int dx, int dy, bool& smooth_ramp) const;
    int tileBaseHeightUnits(int tx, int ty) const;
    float tileWorldHeight(int tx, int ty) const;
    int tileSpecial(int tx, int ty) const;
    int rampDirection(int tx, int ty) const;
    bool tileBlocked(int tx, int ty) const;

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
    float tile_size_ = 16.0f;
    float move_speed_units_per_second_ = 64.0f;
    bool moving_ = false;
};

} // namespace pr::gameplay::world3d::characters
