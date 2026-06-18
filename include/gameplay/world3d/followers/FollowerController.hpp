#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"
#include "gameplay/world3d/characters/CharacterController.hpp"
#include "gameplay/world3d/characters/CharacterMovementConfig.hpp"
#include "gameplay/world3d/characters/SpriteSheetAnimator.hpp"
#include "gameplay/world3d/effects/LandingDustSystem.hpp"
#include "gameplay/world3d/followers/FollowerConfig.hpp"
#include "gameplay/world3d/followers/FollowerIdleExitTarget.hpp"
#include "gameplay/world3d/followers/NatureIdleConfig.hpp"
#include "gameplay/world3d/followers/NatureIdlePlanner.hpp"
#include "gameplay/world3d/rendering/BillboardPlacement.hpp"
#include "gameplay/world3d/terrain/ActorTerrainBinding.hpp"
#include "gameplay/world3d/terrain/GridStepMotor.hpp"

#include <SDL.h>
#include <deque>
#include <memory>
#include <optional>
#include <random>
#include <utility>

namespace pr::gameplay::world3d::followers {

class FollowerController {
public:
    FollowerController(
        const std::string& project_root,
        const SceneConfig& scene,
        const FollowerSummonConfig& summon_config,
        const FollowerSessionConfig& session_config);

    // Loads charbin packages without SDL; required for update() on bgfx path.
    bool initializeResources();
    bool resourcesReady() const { return resources_ready_; }

    void collectBillboardDraws(
        const camera::Gen4FollowCamera& camera,
        int viewport_w,
        int viewport_h,
        std::vector<rendering::CharacterBillboardDraw>& out) const;
    void update(
        double dt,
        const camera::Vec3& player_world_pos,
        FacingDirection player_facing,
        const characters::CharacterController::MovementSegment& player_segment,
        bool player_idle,
        bool player_activity,
        bool player_running);
    bool visibleForSimulation() const;
    std::optional<float> renderDepth(
        const camera::Gen4FollowCamera& camera,
        int viewport_w,
        int viewport_h) const;
    std::vector<std::pair<int, int>> reservedTiles() const;
    bool triggerDebugJump();
    bool triggerDebugPoke();
    std::string debugActivityLabel() const;
    std::optional<effects::LandingDustSpawnRequest> consumeLandingDustSpawn();

private:
    enum class State {
        Hidden,
        BallRelease,
        EntryFlash,
        Active
    };

    struct TilePoint {
        int x = 0;
        int y = 0;
    };

    enum class ManualDebugActionType {
        None,
        Jump,
        Poke
    };

    std::string project_root_;
    const SceneConfig& scene_;
    FollowerSummonConfig summon_config_{};
    FollowerSessionConfig session_config_{};
    characters::CharacterMovementConfig movement_config_{};

    State state_ = State::Hidden;
    CharacterSpriteDefinition follower_def_{};
    std::unique_ptr<characters::SpriteSheetAnimator> follower_animator_;
    CharacterSpriteDefinition ball_def_{};
    NatureIdleBehaviorConfig idle_config_{};

    camera::Vec3 follower_pos_{};
    camera::Vec3 ball_pos_{};
    camera::Vec3 follower_move_from_{};
    camera::Vec3 follower_move_to_{};
    FacingDirection follower_facing_ = FacingDirection::South;
    std::vector<int> ball_frames_;
    double state_elapsed_seconds_ = 0.0;
    std::deque<TilePoint> path_;
    std::deque<TilePoint> player_step_trail_;
    std::deque<IdleAction> idle_actions_;
    TilePoint last_player_tile_{};
    TilePoint player_tile_{};
    TilePoint follower_tile_{};
    TilePoint step_dest_tile_{};
    terrain::GridStepMotor step_motor_{};
    terrain::GridStepMotor replay_step_motor_{};
    TilePoint idle_origin_tile_{};
    TilePoint last_player_segment_from_{};
    TilePoint last_player_segment_to_{};
    TilePoint replay_from_tile_{};
    TilePoint replay_to_tile_{};
    bool have_last_player_tile_ = false;
    bool have_last_player_segment_ = false;
    bool replay_follow_active_ = false;
    bool follower_moving_ = false;
    float follower_move_t_ = 1.0f;
    float follower_step_duration_s_ = 0.25f;
    FacingDirection idle_origin_facing_ = FacingDirection::South;
    FacingDirection player_facing_ = FacingDirection::South;
    camera::Vec3 render_offset_{};
    int render_screen_offset_y_px_ = 0;
    double idle_seconds_ = 0.0;
    double idle_cooldown_seconds_ = 0.0;
    double cancel_soft_snap_elapsed_seconds_ = 0.0;
    double action_elapsed_seconds_ = 0.0;
    double current_step_speed_multiplier_ = 1.0;
    double current_step_hop_height_tiles_ = 0.0;
    bool player_idle_ = false;
    bool player_running_ = false;
    bool idle_behavior_active_ = false;
    bool returning_to_origin_ = false;
    bool cancel_return_active_ = false;
    bool sleep_action_active_ = false;
    ManualDebugActionType manual_debug_action_ = ManualDebugActionType::None;
    double manual_debug_elapsed_seconds_ = 0.0;
    std::string active_behavior_label_ = "none";
    int action_jump_landings_emitted_ = 0;
    std::optional<effects::LandingDustSpawnRequest> pending_landing_dust_spawn_;
    bool initialized_rng_ = false;
    bool resources_ready_ = false;
    std::mt19937 rng_{};

    terrain::ActorTerrainBinding terrainBinding() const;
    int tileHeightUnits(int tx, int ty) const;
    camera::Vec3 tileToWorldCenter(int tx, int ty) const;
    void beginStepToTile(const TilePoint& target, double speed_multiplier, bool hop_movement);
    void updateActiveStep(double dt);
    bool updateReplayFollow(const characters::CharacterController::MovementSegment& player_segment);
    void updateNormalFollow();
    void updateNatureIdle(double dt);
    void cancelNatureIdle();
    bool beginNextIdleAction();
    void finishNatureIdle(bool natural_end);
    void updateManualDebugAction(double dt);
};

} // namespace pr::gameplay::world3d::followers
