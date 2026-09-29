#include "gameplay/world3d/streaming/WorldChunkWindow.hpp"

#include <algorithm>

namespace pr::gameplay::world3d::streaming {

bool chunkContainsTile(
    const characters::LoadedWorldChunk& chunk,
    int tile_x,
    int tile_y) {
    const int width = std::max(1, chunk.scene.grid.width);
    const int height = std::max(1, chunk.scene.grid.height);
    return tile_x >= chunk.origin_tile_x && tile_y >= chunk.origin_tile_y &&
        tile_x < chunk.origin_tile_x + width && tile_y < chunk.origin_tile_y + height;
}

bool chunkIntersectsTileWindow(
    const characters::LoadedWorldChunk& chunk,
    int center_tile_x,
    int center_tile_y,
    int margin_tiles) {
    const int min_x = chunk.origin_tile_x;
    const int min_y = chunk.origin_tile_y;
    const int max_x = min_x + std::max(1, chunk.scene.grid.width) - 1;
    const int max_y = min_y + std::max(1, chunk.scene.grid.height) - 1;
    const int distance_x = center_tile_x < min_x
        ? min_x - center_tile_x
        : center_tile_x > max_x ? center_tile_x - max_x : 0;
    const int distance_y = center_tile_y < min_y
        ? min_y - center_tile_y
        : center_tile_y > max_y ? center_tile_y - max_y : 0;
    return distance_x <= std::max(0, margin_tiles) &&
        distance_y <= std::max(0, margin_tiles);
}

const characters::LoadedWorldChunk* chunkContainingTile(
    const std::vector<characters::LoadedWorldChunk>& chunks,
    int tile_x,
    int tile_y) {
    const auto found = std::find_if(
        chunks.begin(),
        chunks.end(),
        [=](const characters::LoadedWorldChunk& chunk) {
            return chunkContainsTile(chunk, tile_x, tile_y);
        });
    return found == chunks.end() ? nullptr : &*found;
}

bool chunksAreImmediateNeighbors(
    const characters::LoadedWorldChunk& center,
    const characters::LoadedWorldChunk& candidate) {
    const int center_min_x = center.origin_tile_x;
    const int center_min_y = center.origin_tile_y;
    const int center_max_x = center_min_x + std::max(1, center.scene.grid.width) - 1;
    const int center_max_y = center_min_y + std::max(1, center.scene.grid.height) - 1;
    const int candidate_min_x = candidate.origin_tile_x;
    const int candidate_min_y = candidate.origin_tile_y;
    const int candidate_max_x =
        candidate_min_x + std::max(1, candidate.scene.grid.width) - 1;
    const int candidate_max_y =
        candidate_min_y + std::max(1, candidate.scene.grid.height) - 1;
    const int gap_x = candidate_max_x < center_min_x
        ? center_min_x - candidate_max_x - 1
        : center_max_x < candidate_min_x ? candidate_min_x - center_max_x - 1 : 0;
    const int gap_y = candidate_max_y < center_min_y
        ? center_min_y - candidate_max_y - 1
        : center_max_y < candidate_min_y ? candidate_min_y - center_max_y - 1 : 0;
    const int neighborhood_width = std::max(
        std::max(1, center.scene.grid.width),
        std::max(1, candidate.scene.grid.width));
    const int neighborhood_height = std::max(
        std::max(1, center.scene.grid.height),
        std::max(1, candidate.scene.grid.height));
    return gap_x < neighborhood_width && gap_y < neighborhood_height;
}

std::string chunkIdContainingTile(
    const std::vector<characters::LoadedWorldChunk>& chunks,
    int tile_x,
    int tile_y,
    const std::string& fallback_id) {
    const characters::LoadedWorldChunk* found = chunkContainingTile(chunks, tile_x, tile_y);
    if (!found) return fallback_id;
    return found->id.empty() ? found->scene.id : found->id;
}

} // namespace pr::gameplay::world3d::streaming
