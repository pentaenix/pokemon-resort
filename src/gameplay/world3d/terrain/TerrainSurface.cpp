#include "gameplay/world3d/terrain/TerrainSurface.hpp"

#include <algorithm>
#include <cmath>

namespace pr::gameplay::world3d::terrain {

namespace {

enum Special : int {
    kFlat = 0,
    kRampNorth = 2,
    kRampEast = 3,
    kRampSouth = 4,
    kRampWest = 5,
    kConvexNE = 6,
    kConvexSE = 7,
    kConvexSW = 8,
    kConvexNW = 9,
    kConcaveNE = 10,
    kConcaveSE = 11,
    kConcaveSW = 12,
    kConcaveNW = 13,
};

int tileHeightUnits(const SceneConfig& scene, int tx, int ty) {
    if (scene.terrain.heights.empty()) return 0;
    if (ty < 0 || ty >= static_cast<int>(scene.terrain.heights.size())) return 0;
    const auto& row = scene.terrain.heights[static_cast<std::size_t>(ty)];
    if (tx < 0 || tx >= static_cast<int>(row.size())) return 0;
    return static_cast<int>(row[static_cast<std::size_t>(tx)]);
}

int tileSpecial(const SceneConfig& scene, int tx, int ty) {
    if (scene.terrain.specials.empty()) return kFlat;
    if (ty < 0 || ty >= static_cast<int>(scene.terrain.specials.size())) return kFlat;
    const auto& row = scene.terrain.specials[static_cast<std::size_t>(ty)];
    if (tx < 0 || tx >= static_cast<int>(row.size())) return kFlat;
    return static_cast<int>(row[static_cast<std::size_t>(tx)]);
}

float sampleBilinear(const float corners[4], float u, float v) {
    const float north = corners[0] + ((corners[1] - corners[0]) * u);
    const float south = corners[3] + ((corners[2] - corners[3]) * u);
    return north + ((south - north) * v);
}

float sampleBilinearClamped(const float corners[4], float u, float v) {
    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);
    const float north = corners[0] + ((corners[1] - corners[0]) * u);
    const float south = corners[3] + ((corners[2] - corners[3]) * u);
    return north + ((south - north) * v);
}

float sampleCardinalRampWithInset(
    const SceneConfig& scene,
    int special,
    int height_units,
    float u,
    float v) {
    const float floor_height = heightPerFloor(scene);
    const float low = static_cast<float>(height_units) * floor_height;
    const float high = static_cast<float>(height_units + 1) * floor_height;
    const float tile_size = std::max(1.0f, scene.grid.tile_size);
    const float inset = std::clamp(scene.terrain.ramp_incline_inset_px / tile_size, 0.0f, 0.95f);

    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);

    float along = 0.0f;
    switch (special) {
        case kRampNorth:
            along = 1.0f - v;
            break;
        case kRampEast:
            along = u;
            break;
        case kRampSouth:
            along = v;
            break;
        case kRampWest:
            along = 1.0f - u;
            break;
        default:
            return low;
    }

    if (inset <= 0.0f) {
        return low + ((high - low) * along);
    }
    if (along <= inset) {
        return low;
    }
    const float slope_t = std::clamp((along - inset) / (1.0f - inset), 0.0f, 1.0f);
    return low + ((high - low) * slope_t);
}

void applyCardinalRampCorners(int direction, float low, float high, float out_corners[4]) {
    out_corners[0] = low;
    out_corners[1] = low;
    out_corners[2] = low;
    out_corners[3] = low;
    switch (direction) {
        case kRampNorth:
            out_corners[0] = high;
            out_corners[1] = high;
            out_corners[2] = low;
            out_corners[3] = low;
            break;
        case kRampEast:
            out_corners[0] = low;
            out_corners[1] = high;
            out_corners[2] = high;
            out_corners[3] = low;
            break;
        case kRampSouth:
            out_corners[0] = low;
            out_corners[1] = low;
            out_corners[2] = high;
            out_corners[3] = high;
            break;
        case kRampWest:
            out_corners[0] = high;
            out_corners[1] = low;
            out_corners[2] = low;
            out_corners[3] = high;
            break;
        default:
            break;
    }
}

