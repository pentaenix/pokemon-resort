#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"
#include "gameplay/world3d/characters/SpriteSheetAnimator.hpp"
#include "gameplay/world3d/effects/LandingDustSystem.hpp"
#include "gameplay/world3d/followers/FollowerConfig.hpp"
#include "gameplay/world3d/followers/FollowerIdleExitTarget.hpp"
#include "gameplay/world3d/followers/NatureIdleConfig.hpp"
#include "gameplay/world3d/followers/NatureIdlePlanner.hpp"
#include "gameplay/world3d/rendering/BillboardSpriteRenderer.hpp"

#include <SDL.h>
#include <deque>
#include <memory>
#include <optional>
#include <random>

namespace pr::gameplay::world3d::followers {

class FollowerController {
public:
    FollowerController(
        const std::string& project_root,
        const SceneConfig& scene,
        const FollowerSummonConfig& summon_config,
        const FollowerSessionConfig& session_config);

    void initialize(SDL_Renderer* renderer);
    void update(
        double dt,
        const camera::Vec3& player_world_pos,
        FacingDirection player_facing,
        bool player_idle,
        bool player_activity);
    void render(
        SDL_Renderer* renderer,
        const camera::Gen4FollowCamera& camera,
        int viewport_w,
        int viewport_h,
        float tint_r,
        float tint_g,
        float tint_b,
        float brightness);

    bool activeForRender() const;
    std::optional<float> renderDepth(
        const camera::Gen4FollowCamera& camera,
        int viewport_w,
        int viewport_h) const;
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

    State state_ = State::Hidden;
    CharacterSpriteDefinition follower_def_{};
    std::unique_ptr<characters::SpriteSheetAnimator> follower_animator_;
    std::unique_ptr<rendering::BillboardSpriteRenderer> follower_renderer_;
    CharacterSpriteDefinition ball_def_{};
    std::unique_ptr<rendering::BillboardSpriteRenderer> ball_renderer_;
    NatureIdleBehaviorConfig idle_config_{};

    camera::Vec3 follower_pos_{};
    camera::Vec3 ball_pos_{};
    camera::Vec3 follower_move_from_{};
    camera::Vec3 follower_move_to_{};
    FacingDirection follower_facing_ = FacingDirection::South;
    std::vector<int> ball_frames_;
    double state_elapsed_seconds_ = 0.0;
    std::deque<TilePoint> path_;
    std::deque<IdleAction> idle_actions_;
    TilePoint last_player_tile_{};
    TilePoint player_tile_{};
    TilePoint follower_tile_{};
    TilePoint idle_origin_tile_{};
    bool have_last_player_tile_ = false;
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
    std::mt19937 rng_{};

    int tileHeightUnits(int tx, int ty) const;
    camera::Vec3 tileToWorldCenter(int tx, int ty) const;
    void beginStepToTile(const TilePoint& target, double speed_multiplier, bool hop_movement);
    void updateActiveStep(double dt);
    void updateNormalFollow();
    void updateNatureIdle(double dt);
    void cancelNatureIdle();
    bool beginNextIdleAction();
    void finishNatureIdle(bool natural_end);
    void updateManualDebugAction(double dt);
};

} // namespace pr::gameplay::world3d::followers
