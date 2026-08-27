#include "gameplay/world3d/followers/NatureIdlePlanner.hpp"

#include "gameplay/world3d/interiors/DefaultRoom.hpp"

#include "gameplay/world3d/terrain/TerrainSurface.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <deque>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace pr::gameplay::world3d::followers {

namespace {

using BehaviorMap = std::unordered_map<std::string, IdleBehaviorId>;

const BehaviorMap& behaviorMap() {
    static const BehaviorMap kMap{
        {"random_walk", IdleBehaviorId::RandomWalk},
        {"random_explore", IdleBehaviorId::RandomExplore},
        {"watch_player", IdleBehaviorId::WatchPlayer},
        {"approach_player_side", IdleBehaviorId::ApproachPlayerSide},
        {"circle_player", IdleBehaviorId::CirclePlayer},
        {"dance_circle", IdleBehaviorId::DanceCircle},
        {"spin_orbit", IdleBehaviorId::SpinOrbit},
        {"poke_player", IdleBehaviorId::PokePlayer},
        {"poke_in_place", IdleBehaviorId::PokeInPlace},
        {"sleep", IdleBehaviorId::Sleep},
        {"face_away", IdleBehaviorId::FaceAway},
        {"drift_away", IdleBehaviorId::DriftAway},
        {"inspect_poi", IdleBehaviorId::InspectPoi},
        {"jump_fidget", IdleBehaviorId::JumpFidget},
        {"hop_wander", IdleBehaviorId::HopWander},
        {"guard_post", IdleBehaviorId::GuardPost},
        {"shy_hide", IdleBehaviorId::ShyHide},
        {"follow_npc", IdleBehaviorId::FollowNpc},
        {"do_nothing", IdleBehaviorId::None},
    };
    return kMap;
}

const std::unordered_map<std::string, std::string>& natureGroups() {
    static const std::unordered_map<std::string, std::string> kGroups{
        {"Lonely", "assertive"}, {"Brave", "assertive"}, {"Adamant", "assertive"}, {"Naughty", "assertive"},
        {"Bold", "steady"}, {"Relaxed", "steady"}, {"Impish", "steady"}, {"Lax", "steady"},
        {"Modest", "curious"}, {"Mild", "curious"}, {"Quiet", "curious"}, {"Rash", "curious"},
        {"Calm", "composed"}, {"Gentle", "composed"}, {"Sassy", "composed"}, {"Careful", "composed"},
        {"Timid", "energetic"}, {"Hasty", "energetic"}, {"Jolly", "energetic"}, {"Naive", "energetic"},
        {"Hardy", "neutral"}, {"Docile", "neutral"}, {"Serious", "neutral"}, {"Bashful", "neutral"}, {"Quirky", "neutral"},
    };
    return kGroups;
}

std::string lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

int clampWeight(const NatureIdleBehaviorConfig& config, int value) {
    return std::clamp(value, config.behavior_weight_clamp.min, config.behavior_weight_clamp.max);
}

double randomRange(std::mt19937& rng, double min_value, double max_value) {
    std::uniform_real_distribution<double> dist(min_value, max_value);
    return dist(rng);
}

int randomInt(std::mt19937& rng, int min_value, int max_value) {
    std::uniform_int_distribution<int> dist(min_value, max_value);
    return dist(rng);
}

double estimateMoveSeconds(std::size_t path_steps, double speed_multiplier) {
    constexpr double kBaseFollowerStepSeconds = 0.25;
    return static_cast<double>(path_steps) * (kBaseFollowerStepSeconds / std::max(0.1, speed_multiplier));
}

FacingDirection faceToward(const GridPoint& from, const GridPoint& to) {
    const int dx = to.x - from.x;
    const int dy = to.y - from.y;
    if (std::abs(dx) >= std::abs(dy)) return (dx >= 0) ? FacingDirection::East : FacingDirection::West;
    return (dy >= 0) ? FacingDirection::South : FacingDirection::North;
}

FacingDirection faceAway(const GridPoint& from, const GridPoint& target) {
    const FacingDirection toward = faceToward(from, target);
    if (toward == FacingDirection::East) return FacingDirection::West;
    if (toward == FacingDirection::West) return FacingDirection::East;
    if (toward == FacingDirection::South) return FacingDirection::North;
    return FacingDirection::South;
}

std::vector<FacingDirection> fullSpinSequence(FacingDirection start) {
    switch (start) {
        case FacingDirection::South: return {FacingDirection::West, FacingDirection::North, FacingDirection::East, FacingDirection::South};
        case FacingDirection::West: return {FacingDirection::North, FacingDirection::East, FacingDirection::South, FacingDirection::West};
        case FacingDirection::East: return {FacingDirection::South, FacingDirection::West, FacingDirection::North, FacingDirection::East};
        case FacingDirection::North: return {FacingDirection::East, FacingDirection::South, FacingDirection::West, FacingDirection::North};
    }
    return {FacingDirection::South};
}

struct Navigator {
    const SceneConfig& scene;
    int width() const { return !scene.terrain.heights.empty() ? static_cast<int>(scene.terrain.heights.front().size()) : std::max(1, scene.grid.width); }
    int height() const { return !scene.terrain.heights.empty() ? static_cast<int>(scene.terrain.heights.size()) : std::max(1, scene.grid.height); }
    bool inBounds(int x, int y) const { return x >= 0 && x < width() && y >= 0 && y < height(); }
    int heightUnits(int x, int y) const {
        if (!inBounds(x, y) || scene.terrain.heights.empty()) return 0;
        return static_cast<int>(scene.terrain.heights[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)]);
    }
    int special(int x, int y) const {
        if (!inBounds(x, y) || scene.terrain.specials.empty()) return 0;
        return static_cast<int>(scene.terrain.specials[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)]);
    }
    bool blocked(int x, int y) const {
        if (!inBounds(x, y)) return false;
        if (interiors::boundaryCellBlocked(scene, x, y)) return true;
        if (scene.terrain.collision.empty()) return false;
        return scene.terrain.collision[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] != 0;
    }
    bool occupiedByNpc(int x, int y) const {
        for (const auto& npc : scene.characters) {
            if (npc.tile_x == x && npc.tile_y == y) return true;
        }
        return false;
    }
    int rampDirection(int x, int y) const {
        const int sp = special(x, y);
        return (sp >= 2 && sp <= 5) ? sp : 0;
    }
    bool canStep(const GridPoint& from, const GridPoint& to) const {
        if (!inBounds(to.x, to.y) || blocked(to.x, to.y)) return false;
        const int dx = to.x - from.x;
        const int dy = to.y - from.y;
        const int dh = heightUnits(to.x, to.y) - heightUnits(from.x, from.y);
        if (std::abs(dx) + std::abs(dy) != 1) return false;
        if (terrain::canTraverseTerrainEdge(scene, from.x, from.y, to.x, to.y, dx, dy)) return true;
        if (dh == 0) return true;
        if (std::abs(dh) > 1) return false;
        auto ramp_allows = [&](const GridPoint& tile) {
            const int dir = rampDirection(tile.x, tile.y);
            if (dir == 0) return false;
            if (dh > 0) {
                return (dir == 2 && dy == -1) || (dir == 3 && dx == 1) || (dir == 4 && dy == 1) || (dir == 5 && dx == -1);
            }
            return (dir == 2 && dy == 1) || (dir == 3 && dx == -1) || (dir == 4 && dy == -1) || (dir == 5 && dx == 1);
        };
        return ramp_allows(from) || ramp_allows(to);
    }
    bool validIdleTile(const GridPoint& tile, const GridPoint& player_tile) const {
        return inBounds(tile.x, tile.y) && !blocked(tile.x, tile.y) && !occupiedByNpc(tile.x, tile.y) &&
               !(tile.x == player_tile.x && tile.y == player_tile.y);
    }
    std::vector<GridPoint> pathfind(const GridPoint& start, const GridPoint& goal, const GridPoint* occupied_tile = nullptr) const {
        if (start.x == goal.x && start.y == goal.y) return {};
        if (!inBounds(goal.x, goal.y)) return {};
        const int w = width();
        const int h = height();
        std::vector<int> prev(static_cast<std::size_t>(w * h), -1);
        std::deque<GridPoint> q;
        q.push_back(start);
        prev[static_cast<std::size_t>(start.y * w + start.x)] = start.y * w + start.x;
        const std::array<GridPoint, 4> dirs{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
        while (!q.empty()) {
            const GridPoint cur = q.front();
            q.pop_front();
            for (const GridPoint dir : dirs) {
                GridPoint next{cur.x + dir.x, cur.y + dir.y};
                if (!inBounds(next.x, next.y) || blocked(next.x, next.y)) continue;
                if (occupied_tile &&
                    next.x == occupied_tile->x &&
                    next.y == occupied_tile->y &&
                    !(next.x == goal.x && next.y == goal.y)) {
                    continue;
                }
                if (!canStep(cur, next)) continue;
                const int idx = next.y * w + next.x;
                if (prev[static_cast<std::size_t>(idx)] != -1) continue;
                prev[static_cast<std::size_t>(idx)] = cur.y * w + cur.x;
                if (next.x == goal.x && next.y == goal.y) {
                    std::vector<GridPoint> path;
                    int walk = idx;
                    while (walk != (start.y * w + start.x)) {
                        path.push_back(GridPoint{walk % w, walk / w});
                        walk = prev[static_cast<std::size_t>(walk)];
                    }
                    std::reverse(path.begin(), path.end());
                    return path;
                }
                q.push_back(next);
            }
        }
        return {};
    }
};