void fillTileCornerHeightsLocal(const SceneConfig& scene, int tx, int ty, float out_corners[4]) {
    const float floor_height = heightPerFloor(scene);
    const int h = tileHeightUnits(scene, tx, ty);
    const float low = static_cast<float>(h) * floor_height;
    const float high = static_cast<float>(h + 1) * floor_height;
    out_corners[0] = low;
    out_corners[1] = low;
    out_corners[2] = low;
    out_corners[3] = low;

    const int special = tileSpecial(scene, tx, ty);
    if (special >= kRampNorth && special <= kRampWest) {
        applyCardinalRampCorners(special, low, high, out_corners);
        return;
    }

    switch (special) {
        case kConvexNE:
            out_corners[2] = high;
            break;
        case kConvexSE:
            out_corners[1] = high;
            break;
        case kConvexSW:
            out_corners[0] = high;
            break;
        case kConvexNW:
            out_corners[3] = high;
            break;
        case kConcaveNE:
            out_corners[0] = high;
            out_corners[1] = high;
            out_corners[3] = high;
            break;
        case kConcaveSE:
            out_corners[0] = high;
            out_corners[3] = high;
            break;
        case kConcaveSW:
            out_corners[2] = high;
            break;
        case kConcaveNW:
            out_corners[1] = high;
            out_corners[2] = high;
            break;
        default:
            break;
    }
}

void rampAscendVectorLocal(int ramp_direction, int& out_dx, int& out_dy) {
    out_dx = 0;
    out_dy = 0;
    if (ramp_direction == kRampNorth) {
        out_dy = -1;
    } else if (ramp_direction == kRampEast) {
        out_dx = 1;
    } else if (ramp_direction == kRampSouth) {
        out_dy = 1;
    } else if (ramp_direction == kRampWest) {
        out_dx = -1;
    }
}

bool rampStepUsesTile(int special, int step_dx, int step_dy) {
    if (special < kRampNorth || special > kRampWest) {
        return false;
    }
    int ax = 0;
    int ay = 0;
    rampAscendVectorLocal(special, ax, ay);
    return (step_dx == ax && step_dy == ay) || (step_dx == -ax && step_dy == -ay);
}

bool tileHasTraversableSlope(const SceneConfig& scene, int tx, int ty, int step_dx, int step_dy) {
    const int special = tileSpecial(scene, tx, ty);
    if (special >= kRampNorth && special <= kRampWest) {
        return rampStepUsesTile(special, step_dx, step_dy);
    }
    return special >= kConvexNE && special <= kConcaveNW;
}

} // namespace

bool isSlopeSpecial(int special) {
    return special >= 2 && special <= 13;
}

float heightPerFloor(const SceneConfig& scene) {
    return scene.terrain.height_per_floor > 0.0f
        ? scene.terrain.height_per_floor
        : std::max(1.0f, scene.grid.tile_size);
}

void rampAscendVector(int ramp_direction, int& out_dx, int& out_dy) {
    out_dx = 0;
    out_dy = 0;
    if (ramp_direction == kRampNorth) {
        out_dy = -1;
    } else if (ramp_direction == kRampEast) {
        out_dx = 1;
    } else if (ramp_direction == kRampSouth) {
        out_dy = 1;
    } else if (ramp_direction == kRampWest) {
        out_dx = -1;
    }
}

TileCoord resolveRampSampleTile(
    const SceneConfig& scene,
    int from_tx,
    int from_ty,
    int to_tx,
    int to_ty,
    int step_dx,
    int step_dy,
    int height_delta_units) {
    if (height_delta_units > 0) {
        return TileCoord{from_tx, from_ty};
    }
    if (height_delta_units < 0) {
        return TileCoord{to_tx, to_ty};
    }

    const bool from_slope = tileHasTraversableSlope(scene, from_tx, from_ty, step_dx, step_dy);
    const bool to_slope = tileHasTraversableSlope(scene, to_tx, to_ty, step_dx, step_dy);
    if (from_slope && !to_slope) {
        return TileCoord{from_tx, from_ty};
    }
    if (to_slope && !from_slope) {
        return TileCoord{to_tx, to_ty};
    }
    if (from_slope && to_slope) {
        return TileCoord{from_tx, from_ty};
    }
    const int from_spec = tileSpecial(scene, from_tx, from_ty);
    const int to_spec = tileSpecial(scene, to_tx, to_ty);
    if (isSlopeSpecial(from_spec)) {
        return TileCoord{from_tx, from_ty};
    }
    if (isSlopeSpecial(to_spec)) {
        return TileCoord{to_tx, to_ty};
    }
    return TileCoord{from_tx, from_ty};
}

