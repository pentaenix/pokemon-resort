#pragma once

#include "gameplay/world3d/characters/CharacterTerrainQuery.hpp"

#include <string>
#include <vector>

namespace pr::gameplay::world3d::streaming {

bool chunkContainsTile(
    const characters::LoadedWorldChunk& chunk,
    int tile_x,
    int tile_y);

bool chunkIntersectsTileWindow(
    const characters::LoadedWorldChunk& chunk,
    int center_tile_x,
    int center_tile_y,
    int margin_tiles);

const characters::LoadedWorldChunk* chunkContainingTile(
    const std::vector<characters::LoadedWorldChunk>& chunks,
    int tile_x,
    int tile_y);

bool chunksAreImmediateNeighbors(
    const characters::LoadedWorldChunk& center,
    const characters::LoadedWorldChunk& candidate);

std::string chunkIdContainingTile(
    const std::vector<characters::LoadedWorldChunk>& chunks,
    int tile_x,
    int tile_y,
    const std::string& fallback_id);

} // namespace pr::gameplay::world3d::streaming