IdleAction moveAction(std::vector<GridPoint> path, double speed_multiplier, bool hop = false) {
    IdleAction action;
    action.type = IdleActionType::MovePath;
    action.path = std::move(path);
    action.speed_multiplier = speed_multiplier;
    action.hop_movement = hop;
    return action;
}

IdleAction waitAction(double seconds) {
    IdleAction action;
    action.type = IdleActionType::Wait;
    action.duration_seconds = seconds;
    return action;
}

IdleAction faceAction(FacingDirection facing) {
    IdleAction action;
    action.type = IdleActionType::Face;
    action.facing = facing;
    return action;
}

IdleAction pokeAction(int count, double distance_tiles, double forward_seconds, double return_seconds) {
    IdleAction action;
    action.type = IdleActionType::Poke;
    action.repeat_count = count;
    action.poke_distance_tiles = distance_tiles;
    action.phase_a_seconds = forward_seconds;
    action.phase_b_seconds = return_seconds;
    return action;
}

IdleAction jumpAction(int count, int height_pixels, double duration_seconds) {
    IdleAction action;
    action.type = IdleActionType::Jump;
    action.repeat_count = count;
    action.jump_height_pixels = height_pixels;
    action.duration_seconds = duration_seconds;
    return action;
}

GridPoint adjacentForPlayer(const GridPoint& player_tile, FacingDirection facing, int slot) {
    static const std::array<GridPoint, 4> offsets{{{0, 1}, {-1, 0}, {1, 0}, {0, -1}}};
    GridPoint tile{player_tile.x + offsets[slot].x, player_tile.y + offsets[slot].y};
    if (facing == FacingDirection::North && slot == 0) tile = {player_tile.x, player_tile.y - 1};
    if (facing == FacingDirection::South && slot == 0) tile = {player_tile.x, player_tile.y + 1};
    if (facing == FacingDirection::East && slot == 0) tile = {player_tile.x + 1, player_tile.y};
    if (facing == FacingDirection::West && slot == 0) tile = {player_tile.x - 1, player_tile.y};
    return tile;
}

