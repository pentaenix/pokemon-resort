#include "gameplay/world3d/streaming/WorldChunkWindow.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using pr::gameplay::world3d::characters::LoadedWorldChunk;
using namespace pr::gameplay::world3d::streaming;

void expect(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "[FAIL] " << message << '\n';
    std::exit(1);
}

LoadedWorldChunk chunk(std::string id, int origin_x, int origin_y) {
    LoadedWorldChunk result;
    result.id = std::move(id);
    result.scene.grid.width = 32;
    result.scene.grid.height = 32;
    result.origin_tile_x = origin_x;
    result.origin_tile_y = origin_y;
    return result;
}

} // namespace

int main() {
    const LoadedWorldChunk center = chunk("center", 0, 0);
    const LoadedWorldChunk east = chunk("east", 32, 0);
    const LoadedWorldChunk far_east = chunk("far_east", 64, 0);
    const LoadedWorldChunk north = chunk("north", 0, -32);
    const LoadedWorldChunk north_east = chunk("north_east", 32, -32);

    expect(chunkContainsTile(center, 0, 0), "chunk includes its first tile");
    expect(chunkContainsTile(center, 31, 31), "chunk includes its last tile");
    expect(!chunkContainsTile(center, 32, 31), "chunk excludes the next column");
    expect(chunkContainsTile(east, 32, 0), "adjacent chunk owns boundary tile");

    expect(
        chunkIntersectsTileWindow(east, 15, 15, 17),
        "preload margin reaches an adjacent chunk");
    expect(
        !chunkIntersectsTileWindow(east, 14, 15, 16),
        "chunk outside preload margin stays unloaded");
    expect(
        chunkIntersectsTileWindow(north, 15, 0, 1),
        "window reaches a north neighbor across the seam");
    expect(chunksAreImmediateNeighbors(center, center), "center chunk stays resident");
    expect(chunksAreImmediateNeighbors(center, east), "east neighbor stays resident");
    expect(chunksAreImmediateNeighbors(center, north_east), "diagonal neighbor stays resident");
    expect(!chunksAreImmediateNeighbors(center, far_east), "two-away chunk is not resident");

    const std::vector<LoadedWorldChunk> chunks{center, east, north};
    expect(
        chunkIdContainingTile(chunks, 47, 12, "fallback") == "east",
        "containing chunk id follows player position");
    expect(
        chunkIdContainingTile(chunks, 200, 200, "fallback") == "fallback",
        "outside position uses fallback id");

    std::cout << "[PASS] world chunk window tests\n";
    return 0;
}