TileCoord resolveRampSampleTileEnd(
    const SceneConfig& scene,
    int from_tx,
    int from_ty,
    int to_tx,
    int to_ty,
    int step_dx,
    int step_dy,
    int height_delta_units) {
    if (height_delta_units != 0) {
        return resolveRampSampleTile(scene, from_tx, from_ty, to_tx, to_ty, step_dx, step_dy, height_delta_units);
    }

    const bool from_slope = tileHasTraversableSlope(scene, from_tx, from_ty, step_dx, step_dy);
    const bool to_slope = tileHasTraversableSlope(scene, to_tx, to_ty, step_dx, step_dy);
    if (from_slope && to_slope) {
        return TileCoord{to_tx, to_ty};
    }
    return resolveRampSampleTile(scene, from_tx, from_ty, to_tx, to_ty, step_dx, step_dy, height_delta_units);
}

void fillTileCornerHeights(const SceneConfig& scene, int tx, int ty, float out_corners[4]) {
    fillTileCornerHeightsLocal(scene, tx, ty, out_corners);
}

float heightAtWorldPositionStitched(
    const SceneConfig& scene,
    float world_x,
    float world_z,
    int primary_tx,
    int primary_ty) {
    const float tile_size = std::max(1.0f, scene.grid.tile_size);
    const int grid_w = std::max(1, scene.grid.width);
    const int grid_h = std::max(1, scene.grid.height);

    auto sample_on = [&](int tx, int ty, float u, float v) -> float {
        const int special = tileSpecial(scene, tx, ty);
        if (special >= kRampNorth && special <= kRampWest) {
            return sampleCardinalRampWithInset(scene, special, tileHeightUnits(scene, tx, ty), u, v);
        }
        float corners[4]{};
        fillTileCornerHeightsLocal(scene, tx, ty, corners);
        return sampleBilinearClamped(corners, u, v);
    };

    const float u = (world_x - (static_cast<float>(primary_tx) * tile_size)) / tile_size;
    const float v = (world_z - (static_cast<float>(primary_ty) * tile_size)) / tile_size;

    if (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f) {
        return sample_on(primary_tx, primary_ty, u, v);
    }

    float h_a = sample_on(primary_tx, primary_ty, std::clamp(u, 0.0f, 1.0f), std::clamp(v, 0.0f, 1.0f));

    if (v < 0.0f && primary_ty > 0) {
        const float t = std::clamp(-v, 0.0f, 1.0f);
        const float h_b = sample_on(primary_tx, primary_ty - 1, u, 1.0f + v);
        h_a = h_b + ((h_a - h_b) * (1.0f - t));
    } else if (v > 1.0f && primary_ty + 1 < grid_h) {
        const float t = std::clamp(v - 1.0f, 0.0f, 1.0f);
        const float h_b = sample_on(primary_tx, primary_ty + 1, u, v - 1.0f);
        h_a = h_a + ((h_b - h_a) * t);
    }

    if (u < 0.0f && primary_tx > 0) {
        const float t = std::clamp(-u, 0.0f, 1.0f);
        const float h_b = sample_on(primary_tx - 1, primary_ty, 1.0f + u, v);
        h_a = h_b + ((h_a - h_b) * (1.0f - t));
    } else if (u > 1.0f && primary_tx + 1 < grid_w) {
        const float t = std::clamp(u - 1.0f, 0.0f, 1.0f);
        const float h_b = sample_on(primary_tx + 1, primary_ty, u - 1.0f, v);
        h_a = h_a + ((h_b - h_a) * t);
    }

    return h_a;
}