std::optional<GridPoint> chooseRandomTile(
    const Navigator& nav,
    const GridPoint& center,
    const GridPoint& player_tile,
    int min_radius,
    int max_radius,
    std::mt19937& rng,
    bool prefer_away = false) {
    std::vector<GridPoint> options;
    for (int y = center.y - max_radius; y <= center.y + max_radius; ++y) {
        for (int x = center.x - max_radius; x <= center.x + max_radius; ++x) {
            GridPoint tile{x, y};
            if (!nav.validIdleTile(tile, player_tile)) continue;
            const int md = std::abs(tile.x - center.x) + std::abs(tile.y - center.y);
            if (md < min_radius || md > max_radius) continue;
            options.push_back(tile);
        }
    }
    if (options.empty()) return std::nullopt;
    if (prefer_away) {
        std::sort(options.begin(), options.end(), [&](const GridPoint& a, const GridPoint& b) {
            const int da = std::abs(a.x - player_tile.x) + std::abs(a.y - player_tile.y);
            const int db = std::abs(b.x - player_tile.x) + std::abs(b.y - player_tile.y);
            return da > db;
        });
        options.resize(std::min<std::size_t>(options.size(), 8U));
    }
    return options[static_cast<std::size_t>(randomInt(rng, 0, static_cast<int>(options.size()) - 1))];
}

