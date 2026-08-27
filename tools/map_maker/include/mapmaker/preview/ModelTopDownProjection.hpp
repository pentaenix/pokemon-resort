#pragma once

#include <span>
#include <vector>

namespace pr::mapmaker {

struct ModelTopDownPoint {
    float x = 0.0f;
    float z = 0.0f;
};

struct ModelTopDownProjection {
    std::vector<ModelTopDownPoint> outline;
    float width = 0.0f;
    float depth = 0.0f;
    bool valid = false;
};

struct PlacedModelTopDownProjection {
    std::vector<ModelTopDownPoint> outline_tiles;
    float width_tiles = 0.0f;
    float depth_tiles = 0.0f;
};

// Produces the convex top-down silhouette of model-space X/Z vertices.
ModelTopDownProjection buildModelTopDownProjection(std::span<const ModelTopDownPoint> points);

// Applies the shipping model placement yaw/scale convention and converts the
// local silhouette into offsets measured in map tiles.
PlacedModelTopDownProjection placeModelTopDownProjection(
    const ModelTopDownProjection& projection,
    float yaw_degrees,
    float scale,
    float tile_size);

} // namespace pr::mapmaker