float heightAtWorldPositionOnTile(
    const SceneConfig& scene,
    float world_x,
    float world_z,
    int sample_tx,
    int sample_ty,
    bool clamp_uv) {
    const float tile_size = std::max(1.0f, scene.grid.tile_size);
    const float u = (world_x - (static_cast<float>(sample_tx) * tile_size)) / tile_size;
    const float v = (world_z - (static_cast<float>(sample_ty) * tile_size)) / tile_size;

    float corners[4]{};
    const int special = tileSpecial(scene, sample_tx, sample_ty);
    if (special >= kRampNorth && special <= kRampWest) {
        if (clamp_uv) {
            return sampleCardinalRampWithInset(scene, special, tileHeightUnits(scene, sample_tx, sample_ty), u, v);
        }
        const float floor_height = heightPerFloor(scene);
        const float low = static_cast<float>(tileHeightUnits(scene, sample_tx, sample_ty)) * floor_height;
        const float high = static_cast<float>(tileHeightUnits(scene, sample_tx, sample_ty) + 1) * floor_height;
        float along = special == kRampNorth ? 1.0f - v :
            special == kRampEast ? u :
            special == kRampSouth ? v :
            1.0f - u;
        const float inset = std::clamp(scene.terrain.ramp_incline_inset_px / tile_size, 0.0f, 0.95f);
        along = inset <= 0.0f ? along : (along <= inset ? 0.0f : (along - inset) / (1.0f - inset));
        return low + ((high - low) * along);
    }
    fillTileCornerHeightsLocal(scene, sample_tx, sample_ty, corners);
    if (clamp_uv) {
        return sampleBilinearClamped(corners, u, v);
    }
    return sampleBilinear(corners, u, v);
}

float heightAtWorldPosition(
    const SceneConfig& scene,
    float world_x,
    float world_z,
    int fallback_tx,
    int fallback_ty) {
    const float tile_size = std::max(1.0f, scene.grid.tile_size);
    int tx = static_cast<int>(std::floor(world_x / tile_size));
    int ty = static_cast<int>(std::floor(world_z / tile_size));
    const int grid_w = std::max(1, scene.grid.width);
    const int grid_h = std::max(1, scene.grid.height);
    if (tx < 0 || tx >= grid_w || ty < 0 || ty >= grid_h) {
        tx = fallback_tx;
        ty = fallback_ty;
    }
    return heightAtWorldPositionOnTile(scene, world_x, world_z, tx, ty, true);
}

float heightAtTileCenter(const SceneConfig& scene, int tx, int ty) {
    const float tile_size = std::max(1.0f, scene.grid.tile_size);
    const float cx = (static_cast<float>(tx) + 0.5f) * tile_size;
    const float cz = (static_cast<float>(ty) + 0.5f) * tile_size;
    return heightAtActorFeet(scene, cx, cz, tx, ty);
}

bool isSmoothRampHeightStep(
    const SceneConfig& scene,
    int from_tx,
    int from_ty,
    int to_tx,
    int to_ty,
    int step_dx,
    int step_dy) {
    const int from_h = tileHeightUnits(scene, from_tx, from_ty);
    const int to_h = tileHeightUnits(scene, to_tx, to_ty);
    const int dh = to_h - from_h;
    if (dh == 0) {
        return tileHasTraversableSlope(scene, from_tx, from_ty, step_dx, step_dy) ||
            tileHasTraversableSlope(scene, to_tx, to_ty, step_dx, step_dy);
    }
    if (std::abs(dh) > 1) {
        return false;
    }
    const auto ramp_allows = [&](int ramp_tile_x, int ramp_tile_y) -> bool {
        const int special = tileSpecial(scene, ramp_tile_x, ramp_tile_y);
        if (special < kRampNorth || special > kRampWest) {
            return false;
        }
        int ax = 0;
        int ay = 0;
        rampAscendVectorLocal(special, ax, ay);
        if (dh > 0) {
            return step_dx == ax && step_dy == ay;
        }
        return step_dx == -ax && step_dy == -ay;
    };
    return dh > 0 ? ramp_allows(from_tx, from_ty) : ramp_allows(to_tx, to_ty);
}

float heightAtActorFeet(
    const SceneConfig& scene,
    float foot_x,
    float foot_z,
    int logical_tx,
    int logical_ty) {
    return heightAtWorldPositionStitched(scene, foot_x, foot_z, logical_tx, logical_ty);
}

} // namespace pr::gameplay::world3d::terrain