void appendFaceSequence(std::vector<IdleAction>& actions, const std::vector<FacingDirection>& sequence, double wait_seconds) {
    for (FacingDirection facing : sequence) {
        actions.push_back(faceAction(facing));
        actions.push_back(waitAction(wait_seconds));
    }
}

IdlePlan fallbackPlan(IdleBehaviorId behavior, FacingDirection facing, const NatureIdleBehaviorConfig& config) {
    IdlePlan plan;
    plan.behavior = behavior;
    plan.valid = true;
    if (behavior == IdleBehaviorId::WatchPlayer) {
        plan.actions.push_back(faceAction(facing));
        plan.actions.push_back(waitAction(std::max(0.5, config.cooldown_between_behaviors_seconds.min)));
    } else if (behavior == IdleBehaviorId::FaceAway) {
        plan.actions.push_back(faceAction(facing));
        plan.actions.push_back(waitAction(std::max(0.75, config.cooldown_between_behaviors_seconds.min)));
    } else if (behavior == IdleBehaviorId::RandomWalk) {
        plan.actions.push_back(waitAction(std::max(0.5, config.cooldown_between_behaviors_seconds.min)));
    }
    return plan;
}

} // namespace

bool normalizeNatureName(const std::string& raw, std::string& out_canonical) {
    const std::string folded = lower(raw);
    for (const auto& [nature, group] : natureGroups()) {
        (void)group;
        if (lower(nature) == folded) {
            out_canonical = nature;
            return true;
        }
    }
    out_canonical.clear();
    return false;
}

