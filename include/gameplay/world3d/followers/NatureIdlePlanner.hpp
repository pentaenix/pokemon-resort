#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/followers/NatureIdleConfig.hpp"

#include <random>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::followers {

enum class IdleBehaviorId {
    None,
    RandomWalk,
    RandomExplore,
    WatchPlayer,
    ApproachPlayerSide,
    CirclePlayer,
    DanceCircle,
    SpinOrbit,
    PokePlayer,
    PokeInPlace,
    Sleep,
    FaceAway,
    DriftAway,
    InspectPoi,
    JumpFidget,
    HopWander,
    GuardPost,
    ShyHide,
    FollowNpc,
};

enum class IdleActionType {
    MovePath,
    Wait,
    Face,
    Poke,
    Jump,
    Sleep
};

struct GridPoint {
    int x = 0;
    int y = 0;
};

struct IdleAction {
    IdleActionType type = IdleActionType::Wait;
    std::vector<GridPoint> path;
    FacingDirection facing = FacingDirection::South;
    double duration_seconds = 0.0;
    double speed_multiplier = 1.0;
    bool hop_movement = false;
    int repeat_count = 0;
    double poke_distance_tiles = 0.0;
    double phase_a_seconds = 0.0;
    double phase_b_seconds = 0.0;
    int jump_height_pixels = 0;
};

struct IdlePlan {
    IdleBehaviorId behavior = IdleBehaviorId::None;
    std::vector<IdleAction> actions;
    bool valid = false;
};

bool normalizeNatureName(const std::string& raw, std::string& out_canonical);
bool idleBehaviorIdFromString(const std::string& raw, IdleBehaviorId& out_id);
IdlePlan planNatureIdleBehavior(
    const SceneConfig& scene,
    const NatureIdleBehaviorConfig& config,
    const std::string& canonical_nature,
    const GridPoint& player_tile,
    FacingDirection player_facing,
    const GridPoint& follower_tile,
    FacingDirection follower_facing,
    const GridPoint* occupied_tile,
    bool sleep_available,
    std::mt19937& rng);
std::vector<GridPoint> buildFollowerPath(
    const SceneConfig& scene,
    const GridPoint& start,
    const GridPoint& goal,
    const GridPoint* occupied_tile = nullptr);

} // namespace pr::gameplay::world3d::followers
