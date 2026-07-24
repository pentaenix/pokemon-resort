#include "gameplay/world3d/followers/FollowerController.hpp"

#include "gameplay/world3d/interactions/InteractionSequence.hpp"

#include "gameplay/world3d/data/JsonOverworldLoader.hpp"
#include "gameplay/world3d/rendering/BillboardPlacement.hpp"
#include "gameplay/world3d/terrain/ActorTerrainBinding.hpp"
#include "gameplay/world3d/terrain/GridStepMotor.hpp"
#include "gameplay/world3d/terrain/TerrainSurface.hpp"

#include <limits>

#include "gameplay/world3d/data/JsonOverworldLoader.hpp"

#include <algorithm>
#include <cmath>
#include <random>

namespace pr::gameplay::world3d::followers {

namespace {

double ballReleasePhaseSeconds(const FollowerSummonConfig& config) {
    return static_cast<double>(config.ball_animation.duration_ms) / 1000.0;
}

double ballReleaseTotalSeconds(const FollowerSummonConfig& config) {
    return static_cast<double>(config.ball_animation.duration_ms + config.ball_animation.hold_last_frame_ms) / 1000.0;
}

SDL_Rect sourceRectForFrame(const CharacterSpriteDefinition& def, int frame) {
    const int safe_columns = std::max(1, def.columns);
    const int safe_rows = std::max(1, def.rows);
    const int safe_frame = std::max(0, frame);
    const int col = safe_frame % safe_columns;
    const int row = std::min(safe_rows - 1, safe_frame / safe_columns);
    return SDL_Rect{col * def.frame_width, row * def.frame_height, def.frame_width, def.frame_height};
}

camera::Vec3 facingOffset(FacingDirection facing, double amount) {
    if (facing == FacingDirection::North) return camera::Vec3{0.0f, 0.0f, static_cast<float>(-amount)};
    if (facing == FacingDirection::South) return camera::Vec3{0.0f, 0.0f, static_cast<float>(amount)};
    if (facing == FacingDirection::East) return camera::Vec3{static_cast<float>(amount), 0.0f, 0.0f};
    return camera::Vec3{static_cast<float>(-amount), 0.0f, 0.0f};
}

const char* behaviorLabel(IdleBehaviorId behavior) {
    switch (behavior) {
        case IdleBehaviorId::RandomWalk: return "random_walk";
        case IdleBehaviorId::RandomExplore: return "random_explore";
        case IdleBehaviorId::WatchPlayer: return "watch_player";
        case IdleBehaviorId::ApproachPlayerSide: return "approach_player_side";
        case IdleBehaviorId::CirclePlayer: return "circle_player";
        case IdleBehaviorId::DanceCircle: return "dance_circle";
        case IdleBehaviorId::SpinOrbit: return "spin_orbit";
        case IdleBehaviorId::PokePlayer: return "poke_player";
        case IdleBehaviorId::PokeInPlace: return "poke_in_place";
        case IdleBehaviorId::Sleep: return "sleep";
        case IdleBehaviorId::FaceAway: return "face_away";
        case IdleBehaviorId::DriftAway: return "drift_away";
        case IdleBehaviorId::InspectPoi: return "inspect_poi";
        case IdleBehaviorId::JumpFidget: return "jump_fidget";
        case IdleBehaviorId::HopWander: return "hop_wander";
        case IdleBehaviorId::GuardPost: return "guard_post";
        case IdleBehaviorId::ShyHide: return "shy_hide";
        case IdleBehaviorId::FollowNpc: return "follow_npc";
        case IdleBehaviorId::None: return "none";
    }
    return "none";
}

} // namespace

FollowerController::FollowerController(
    const std::string& project_root,
    const SceneConfig& scene,
    const FollowerSummonConfig& summon_config,
    const FollowerSessionConfig& session_config)
    : project_root_(project_root),
      scene_(scene),
      summon_config_(summon_config),
      session_config_(session_config),
      movement_config_(characters::loadCharacterMovementConfig(project_root)),
      terrain_query_(characters::makeLocalCharacterTerrainQuery(scene)),
      idle_config_(loadNatureIdleBehaviorConfig(project_root)),
      idle_script_catalog_(scripts::loadScriptCatalog(project_root)) {}

bool FollowerController::initializeResources() {
    if (!session_config_.enabled || session_config_.pokemon_species.empty()) {
        state_ = State::Hidden;
        resources_ready_ = false;
        return false;
    }
    if (resources_ready_) {
        return true;
    }
    if (!initialized_rng_) {
        rng_.seed(std::random_device{}());
        initialized_rng_ = true;
    }

    const std::string follower_path = session_config_.pokemon_charbin_path.empty()
        ? resolveFollowerPokemonCharbinPath(project_root_, session_config_.pokemon_species)
        : session_config_.pokemon_charbin_path;
    const auto follower_opt = data::tryLoadCharacterDefinition(
        project_root_,
        follower_path,
        data::CharacterAppearanceSelection{session_config_.pokemon_form_id, session_config_.pokemon_shiny});
    if (!follower_opt) {
        resources_ready_ = false;
        return false;
    }
    follower_def_ = *follower_opt;
    follower_animator_ = std::make_unique<characters::SpriteSheetAnimator>(follower_def_);

    const auto ball_opt = data::tryLoadCharacterDefinition(
        project_root_,
        resolveFollowerPokeballCharbinPath(project_root_, session_config_.pokeball_id));
    if (!ball_opt) {
        resources_ready_ = false;
        follower_animator_.reset();
        return false;
    }
    ball_def_ = *ball_opt;
    ball_frames_ = ball_def_.play.frames.empty() ? std::vector<int>{0} : ball_def_.play.frames;
    follower_step_duration_s_ = std::max(0.01f, static_cast<float>(summon_config_.follow_step_duration_ms) / 1000.0f);
    resources_ready_ = true;
    return true;
}

void FollowerController::setTerrainQuery(std::shared_ptr<characters::CharacterTerrainQuery> terrain_query) {
    if (!terrain_query) return;
    terrain_query_ = std::move(terrain_query);
    motor_.setTerrainQuery(terrain_query_);
    if (state_ == State::Active && !follower_moving_) {
        follower_pos_.y = terrainBinding().simulation_y;
    }
}

terrain::ActorTerrainBinding FollowerController::terrainBinding() const {
    terrain::TileCoord sample{follower_tile_.x, follower_tile_.y};
    if (replay_follow_active_) {
        sample = replay_step_motor_.activeSampleTile(follower_move_t_);
    } else if (follower_moving_) {
        return motor_.terrainBinding();
    }
    terrain::ActorTerrainBinding binding = terrain_query_
        ? terrain_query_->bindActorStanding(follower_tile_.x, follower_tile_.y, follower_pos_.x, follower_pos_.z)
        : terrain::bindActorStanding(scene_, follower_tile_.x, follower_tile_.y, follower_pos_.x, follower_pos_.z);
    binding.height_sample_tx = sample.x;
    binding.height_sample_ty = sample.y;
    return binding;
}

void FollowerController::beginStepToTile(const TilePoint& target, double speed_multiplier, bool hop_movement) {
    const TilePoint from_tile = follower_tile_;
    follower_move_from_ = follower_pos_;
    step_dest_tile_ = target;
    follower_move_t_ = 0.0f;
    current_step_speed_multiplier_ = std::max(0.1, speed_multiplier);
    current_step_hop_height_tiles_ = hop_movement ? 0.25 : 0.0;
    const int dx = target.x - from_tile.x;
    const int dy = target.y - from_tile.y;
    const float tile_size = std::max(1.0f, scene_.grid.tile_size);
    const float step_duration = std::max(0.001f, follower_step_duration_s_ / static_cast<float>(current_step_speed_multiplier_));
    motor_.setTerrainQuery(terrain_query_);
    motor_.setMoveSpeedUnitsPerSecond(tile_size / step_duration);
    motor_.resetToTile(from_tile.x, from_tile.y, follower_pos_);
    const characters::GridActorMotor::StepResult step = motor_.tryStartStep(dx, dy);
    follower_moving_ = step.started;
    follower_move_to_ = motor_.moveTarget();
    if (!follower_moving_) {
        current_step_hop_height_tiles_ = 0.0;
        return;
    }
    if (dx > 0) follower_facing_ = FacingDirection::East;
    else if (dx < 0) follower_facing_ = FacingDirection::West;
    else if (dy > 0) follower_facing_ = FacingDirection::South;
    else if (dy < 0) follower_facing_ = FacingDirection::North;
    follower_animator_->setFacing(follower_facing_);
    follower_animator_->setMoving(true);
}

void FollowerController::updateActiveStep(double dt) {
    render_offset_ = camera::Vec3{};
    if (!follower_moving_) return;
    const bool finished = motor_.update(dt);
    follower_move_t_ = motor_.moveT();
    follower_pos_ = motor_.position();
    if (finished) {
        follower_pos_ = follower_move_to_;
        follower_tile_ = step_dest_tile_;
        follower_moving_ = false;
        current_step_hop_height_tiles_ = 0.0;
        return;
    }
    if (current_step_hop_height_tiles_ > 0.0) {
        const double t = std::clamp(static_cast<double>(follower_move_t_), 0.0, 1.0);
        render_offset_.y = static_cast<float>((4.0 * t * (1.0 - t)) * current_step_hop_height_tiles_ * scene_.grid.tile_size);
    }
}

bool FollowerController::updateReplayFollow(
    const characters::CharacterController::MovementSegment& player_segment) {
    if (interaction_locked_) {
        return false;
    }
    if (!player_segment.active || player_step_trail_.size() < 3) {
        if (replay_follow_active_) {
            follower_pos_ = tileToWorldCenter(replay_to_tile_.x, replay_to_tile_.y);
            follower_tile_ = replay_to_tile_;
            follower_moving_ = false;
            replay_follow_active_ = false;
        }
        return false;
    }

    const TilePoint from = player_step_trail_[player_step_trail_.size() - 3U];
    const TilePoint to = player_step_trail_[player_step_trail_.size() - 2U];
    if (from.x == to.x && from.y == to.y) {
        return false;
    }

    if (!replay_follow_active_ ||
        replay_from_tile_.x != from.x ||
        replay_from_tile_.y != from.y ||
        replay_to_tile_.x != to.x ||
        replay_to_tile_.y != to.y) {
        replay_from_tile_ = from;
        replay_to_tile_ = to;
        if (terrain_query_) {
            replay_step_motor_ = terrain_query_->beginStep(
                from.x,
                from.y,
                to.x,
                to.y,
                to.x - from.x,
                to.y - from.y,
                terrain_query_->tileBaseHeightUnits(from.x, from.y),
                terrain_query_->tileBaseHeightUnits(to.x, to.y));
        } else {
            replay_step_motor_ = terrain::GridStepMotor::beginStep(
                scene_,
                from.x,
                from.y,
                to.x,
                to.y,
                to.x - from.x,
                to.y - from.y,
                tileHeightUnits(from.x, from.y),
                tileHeightUnits(to.x, to.y));
        }
    }

    replay_follow_active_ = true;
    follower_tile_ = from;
    step_dest_tile_ = to;
    follower_move_t_ = std::clamp(player_segment.t, 0.0f, 1.0f);
    follower_moving_ = true;
    current_step_hop_height_tiles_ = 0.0;
    if (to.x > from.x) follower_facing_ = FacingDirection::East;
    else if (to.x < from.x) follower_facing_ = FacingDirection::West;
    else if (to.y > from.y) follower_facing_ = FacingDirection::South;
    else if (to.y < from.y) follower_facing_ = FacingDirection::North;

    const camera::Vec3 from_pos = tileToWorldCenter(from.x, from.y);
    const camera::Vec3 to_pos = tileToWorldCenter(to.x, to.y);
    follower_pos_.x = from_pos.x + ((to_pos.x - from_pos.x) * follower_move_t_);
    follower_pos_.z = from_pos.z + ((to_pos.z - from_pos.z) * follower_move_t_);
    if (replay_step_motor_.interpolate_y) {
        follower_pos_.y = terrain_query_
            ? terrain_query_->actorHeightDuringStep(
                follower_pos_.x,
                follower_pos_.z,
                replay_step_motor_,
                follower_move_t_)
            : terrain::actorHeightDuringStep(
                scene_,
                follower_pos_.x,
                follower_pos_.z,
                replay_step_motor_,
                follower_move_t_);
    } else {
        follower_pos_.y = from_pos.y + ((to_pos.y - from_pos.y) * follower_move_t_);
    }
    return true;
}

void FollowerController::finishNatureIdle(bool natural_end) {
    idle_behavior_active_ = false;
    returning_to_origin_ = false;
    cancel_return_active_ = false;
    sleep_action_active_ = false;
    idle_actions_.clear();
    action_elapsed_seconds_ = 0.0;
    action_jump_landings_emitted_ = 0;
    render_offset_ = camera::Vec3{};
    render_screen_offset_y_px_ = 0;
    active_behavior_label_ = "none";
    pending_landing_dust_spawn_.reset();
    if (natural_end && idle_config_.restore_original_direction_on_natural_end) {
        follower_facing_ = idle_origin_facing_;
    }
    if (natural_end) {
        idle_cooldown_seconds_ = idle_config_.cooldown_between_behaviors_seconds.min +
            (idle_config_.cooldown_between_behaviors_seconds.max - idle_config_.cooldown_between_behaviors_seconds.min) * 0.5;
    }
}

void FollowerController::cancelNatureIdle() {
    idle_behavior_active_ = false;
    returning_to_origin_ = false;
    sleep_action_active_ = false;
    idle_actions_.clear();
    path_.clear();
    action_elapsed_seconds_ = 0.0;
    action_jump_landings_emitted_ = 0;
    render_offset_ = camera::Vec3{};
    render_screen_offset_y_px_ = 0;
    cancel_return_active_ = true;
    cancel_soft_snap_elapsed_seconds_ = 0.0;
    idle_seconds_ = 0.0;
    active_behavior_label_ = "cancel_return";
    pending_landing_dust_spawn_.reset();
}

bool FollowerController::beginNextIdleAction() {
    while (!idle_actions_.empty()) {
        IdleAction& action = idle_actions_.front();
        action_elapsed_seconds_ = 0.0;
        action_jump_landings_emitted_ = 0;
        if (action.type == IdleActionType::Face) {
            follower_facing_ = action.facing;
            idle_actions_.pop_front();
            continue;
        }
        if (action.type == IdleActionType::MovePath && action.path.empty()) {
            idle_actions_.pop_front();
            continue;
        }
        sleep_action_active_ = action.type == IdleActionType::Sleep;
        return true;
    }
    sleep_action_active_ = false;
    return false;
}

void FollowerController::updateNormalFollow() {
    if (follower_moving_) return;
    if (path_.empty()) {
        const bool player_has_moved =
            last_player_tile_.x != player_tile_.x || last_player_tile_.y != player_tile_.y;
        const bool follower_not_at_target =
            follower_tile_.x != last_player_tile_.x || follower_tile_.y != last_player_tile_.y;
        if (player_has_moved && follower_not_at_target) {
            const GridPoint occupied{player_tile_.x, player_tile_.y};
            const auto catchup_path = buildFollowerPath(
                scene_,
                GridPoint{follower_tile_.x, follower_tile_.y},
                GridPoint{last_player_tile_.x, last_player_tile_.y},
                &occupied);
            for (const GridPoint& step : catchup_path) {
                path_.push_back(TilePoint{step.x, step.y});
            }
        }
        if (path_.empty()) return;
    }
    const double owner_speed_multiplier = player_running_
        ? static_cast<double>(movement_config_.runSpeed() / std::max(1.0f, movement_config_.walkSpeed()))
        : 1.0;
    const double speed = cancel_return_active_
        ? idle_config_.cancel_return_speed_multiplier
        : owner_speed_multiplier;
    const TilePoint next = path_.front();
    path_.pop_front();
    beginStepToTile(next, speed, false);
}

void FollowerController::updateNatureIdle(double dt) {
    if (!idle_behavior_active_) return;
    if (follower_moving_) return;
    if (idle_actions_.empty() && !returning_to_origin_) {
        const GridPoint occupied{player_tile_.x, player_tile_.y};
        const std::optional<GridPoint> exit_target = selectIdleExitTarget(
            scene_,
            GridPoint{player_tile_.x, player_tile_.y},
            GridPoint{follower_tile_.x, follower_tile_.y},
            GridPoint{idle_origin_tile_.x, idle_origin_tile_.y},
            &occupied);
        if (idle_config_.restore_original_position_on_natural_end &&
            exit_target &&
            (follower_tile_.x != exit_target->x || follower_tile_.y != exit_target->y)) {
            auto return_path = buildFollowerPath(
                scene_,
                GridPoint{follower_tile_.x, follower_tile_.y},
                *exit_target,
                &occupied);
            if (!return_path.empty()) {
                IdleAction return_action;
                return_action.type = IdleActionType::MovePath;
                return_action.path.assign(return_path.begin(), return_path.end());
                return_action.speed_multiplier = idle_config_.return_to_origin_speed_multiplier;
                idle_actions_.push_back(std::move(return_action));
                if (idle_config_.restore_original_direction_on_natural_end) {
                    IdleAction face_restore;
                    face_restore.type = IdleActionType::Face;
                    face_restore.facing = idle_origin_facing_;
                    idle_actions_.push_back(face_restore);
                }
                returning_to_origin_ = true;
            } else {
                follower_pos_ = tileToWorldCenter(exit_target->x, exit_target->y);
                follower_tile_ = TilePoint{exit_target->x, exit_target->y};
            }
        } else {
            finishNatureIdle(true);
            return;
        }
    }
    if (!beginNextIdleAction()) {
        finishNatureIdle(true);
        return;
    }

    IdleAction& action = idle_actions_.front();
    if (action.type == IdleActionType::MovePath) {
        beginStepToTile(TilePoint{action.path.front().x, action.path.front().y}, action.speed_multiplier, action.hop_movement);
        action.path.erase(action.path.begin());
        return;
    }

    action_elapsed_seconds_ += dt;
    if (action.type == IdleActionType::Wait) {
        if (action_elapsed_seconds_ >= action.duration_seconds) {
            idle_actions_.pop_front();
        }
        return;
    }
    if (action.type == IdleActionType::Poke) {
        const double cycle = action.phase_a_seconds + action.phase_b_seconds;
        const double total = cycle * static_cast<double>(std::max(1, action.repeat_count));
        const double phase = std::fmod(action_elapsed_seconds_, cycle);
        const double forward = (phase <= action.phase_a_seconds)
            ? (phase / std::max(0.001, action.phase_a_seconds))
            : (1.0 - ((phase - action.phase_a_seconds) / std::max(0.001, action.phase_b_seconds)));
        render_offset_ = facingOffset(follower_facing_, forward * action.poke_distance_tiles * scene_.grid.tile_size);
        if (action_elapsed_seconds_ >= total) {
            render_offset_ = camera::Vec3{};
            idle_actions_.pop_front();
        }
        return;
    }
    if (action.type == IdleActionType::Jump) {
        const double cycle = std::max(0.001, action.duration_seconds);
        const double total = cycle * static_cast<double>(std::max(1, action.repeat_count));
        const double phase = std::fmod(action_elapsed_seconds_, cycle) / cycle;
        render_screen_offset_y_px_ = -static_cast<int>(std::lround((4.0 * phase * (1.0 - phase)) * static_cast<double>(action.jump_height_pixels)));
        const int completed_landings = std::min(
            std::max(0, action.repeat_count),
            static_cast<int>(std::floor(action_elapsed_seconds_ / cycle)));
        while (action_jump_landings_emitted_ < completed_landings) {
            pending_landing_dust_spawn_ = effects::LandingDustSpawnRequest{
                reinterpret_cast<std::uintptr_t>(this),
                follower_pos_,
                cycle};
            ++action_jump_landings_emitted_;
        }
        if (action_elapsed_seconds_ >= total) {
            render_screen_offset_y_px_ = 0;
            idle_actions_.pop_front();
        }
        return;
    }
    if (action.type == IdleActionType::Sleep) {
        if (action_elapsed_seconds_ >= action.duration_seconds) {
            sleep_action_active_ = false;
            idle_actions_.pop_front();
        }
    }
}

void FollowerController::updateManualDebugAction(double dt) {
    if (manual_debug_action_ == ManualDebugActionType::None) return;

    render_offset_ = camera::Vec3{};
    render_screen_offset_y_px_ = 0;
    manual_debug_elapsed_seconds_ += dt;

    if (manual_debug_action_ == ManualDebugActionType::Jump) {
        const double duration = std::max(0.01, idle_config_.jump.duration_seconds);
        const double phase = std::clamp(manual_debug_elapsed_seconds_ / duration, 0.0, 1.0);
        const int jump_height_pixels = manual_debug_jump_height_pixels_ > 0
            ? manual_debug_jump_height_pixels_
            : idle_config_.jump.height_pixels;
        render_screen_offset_y_px_ =
            -static_cast<int>(std::lround((4.0 * phase * (1.0 - phase)) * static_cast<double>(jump_height_pixels)));
        if (manual_debug_elapsed_seconds_ >= duration) {
            pending_landing_dust_spawn_ = effects::LandingDustSpawnRequest{
                reinterpret_cast<std::uintptr_t>(this),
                follower_pos_,
                duration};
            manual_debug_action_ = ManualDebugActionType::None;
            manual_debug_elapsed_seconds_ = 0.0;
            manual_debug_jump_height_pixels_ = 0;
            render_screen_offset_y_px_ = 0;
            active_behavior_label_ = "none";
        }
        return;
    }

    const double forward = std::max(0.01, idle_config_.poke.forward_seconds);
    const double back = std::max(0.01, idle_config_.poke.return_seconds);
    const double total = forward + back;
    const double phase = std::clamp(manual_debug_elapsed_seconds_, 0.0, total);
    double amount = 0.0;
    if (phase <= forward) {
        amount = (phase / forward) * idle_config_.poke.distance_tiles * scene_.grid.tile_size;
    } else {
        amount = (1.0 - ((phase - forward) / back)) * idle_config_.poke.distance_tiles * scene_.grid.tile_size;
    }
    render_offset_ = facingOffset(follower_facing_, amount);
    if (manual_debug_elapsed_seconds_ >= total) {
        manual_debug_action_ = ManualDebugActionType::None;
        manual_debug_elapsed_seconds_ = 0.0;
        render_offset_ = camera::Vec3{};
        active_behavior_label_ = "none";
    }
}

void FollowerController::update(
    double dt,
    const camera::Vec3& player_world_pos,
    FacingDirection player_facing,
    const characters::CharacterController::MovementSegment& player_segment,
    bool player_idle,
    bool player_activity,
    bool player_running) {
    if (!resources_ready_ || !session_config_.enabled) return;
    script_time_seconds_ += dt;

    player_facing_ = player_facing;
    player_idle_ = player_idle;
    player_running_ = player_running;
    const bool replay_mode = session_config_.movement_mode == "replay";
    const float ts = std::max(1.0f, scene_.grid.tile_size);
    const TilePoint player_tile = replay_mode && player_segment.active
        ? TilePoint{player_segment.to_x, player_segment.to_y}
        : TilePoint{
            static_cast<int>(std::floor(player_world_pos.x / ts)),
            static_cast<int>(std::floor(player_world_pos.z / ts))};
    player_tile_ = player_tile;

    if (!have_last_player_tile_) {
        last_player_tile_ = player_tile;
        player_tile_ = player_tile;
        follower_tile_ = player_tile;
        player_step_trail_.clear();
        player_step_trail_.push_back(player_tile);
        have_last_player_tile_ = true;
    }
    if (replay_mode &&
        player_segment.active &&
        (!have_last_player_segment_ ||
         last_player_segment_from_.x != player_segment.from_x ||
         last_player_segment_from_.y != player_segment.from_y ||
         last_player_segment_to_.x != player_segment.to_x ||
         last_player_segment_to_.y != player_segment.to_y)) {
        const TilePoint from{player_segment.from_x, player_segment.from_y};
        const TilePoint to{player_segment.to_x, player_segment.to_y};
        if (player_step_trail_.empty() ||
            player_step_trail_.back().x != from.x ||
            player_step_trail_.back().y != from.y) {
            player_step_trail_.push_back(from);
        }
        if (player_step_trail_.back().x != to.x || player_step_trail_.back().y != to.y) {
            player_step_trail_.push_back(to);
        }
        while (player_step_trail_.size() > 32) {
            player_step_trail_.pop_front();
        }
        last_player_segment_from_ = from;
        last_player_segment_to_ = to;
        have_last_player_segment_ = true;
        path_.clear();
        if (state_ == State::Hidden) {
            state_ = State::BallRelease;
            state_elapsed_seconds_ = 0.0;
            ball_pos_ = tileToWorldCenter(from.x, from.y);
            follower_pos_ = ball_pos_;
            follower_tile_ = from;
        }
        last_player_tile_ = player_tile;
    } else if (player_tile.x != last_player_tile_.x || player_tile.y != last_player_tile_.y) {
        path_.push_back(last_player_tile_);
        if (path_.size() > 24) path_.pop_front();
        if (player_step_trail_.empty() ||
            player_step_trail_.back().x != player_tile.x ||
            player_step_trail_.back().y != player_tile.y) {
            player_step_trail_.push_back(player_tile);
            while (player_step_trail_.size() > 32) {
                player_step_trail_.pop_front();
            }
        }
        if (state_ == State::Hidden) {
            state_ = State::BallRelease;
            state_elapsed_seconds_ = 0.0;
            ball_pos_ = tileToWorldCenter(last_player_tile_.x, last_player_tile_.y);
            follower_pos_ = ball_pos_;
            follower_tile_ = last_player_tile_;
        }
        last_player_tile_ = player_tile;
    }

    state_elapsed_seconds_ += dt;
    if (state_ == State::BallRelease) {
        if (state_elapsed_seconds_ >= ballReleaseTotalSeconds(summon_config_)) {
            state_ = State::EntryFlash;
            state_elapsed_seconds_ = 0.0;
        }
        return;
    }
    if (state_ == State::EntryFlash) {
        if (state_elapsed_seconds_ * 1000.0 >= static_cast<double>(summon_config_.entry_animation.duration_ms)) {
            state_ = State::Active;
            state_elapsed_seconds_ = 0.0;
        }
        return;
    }

    const bool replay_following =
        replay_mode &&
        !idle_behavior_active_ &&
        !cancel_return_active_ &&
        manual_debug_action_ == ManualDebugActionType::None &&
        updateReplayFollow(player_segment);
    if (!replay_following) {
        updateActiveStep(dt);
    }
    if (!follower_moving_ && state_ == State::Active) {
        follower_pos_.y = terrainBinding().simulation_y;
    }
    if (follower_animator_) {
        const double playback_speed = player_running_ && (follower_moving_ || !path_.empty())
            ? static_cast<double>(movement_config_.runSpeed() / std::max(1.0f, movement_config_.walkSpeed()))
            : 1.0;
        bool swimming = false;
        if (scene_.water_terrain.pokemon_swim_animation_enabled && terrain_query_) {
            const float tile_size = terrain_query_->tileSize();
            const int water_tx = static_cast<int>(std::floor(follower_pos_.x / tile_size));
            const int water_ty = static_cast<int>(std::floor(follower_pos_.z / tile_size));
            swimming = terrain_query_->tileIsActualWater(water_tx, water_ty);
        }
        follower_animator_->setFacing(follower_facing_);
        follower_animator_->setPlaybackSpeedMultiplier(playback_speed);
        follower_animator_->setSwimming(swimming);
        follower_animator_->setRunning(player_running_ && (follower_moving_ || !path_.empty()));
        follower_animator_->setMoving(interaction_locked_ ? false : follower_moving_);
        if ((!interaction_locked_ || follower_animator_->activitySessionActive()) &&
            manual_debug_action_ == ManualDebugActionType::None) {
            follower_animator_->update(dt);
        }
    }

    if (interaction_locked_) {
        path_.clear();
        idle_actions_.clear();
        idle_behavior_active_ = false;
        returning_to_origin_ = false;
        cancel_return_active_ = false;
        sleep_action_active_ = false;
        idle_seconds_ = 0.0;
        if (follower_moving_) {
            follower_moving_ = false;
            replay_follow_active_ = false;
            follower_pos_ = tileToWorldCenter(follower_tile_.x, follower_tile_.y);
        }
        if (interaction_action_active_ && manual_debug_action_ != ManualDebugActionType::None) {
            updateManualDebugAction(dt);
            if (manual_debug_action_ == ManualDebugActionType::None) {
                interaction_action_active_ = false;
            }
        } else {
            manual_debug_action_ = ManualDebugActionType::None;
            interaction_action_active_ = false;
            render_offset_ = camera::Vec3{};
            render_screen_offset_y_px_ = 0;
            active_behavior_label_ = "none";
            pending_landing_dust_spawn_.reset();
        }
        return;
    }

    if (player_activity && idle_behavior_active_) {
        cancelNatureIdle();
    }

    if (cancel_return_active_) {
        cancel_soft_snap_elapsed_seconds_ += dt;
        const GridPoint occupied{player_tile_.x, player_tile_.y};
        const std::optional<GridPoint> exit_target = selectIdleExitTarget(
            scene_,
            GridPoint{player_tile_.x, player_tile_.y},
            GridPoint{follower_tile_.x, follower_tile_.y},
            GridPoint{idle_origin_tile_.x, idle_origin_tile_.y},
            &occupied);
        if (!follower_moving_ && path_.empty() &&
            exit_target &&
            (follower_tile_.x != exit_target->x || follower_tile_.y != exit_target->y)) {
            const auto return_path = buildFollowerPath(
                scene_,
                GridPoint{follower_tile_.x, follower_tile_.y},
                *exit_target,
                &occupied);
            for (const GridPoint& step : return_path) {
                path_.push_back(TilePoint{step.x, step.y});
            }
        }

        if (!follower_moving_ && !path_.empty()) {
            const TilePoint next = path_.front();
            path_.pop_front();
            beginStepToTile(next, idle_config_.cancel_return_speed_multiplier, false);
        }

        if (exit_target &&
            !follower_moving_ &&
            follower_tile_.x == exit_target->x &&
            follower_tile_.y == exit_target->y) {
            if (idle_config_.restore_original_direction_on_natural_end) {
                follower_facing_ = idle_origin_facing_;
            }
            cancel_return_active_ = false;
            active_behavior_label_ = "none";
        } else if (!follower_moving_ && path_.empty() &&
                   idle_config_.allow_soft_snap_on_cancel_return &&
                   cancel_soft_snap_elapsed_seconds_ >= idle_config_.soft_snap_delay_seconds) {
            const GridPoint fallback_target = exit_target.value_or(GridPoint{idle_origin_tile_.x, idle_origin_tile_.y});
            follower_tile_ = TilePoint{fallback_target.x, fallback_target.y};
            follower_pos_ = tileToWorldCenter(follower_tile_.x, follower_tile_.y);
            if (idle_config_.restore_original_direction_on_natural_end) {
                follower_facing_ = idle_origin_facing_;
            }
            cancel_return_active_ = false;
            active_behavior_label_ = "none";
        }
        return;
    }

    if (idle_behavior_active_) {
        updateNatureIdle(dt);
        return;
    }

    if (manual_debug_action_ != ManualDebugActionType::None) {
        updateManualDebugAction(dt);
        return;
    }

    updateNormalFollow();
    if (!player_idle_ || follower_moving_ || !path_.empty() || !idle_config_.enabled) {
        idle_seconds_ = 0.0;
        if (idle_cooldown_seconds_ > 0.0) idle_cooldown_seconds_ = std::max(0.0, idle_cooldown_seconds_ - dt);
        return;
    }
    if (idle_cooldown_seconds_ > 0.0) {
        idle_cooldown_seconds_ = std::max(0.0, idle_cooldown_seconds_ - dt);
        return;
    }

    std::string canonical_nature;
    if (!normalizeNatureName(session_config_.nature, canonical_nature)) {
        idle_seconds_ = 0.0;
        return;
    }

    idle_seconds_ += dt;
    if (idle_seconds_ < idle_config_.start_after_idle_seconds) return;
    idle_seconds_ = 0.0;
    scripts::ScriptContext script_context{};
    script_context.tags = {"POKEMON", "FOLLOWER", "NATURE_" + scripts::normalizeScriptTag(canonical_nature)};
    script_context.nearest_tag_distance_tiles["PLAYER"] =
        std::abs(player_tile.x - follower_tile_.x) + std::abs(player_tile.y - follower_tile_.y);
    const scripts::OverworldScript* selected_script = scripts::selectScript(
        idle_script_catalog_, scripts::ScriptKind::Idle, script_context,
        idle_script_cooldowns_, script_time_seconds_, rng_);
    if (!selected_script || (!session_config_.idle_script_id.empty() && selected_script->id != session_config_.idle_script_id)) {
        return;
    }
    idle_origin_tile_ = follower_tile_;
    idle_origin_facing_ = follower_facing_;
    IdlePlan plan;
    IdleBehaviorId forced_behavior = IdleBehaviorId::None;
    const bool has_forced_behavior =
        !session_config_.forced_behavior.empty() &&
        idleBehaviorIdFromString(session_config_.forced_behavior, forced_behavior) &&
        forced_behavior != IdleBehaviorId::None;
    if (has_forced_behavior) {
        NatureIdleBehaviorConfig forced_config = idle_config_;
        const GridPoint occupied{player_tile.x, player_tile.y};
        for (auto& [group_name, weights] : forced_config.group_weights) {
            (void)group_name;
            for (auto& [behavior_name, weight] : weights) {
                IdleBehaviorId behavior_id = IdleBehaviorId::None;
                if (idleBehaviorIdFromString(behavior_name, behavior_id)) {
                    weight = (behavior_id == forced_behavior) ? forced_config.behavior_weight_clamp.max : 0;
                }
            }
        }
        plan = planNatureIdleBehavior(
            scene_,
            forced_config,
            canonical_nature,
            GridPoint{player_tile.x, player_tile.y},
            player_facing_,
            GridPoint{follower_tile_.x, follower_tile_.y},
            follower_facing_,
            &occupied,
            false,
            rng_);
    } else {
        const GridPoint occupied{player_tile.x, player_tile.y};
        plan = planNatureIdleBehavior(
            scene_,
            idle_config_,
            canonical_nature,
            GridPoint{player_tile.x, player_tile.y},
            player_facing_,
            GridPoint{follower_tile_.x, follower_tile_.y},
            follower_facing_,
            &occupied,
            false,
            rng_);
    }
    if (!plan.valid || plan.actions.empty()) return;
    idle_actions_.assign(plan.actions.begin(), plan.actions.end());
    idle_behavior_active_ = true;
    returning_to_origin_ = false;
    active_behavior_label_ = behaviorLabel(plan.behavior);
}

bool FollowerController::visibleForSimulation() const {
    return resources_ready_ && state_ != State::Hidden;
}

std::vector<std::pair<int, int>> FollowerController::reservedTiles() const {
    if (!visibleForSimulation()) {
        return {};
    }

    std::vector<std::pair<int, int>> tiles;
    tiles.push_back({follower_tile_.x, follower_tile_.y});
    if ((follower_moving_ || replay_follow_active_) &&
        (step_dest_tile_.x != follower_tile_.x || step_dest_tile_.y != follower_tile_.y)) {
        tiles.push_back({step_dest_tile_.x, step_dest_tile_.y});
    }
    return tiles;
}

std::optional<std::string> FollowerController::interactionTargetIdAtTile(int tx, int ty) const {
    if (state_ != State::Active || follower_moving_ || replay_follow_active_ || !resources_ready_) {
        return std::nullopt;
    }
    if (follower_tile_.x == tx && follower_tile_.y == ty) {
        return session_config_.pokemon_species.empty()
            ? std::string{"follower_pokemon"}
            : std::string{"follower_pokemon:" + session_config_.pokemon_species};
    }
    return std::nullopt;
}

bool FollowerController::setInteractionLocked(bool locked) {
    if (locked && (state_ != State::Active || follower_moving_ || replay_follow_active_ || !resources_ready_)) {
        interaction_locked_ = false;
        return false;
    }
    interaction_locked_ = locked;
    if (!interaction_locked_) {
        return true;
    }
    path_.clear();
    idle_actions_.clear();
    idle_behavior_active_ = false;
    returning_to_origin_ = false;
    cancel_return_active_ = false;
    sleep_action_active_ = false;
    manual_debug_action_ = ManualDebugActionType::None;
    interaction_action_active_ = false;
    render_offset_ = camera::Vec3{};
    render_screen_offset_y_px_ = 0;
    active_behavior_label_ = "none";
    pending_landing_dust_spawn_.reset();
    idle_seconds_ = 0.0;
    return true;
}

std::optional<FollowerInteractionInfo> FollowerController::interactionInfo() const {
    if (state_ != State::Active || !resources_ready_) {
        return std::nullopt;
    }
    FollowerInteractionInfo info{};
    info.tile_x = follower_tile_.x;
    info.tile_y = follower_tile_.y;
    info.display_name = session_config_.pokemon_species.empty() ? std::string{"Pokemon"} : session_config_.pokemon_species;
    info.species_name = follower_def_.species_name.empty() ? session_config_.pokemon_species : follower_def_.species_name;
    info.pokemon_size = follower_def_.pokemon_size;
    info.pokemon_types = follower_def_.pokemon_types;
    info.species_slug = session_config_.pokemon_species;
    info.form_id = session_config_.pokemon_form_id;
    info.shiny = session_config_.pokemon_shiny;
    return info;
}

bool FollowerController::faceInteractionLockedTowardTile(int tx, int ty) {
    if (!interaction_locked_) {
        return false;
    }
    follower_facing_ = interactions::facingTowardTiles(follower_tile_.x, follower_tile_.y, tx, ty, follower_facing_);
    if (follower_animator_) {
        follower_animator_->setFacing(follower_facing_);
    }
    return true;
}

bool FollowerController::faceInteractionLocked(FacingDirection facing) {
    if (!interaction_locked_) {
        return false;
    }
    follower_facing_ = facing;
    if (follower_animator_) {
        follower_animator_->setFacing(follower_facing_);
    }
    return true;
}

bool FollowerController::startInteractionSession() {
    if (!interaction_locked_ || !follower_animator_) {
        return false;
    }
    const std::optional<std::string> action_id =
        interactions::interactionActivityIdForPokemonSize(follower_def_.pokemon_size);
    return action_id && follower_animator_->startActivitySession(*action_id);
}

void FollowerController::requestInteractionSessionExit() {
    if (follower_animator_) {
        follower_animator_->requestActivityExit();
    }
}

bool FollowerController::interactionSessionReady() const {
    return !follower_animator_ ||
        !follower_animator_->activitySessionActive() ||
        follower_animator_->activityStayActive();
}

bool FollowerController::interactionSessionFinished() const {
    return !follower_animator_ || follower_animator_->activityFinished();
}

void FollowerController::collectBillboardDraws(
    const camera::Gen4FollowCamera& camera,
    int viewport_w,
    int viewport_h,
    std::vector<rendering::CharacterBillboardDraw>& out) const {
    if (!visibleForSimulation()) {
        return;
    }

    if (state_ == State::BallRelease) {
        const double animation_seconds = std::max(0.001, ballReleasePhaseSeconds(summon_config_));
        const double held_elapsed_seconds = std::min(state_elapsed_seconds_, animation_seconds);
        const float t = std::clamp(
            static_cast<float>(held_elapsed_seconds / animation_seconds),
            0.0f,
            1.0f);
        int frame = ball_frames_.front();
        if (summon_config_.ball_animation.full_animation) {
            const int idx = std::clamp(
                static_cast<int>(std::round(t * static_cast<float>(std::max(0, static_cast<int>(ball_frames_.size()) - 1)))),
                0,
                static_cast<int>(ball_frames_.size()) - 1);
            frame = ball_frames_[static_cast<std::size_t>(idx)];
        } else if (t > 0.5f) {
            frame = ball_frames_.back();
        }
        const float ts = std::max(1.0f, scene_.grid.tile_size);
        const int ball_tx = static_cast<int>(std::floor(ball_pos_.x / ts));
        const int ball_ty = static_cast<int>(std::floor(ball_pos_.z / ts));
        const terrain::ActorTerrainBinding binding =
            terrain::bindActorStanding(scene_, ball_tx, ball_ty, ball_pos_.x, ball_pos_.z);
        camera::Vec3 sim_pos = ball_pos_;
        if (summon_config_.ball_animation.fall_enabled) {
            sim_pos.y += (1.0f - t) * summon_config_.ball_animation.fall_height_world;
        }
        rendering::CharacterBillboardDraw draw{};
        draw.character = &ball_def_;
        draw.source_rect = sourceRectForFrame(ball_def_, frame);
        draw.sprite_scale_multiplier = summon_config_.ball_animation.sprite_scale;
        draw.draw_shadow = false;
        draw.placement = rendering::buildCharacterBillboardPlacement(
            scene_,
            camera,
            binding,
            ball_def_,
            sim_pos,
            draw.source_rect,
            viewport_w,
            viewport_h,
            summon_config_.ball_animation.sprite_scale,
            summon_config_.ball_animation.screen_offset_y_px,
            camera::Vec3{
                summon_config_.ball_animation.world_offset_x,
                summon_config_.ball_animation.world_offset_y,
                summon_config_.ball_animation.world_offset_z});
        out.push_back(draw);
        return;
    }

    if (!follower_animator_) {
        return;
    }

    rendering::CharacterBillboardDraw draw{};
    draw.character = &follower_def_;
    draw.source_rect = follower_animator_->sourceRect();
    draw.activity_id = follower_animator_->textureSheetId();
    draw.draw_shadow = !follower_animator_->swimming();
    draw.use_run_texture = follower_animator_->running();
    if (state_ == State::EntryFlash) {
        const float t = std::clamp(
            static_cast<float>((state_elapsed_seconds_ * 1000.0) /
                std::max(1.0, static_cast<double>(summon_config_.entry_animation.duration_ms))),
            0.0f,
            1.0f);
        draw.sprite_scale_multiplier =
            summon_config_.entry_animation.start_scale + ((1.0f - summon_config_.entry_animation.start_scale) * t);
        draw.tint_r = summon_config_.entry_animation.tint_r;
        draw.tint_g = summon_config_.entry_animation.tint_g;
        draw.tint_b = summon_config_.entry_animation.tint_b;
        draw.alpha_multiplier = summon_config_.entry_animation.alpha;
        draw.white_overlay_alpha = 1.0f;
    }
    draw.placement = rendering::buildCharacterBillboardPlacement(
        scene_,
        camera,
        terrainBinding(),
        follower_def_,
        follower_pos_,
        draw.source_rect,
        viewport_w,
        viewport_h,
        draw.sprite_scale_multiplier,
        render_screen_offset_y_px_,
        render_offset_);
    out.push_back(draw);
}

std::optional<float> FollowerController::renderDepth(
    const camera::Gen4FollowCamera& camera,
    int viewport_w,
    int viewport_h) const {
    if (!visibleForSimulation()) return std::nullopt;
    camera::Vec3 p = (state_ == State::BallRelease) ? ball_pos_ : follower_pos_;
    float sx = 0.0f, sy = 0.0f, depth = 0.0f;
    if (!camera.worldToScreen(p, viewport_w, viewport_h, sx, sy, depth)) return std::nullopt;
    return depth;
}

bool FollowerController::triggerDebugJump() {
    if (state_ != State::Active || follower_moving_ || idle_behavior_active_ || cancel_return_active_ ||
        manual_debug_action_ != ManualDebugActionType::None) {
        return false;
    }
    manual_debug_action_ = ManualDebugActionType::Jump;
    interaction_action_active_ = false;
    manual_debug_elapsed_seconds_ = 0.0;
    manual_debug_jump_height_pixels_ = 0;
    render_offset_ = camera::Vec3{};
    render_screen_offset_y_px_ = 0;
    pending_landing_dust_spawn_.reset();
    active_behavior_label_ = "debug_jump";
    return true;
}

bool FollowerController::triggerInteractionJump(int height_pixels) {
    if (!triggerDebugJump()) return false;
    interaction_action_active_ = true;
    manual_debug_jump_height_pixels_ = std::max(0, height_pixels);
    return true;
}

bool FollowerController::triggerDebugPoke() {
    if (state_ != State::Active || follower_moving_ || idle_behavior_active_ || cancel_return_active_ ||
        manual_debug_action_ != ManualDebugActionType::None) {
        return false;
    }
    manual_debug_action_ = ManualDebugActionType::Poke;
    manual_debug_elapsed_seconds_ = 0.0;
    render_offset_ = camera::Vec3{};
    render_screen_offset_y_px_ = 0;
    pending_landing_dust_spawn_.reset();
    active_behavior_label_ = "debug_poke";
    return true;
}

std::string FollowerController::debugActivityLabel() const {
    return active_behavior_label_;
}

std::optional<effects::LandingDustSpawnRequest> FollowerController::consumeLandingDustSpawn() {
    std::optional<effects::LandingDustSpawnRequest> out = pending_landing_dust_spawn_;
    pending_landing_dust_spawn_.reset();
    return out;
}

bool FollowerController::effectOnActualWater() const {
    // Particle state follows the committed logical tile, not the interpolated
    // render position. The loaded-world query also understands adjacent maps;
    // querying scene_ directly made every follower appear to leave water when
    // it crossed beyond the primary map's local coordinates.
    return terrain_query_
        ? terrain_query_->tileIsActualWater(follower_tile_.x, follower_tile_.y)
        : terrain::isActualWaterTile(scene_, follower_tile_.x, follower_tile_.y);
}

int FollowerController::tileHeightUnits(int tx, int ty) const {
    if (scene_.terrain.heights.empty()) return 0;
    if (ty < 0 || ty >= static_cast<int>(scene_.terrain.heights.size())) return 0;
    const auto& row = scene_.terrain.heights[static_cast<std::size_t>(ty)];
    if (tx < 0 || tx >= static_cast<int>(row.size())) return 0;
    return static_cast<int>(row[static_cast<std::size_t>(tx)]);
}

camera::Vec3 FollowerController::tileToWorldCenter(int tx, int ty) const {
    const float ts = terrain_query_ ? terrain_query_->tileSize() : std::max(1.0f, scene_.grid.tile_size);
    const float cx = (static_cast<float>(tx) + 0.5f) * ts;
    const float cz = (static_cast<float>(ty) + 0.5f) * ts;
    const float y = terrain_query_
        ? terrain_query_->bindActorStanding(tx, ty, cx, cz).simulation_y
        : terrain::heightAtActorFeet(scene_, cx, cz, tx, ty);
    return camera::Vec3{cx, y, cz};
}

} // namespace pr::gameplay::world3d::followers