bool idleBehaviorIdFromString(const std::string& raw, IdleBehaviorId& out_id) {
    const std::string folded = lower(raw);
    for (const auto& [key, value] : behaviorMap()) {
        if (lower(key) == folded) {
            out_id = value;
            return true;
        }
    }
    out_id = IdleBehaviorId::None;
    return false;
}

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
    std::mt19937& rng) {
    IdlePlan plan;
    const auto group_it = natureGroups().find(canonical_nature);
    if (group_it == natureGroups().end()) return plan;

    std::vector<std::pair<IdleBehaviorId, int>> weights;
    for (const auto& [behavior_key, behavior_id] : behaviorMap()) {
        if (behavior_id == IdleBehaviorId::None) continue;
        if (behavior_id == IdleBehaviorId::Sleep && !sleep_available) continue;
        int weight = 0;
        if (const auto group_weights_it = config.group_weights.find(group_it->second); group_weights_it != config.group_weights.end()) {
            if (const auto it = group_weights_it->second.find(behavior_key); it != group_weights_it->second.end()) weight += it->second;
        }
        if (const auto nature_mod_it = config.nature_modifiers.find(canonical_nature); nature_mod_it != config.nature_modifiers.end()) {
            if (const auto it = nature_mod_it->second.find(behavior_key); it != nature_mod_it->second.end()) weight += it->second;
        }
        weight = clampWeight(config, weight);
        if (weight > 0) weights.push_back({behavior_id, weight});
    }
    if (weights.empty()) return plan;

    if (canonical_nature == "Quirky" && randomRange(rng, 0.0, 1.0) < config.quirky_random_behavior_chance) {
        for (auto& entry : weights) entry.second = 1;
    }

    int total = 0;
    for (const auto& entry : weights) total += entry.second;
    int pick = randomInt(rng, 1, std::max(1, total));
    IdleBehaviorId selected = IdleBehaviorId::None;
    for (const auto& entry : weights) {
        pick -= entry.second;
        if (pick <= 0) {
            selected = entry.first;
            break;
        }
    }
    if (selected == IdleBehaviorId::None) return plan;

    const Navigator nav{scene};
    const double target_duration = randomRange(rng, config.behavior_duration_seconds.min, config.behavior_duration_seconds.max);
    const double move_speed = config.movement_speed_multiplier;
    plan.behavior = selected;
    plan.valid = true;

    auto choose_adjacent = [&](bool allow_front) -> std::optional<GridPoint> {
        std::vector<GridPoint> options;
        for (int i = 0; i < 4; ++i) {
            if (!allow_front && i == 0) continue;
            const GridPoint tile = adjacentForPlayer(player_tile, player_facing, i);
            if (nav.validIdleTile(tile, player_tile)) options.push_back(tile);
        }
        if (options.empty()) return std::nullopt;
        return options[static_cast<std::size_t>(randomInt(rng, 0, static_cast<int>(options.size()) - 1))];
    };

    if (selected == IdleBehaviorId::RandomWalk || selected == IdleBehaviorId::RandomExplore || selected == IdleBehaviorId::InspectPoi || selected == IdleBehaviorId::FollowNpc) {
        GridPoint cursor = follower_tile;
        double spent = 0.0;
        while (spent < target_duration) {
            const auto target = chooseRandomTile(nav, player_tile, player_tile, 1, config.max_player_radius, rng);
            if (!target) break;
            std::vector<GridPoint> path = nav.pathfind(cursor, *target, occupied_tile);
            if (path.empty()) break;
            plan.actions.push_back(moveAction(path, move_speed, selected == IdleBehaviorId::HopWander));
            cursor = *target;
            const double wait = (selected == IdleBehaviorId::RandomExplore || selected == IdleBehaviorId::InspectPoi)
                ? randomRange(rng, 0.25, 0.45)
                : randomRange(rng, 0.4, 1.2);
            if (selected == IdleBehaviorId::RandomExplore || selected == IdleBehaviorId::InspectPoi) {
                for (int turns = randomInt(rng, 2, 4); turns > 0; --turns) {
                    plan.actions.push_back(faceAction(static_cast<FacingDirection>(randomInt(rng, 0, 3))));
                    plan.actions.push_back(waitAction(wait));
                }
                spent += 1.2;
            } else {
                plan.actions.push_back(waitAction(wait));
                spent += 1.0;
            }
        }
    } else if (selected == IdleBehaviorId::WatchPlayer) {
        for (double spent = 0.0; spent < target_duration; spent += 1.2) {
            plan.actions.push_back(faceAction(faceToward(follower_tile, player_tile)));
            plan.actions.push_back(waitAction(randomRange(rng, 0.5, 1.1)));
            plan.actions.push_back(faceAction(static_cast<FacingDirection>(randomInt(rng, 0, 3))));
            plan.actions.push_back(waitAction(randomRange(rng, 0.5, 1.1)));
        }
    } else if (selected == IdleBehaviorId::ApproachPlayerSide || selected == IdleBehaviorId::PokePlayer) {
        double spent = 0.0;
        GridPoint cursor = follower_tile;
        const auto target = choose_adjacent(true);
        if (!target) return fallbackPlan(IdleBehaviorId::WatchPlayer, follower_facing, config);
        std::vector<GridPoint> path = nav.pathfind(cursor, *target, occupied_tile);
        if (!path.empty()) {
            plan.actions.push_back(moveAction(path, move_speed));
            spent += estimateMoveSeconds(path.size(), move_speed);
            cursor = *target;
        }
        if (selected == IdleBehaviorId::PokePlayer) {
            while (spent < target_duration) {
                plan.actions.push_back(faceAction(faceToward(cursor, player_tile)));
                const int poke_count = randomInt(rng, 1, 3);
                plan.actions.push_back(pokeAction(
                    poke_count,
                    config.poke.distance_tiles,
                    config.poke.forward_seconds,
                    config.poke.return_seconds));
                spent += static_cast<double>(poke_count) * (config.poke.forward_seconds + config.poke.return_seconds);
                const double wait = randomRange(rng, 0.20, 0.70);
                plan.actions.push_back(waitAction(wait));
                spent += wait;
            }
        } else {
            while (spent < target_duration) {
                plan.actions.push_back(faceAction(faceToward(cursor, player_tile)));
                const double wait = randomRange(rng, 0.8, 1.6);
                plan.actions.push_back(waitAction(wait));
                spent += wait;
            }
        }
    } else if (selected == IdleBehaviorId::PokeInPlace) {
        double spent = 0.0;
        while (spent < target_duration) {
            plan.actions.push_back(faceAction(faceToward(follower_tile, player_tile)));
            const int poke_count = randomInt(rng, 1, 2);
            plan.actions.push_back(pokeAction(
                poke_count,
                config.poke.distance_tiles,
                config.poke.forward_seconds,
                config.poke.return_seconds));
            spent += static_cast<double>(poke_count) * (config.poke.forward_seconds + config.poke.return_seconds);
            const double wait = randomRange(rng, 0.25, 0.65);
            plan.actions.push_back(waitAction(wait));
            spent += wait;
        }
    } else if (selected == IdleBehaviorId::FaceAway) {
        plan.actions.push_back(faceAction(faceAway(follower_tile, player_tile)));
        plan.actions.push_back(waitAction(target_duration));
    } else if (selected == IdleBehaviorId::DriftAway || selected == IdleBehaviorId::GuardPost) {
        double spent = 0.0;
        GridPoint cursor = follower_tile;
        const auto target = chooseRandomTile(nav, player_tile, player_tile, selected == IdleBehaviorId::GuardPost ? 1 : 2, config.max_player_radius, rng, true);
        if (!target) return fallbackPlan(IdleBehaviorId::FaceAway, follower_facing, config);
        std::vector<GridPoint> path = nav.pathfind(cursor, *target, occupied_tile);
        if (!path.empty()) {
            plan.actions.push_back(moveAction(path, move_speed));
            spent += estimateMoveSeconds(path.size(), move_speed);
            cursor = *target;
        }
        while (spent < target_duration) {
            plan.actions.push_back(faceAction(faceAway(cursor, player_tile)));
            const double wait = randomRange(rng, 0.8, 1.4);
            plan.actions.push_back(waitAction(wait));
            spent += wait;
        }
    } else if (selected == IdleBehaviorId::JumpFidget) {
        double spent = 0.0;
        while (spent < target_duration) {
            const int jump_count = randomInt(rng, 1, 4);
            plan.actions.push_back(jumpAction(
                jump_count,
                config.jump.height_pixels,
                config.jump.duration_seconds));
            spent += static_cast<double>(jump_count) * config.jump.duration_seconds;
            if (spent >= target_duration) break;
            const double wait = randomRange(rng, 0.25, 0.60);
            plan.actions.push_back(waitAction(wait));
            spent += wait;
        }
    } else if (selected == IdleBehaviorId::HopWander) {
        GridPoint cursor = follower_tile;
        double spent = 0.0;
        while (spent < target_duration) {
            const auto target = chooseRandomTile(nav, cursor, player_tile, 1, 3, rng);
            if (!target) break;
            std::vector<GridPoint> path = nav.pathfind(cursor, *target, occupied_tile);
            if (path.empty()) continue;
            plan.actions.push_back(moveAction(path, move_speed, true));
            spent += estimateMoveSeconds(path.size(), move_speed);
            cursor = *target;
            const double wait = randomRange(rng, 0.2, 0.5);
            plan.actions.push_back(waitAction(wait));
            spent += wait;
        }
    } else if (selected == IdleBehaviorId::ShyHide) {
        double spent = 0.0;
        GridPoint cursor = follower_tile;
        GridPoint preferred = adjacentForPlayer(player_tile, player_facing, 3);
        std::optional<GridPoint> target = nav.validIdleTile(preferred, player_tile) ? std::optional<GridPoint>(preferred) : choose_adjacent(false);
        if (!target) return fallbackPlan(IdleBehaviorId::WatchPlayer, follower_facing, config);
        std::vector<GridPoint> path = nav.pathfind(cursor, *target, occupied_tile);
        if (!path.empty()) {
            plan.actions.push_back(moveAction(path, move_speed));
            spent += estimateMoveSeconds(path.size(), move_speed);
            cursor = *target;
        }
        while (spent < target_duration) {
            plan.actions.push_back(faceAction(faceToward(cursor, player_tile)));
            const double toward_wait = randomRange(rng, 0.5, 1.0);
            plan.actions.push_back(waitAction(toward_wait));
            spent += toward_wait;
            if (spent >= target_duration) break;
            plan.actions.push_back(faceAction(faceAway(cursor, player_tile)));
            const double away_wait = randomRange(rng, 0.5, 1.0);
            plan.actions.push_back(waitAction(away_wait));
            spent += away_wait;
        }
    } else if (selected == IdleBehaviorId::CirclePlayer || selected == IdleBehaviorId::DanceCircle || selected == IdleBehaviorId::SpinOrbit) {
        std::vector<GridPoint> ring;
        for (const GridPoint p : std::array<GridPoint, 4>{{{player_tile.x - 1, player_tile.y}, {player_tile.x, player_tile.y - 1}, {player_tile.x + 1, player_tile.y}, {player_tile.x, player_tile.y + 1}}}) {
            if (nav.validIdleTile(p, player_tile)) ring.push_back(p);
        }
        if (ring.size() < 3) return fallbackPlan(IdleBehaviorId::RandomWalk, follower_facing, config);
        GridPoint cursor = follower_tile;
        double spent = 0.0;
        while (spent < target_duration) {
            for (const GridPoint& p : ring) {
                if (spent >= target_duration) break;
                std::vector<GridPoint> path = nav.pathfind(cursor, p, occupied_tile);
                const double orbit_speed = move_speed * ((selected == IdleBehaviorId::SpinOrbit) ? 1.15 : 1.0);
                if (!path.empty()) {
                    plan.actions.push_back(moveAction(path, orbit_speed));
                    spent += estimateMoveSeconds(path.size(), orbit_speed);
                }
                cursor = p;
                if (selected == IdleBehaviorId::DanceCircle && randomRange(rng, 0.0, 1.0) < 0.25) {
                    const double spin_wait = randomRange(rng, 0.11, 0.19);
                    appendFaceSequence(plan.actions, fullSpinSequence(faceToward(cursor, player_tile)), spin_wait);
                    spent += spin_wait * 4.0;
                } else if (selected == IdleBehaviorId::SpinOrbit) {
                    plan.actions.push_back(faceAction(static_cast<FacingDirection>(randomInt(rng, 0, 3))));
                    spent += 0.08;
                }
            }
        }
    } else if (selected == IdleBehaviorId::Sleep) {
        IdleAction action;
        action.type = IdleActionType::Sleep;
        action.duration_seconds = std::max(3.0, target_duration);
        plan.actions.push_back(action);
    }

    if (plan.actions.empty()) {
        for (const std::string& fallback_key : config.fallback_order) {
            const auto it = behaviorMap().find(fallback_key);
            if (it != behaviorMap().end() && it->second != IdleBehaviorId::None) {
                return fallbackPlan(it->second, follower_facing, config);
            }
        }
        plan.valid = false;
    }
    return plan;
}

std::vector<GridPoint> buildFollowerPath(
    const SceneConfig& scene,
    const GridPoint& start,
    const GridPoint& goal,
    const GridPoint* occupied_tile) {
    return Navigator{scene}.pathfind(start, goal, occupied_tile);
}

} // namespace pr::gameplay::world3d::followers
