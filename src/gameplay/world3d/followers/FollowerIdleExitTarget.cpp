#include "gameplay/world3d/followers/FollowerIdleExitTarget.hpp"

#include <array>
#include <limits>

namespace pr::gameplay::world3d::followers {

namespace {

bool isAdjacentToPlayer(const GridPoint& player_tile, const GridPoint& follower_tile) {
    return std::abs(player_tile.x - follower_tile.x) + std::abs(player_tile.y - follower_tile.y) == 1;
}

bool isValidAdjacentCandidate(
    const SceneConfig& scene,
    const GridPoint& tile,
    const GridPoint& player_tile) {
    if (tile.x == player_tile.x && tile.y == player_tile.y) return false;
    if (scene.terrain.heights.empty()) {
        return tile.x >= 0 && tile.x < scene.grid.width && tile.y >= 0 && tile.y < scene.grid.height;
    }
    if (tile.y < 0 || tile.y >= static_cast<int>(scene.terrain.heights.size())) return false;
    const auto& height_row = scene.terrain.heights[static_cast<std::size_t>(tile.y)];
    if (tile.x < 0 || tile.x >= static_cast<int>(height_row.size())) return false;
    if (!scene.terrain.collision.empty()) {
        const auto& collision_row = scene.terrain.collision[static_cast<std::size_t>(tile.y)];
        if (tile.x < static_cast<int>(collision_row.size()) &&
            collision_row[static_cast<std::size_t>(tile.x)] != 0) {
            return false;
        }
    }
    for (const auto& npc : scene.characters) {
        if (npc.tile_x == tile.x && npc.tile_y == tile.y) return false;
    }
    return true;
}

} // namespace

std::optional<GridPoint> selectIdleExitTarget(
    const SceneConfig& scene,
    const GridPoint& player_tile,
    const GridPoint& follower_tile,
    const GridPoint& idle_origin_tile,
    const GridPoint* occupied_tile) {
    if (!isAdjacentToPlayer(player_tile, follower_tile)) {
        return idle_origin_tile;
    }

    const std::array<GridPoint, 4> candidates{{
        {player_tile.x, player_tile.y - 1},
        {player_tile.x + 1, player_tile.y},
        {player_tile.x, player_tile.y + 1},
        {player_tile.x - 1, player_tile.y},
    }};

    std::optional<GridPoint> best_tile;
    std::size_t best_path_len = std::numeric_limits<std::size_t>::max();
    int best_manhattan = std::numeric_limits<int>::max();

    for (const GridPoint& candidate : candidates) {
        if (!isValidAdjacentCandidate(scene, candidate, player_tile)) continue;
        if (candidate.x == follower_tile.x && candidate.y == follower_tile.y) {
            return candidate;
        }
        const auto path = buildFollowerPath(scene, follower_tile, candidate, occupied_tile);
        if (path.empty()) continue;
        const int manhattan = std::abs(candidate.x - follower_tile.x) + std::abs(candidate.y - follower_tile.y);
        if (!best_tile || path.size() < best_path_len ||
            (path.size() == best_path_len && manhattan < best_manhattan)) {
            best_tile = candidate;
            best_path_len = path.size();
            best_manhattan = manhattan;
        }
    }

    if (best_tile) return best_tile;
    return idle_origin_tile;
}

} // namespace pr::gameplay::world3d::followers
