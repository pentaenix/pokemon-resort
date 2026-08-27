#include "gameplay/world3d/terrain/TerrainSurface.hpp"

#include "gameplay/world3d/interiors/DefaultRoom.hpp"

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

bool tileBlocked(const SceneConfig& scene, int tx, int ty) {
    if (interiors::boundaryCellBlocked(scene, tx, ty)) return true;
    if (scene.terrain.collision.empty()) return false;
    if (ty < 0 || ty >= static_cast<int>(scene.terrain.collision.size())) return false;
    const auto& row = scene.terrain.collision[static_cast<std::size_t>(ty)];
    if (tx < 0 || tx >= static_cast<int>(row.size())) return false;
    return row[static_cast<std::size_t>(tx)] != 0;
}

bool inBounds(const SceneConfig& scene, int tx, int ty) {
    return tx >= 0 && ty >= 0 && tx < std::max(1, scene.grid.width) && ty < std::max(1, scene.grid.height);
}

bool isCardinalRamp(int special) {
    return special >= kRampNorth && special <= kRampWest;
}

struct RampAxis {
    int dx = 0;
    int dy = 0;
};

RampAxis rampAxis(int special) {
    RampAxis axis{};
    if (special == kRampNorth) {
        axis.dy = -1;
    } else if (special == kRampEast) {
        axis.dx = 1;
    } else if (special == kRampSouth) {
        axis.dy = 1;
    } else if (special == kRampWest) {
        axis.dx = -1;
    }
    return axis;
}

struct CardinalRampRun {
    int start_x = 0;
    int start_y = 0;
    int index = 0;
    int count = 1;
    float low_units = 0.0f;
    float high_units = 1.0f;
};

CardinalRampRun solveCardinalRampRun(const SceneConfig& scene, int tx, int ty, int special) {
    const RampAxis axis = rampAxis(special);
    CardinalRampRun run{};
    run.start_x = tx;
    run.start_y = ty;

    while (tileSpecial(scene, run.start_x - axis.dx, run.start_y - axis.dy) == special) {
        run.start_x -= axis.dx;
        run.start_y -= axis.dy;
        ++run.index;
    }

    int end_x = run.start_x;
    int end_y = run.start_y;
    int min_base = tileHeightUnits(scene, end_x, end_y);
    int max_base = min_base;
    run.count = 1;
    while (tileSpecial(scene, end_x + axis.dx, end_y + axis.dy) == special) {
        end_x += axis.dx;
        end_y += axis.dy;
        ++run.count;
        const int h = tileHeightUnits(scene, end_x, end_y);
        min_base = std::min(min_base, h);
        max_base = std::max(max_base, h);
    }

    const int low_x = run.start_x - axis.dx;
    const int low_y = run.start_y - axis.dy;
    const int high_x = end_x + axis.dx;
    const int high_y = end_y + axis.dy;

    if (inBounds(scene, low_x, low_y)) {
        run.low_units = static_cast<float>(tileHeightUnits(scene, low_x, low_y));
    } else {
        run.low_units = static_cast<float>(min_base);
    }

    if (inBounds(scene, high_x, high_y)) {
        run.high_units = static_cast<float>(tileHeightUnits(scene, high_x, high_y));
    } else {
        run.high_units = static_cast<float>(max_base + 1);
    }
    if (run.high_units <= run.low_units) {
        run.high_units = std::max(run.low_units + 1.0f, static_cast<float>(max_base + 1));
    }
    return run;
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

void fillCardinalRampCorners(const SceneConfig& scene, int tx, int ty, int special, float out_corners[4]) {
    const float floor_height = heightPerFloor(scene);
    const CardinalRampRun run = solveCardinalRampRun(scene, tx, ty, special);
    const float t0 = static_cast<float>(run.index) / static_cast<float>(std::max(1, run.count));
    const float t1 = static_cast<float>(run.index + 1) / static_cast<float>(std::max(1, run.count));
    const float low_edge = (run.low_units + ((run.high_units - run.low_units) * t0)) * floor_height;
    const float high_edge = (run.low_units + ((run.high_units - run.low_units) * t1)) * floor_height;
    applyCardinalRampCorners(special, low_edge, high_edge, out_corners);
}

bool isNorthSouthRamp(int special) {
    return special == kRampNorth || special == kRampSouth;
}

bool isEastWestRamp(int special) {
    return special == kRampEast || special == kRampWest;
}

bool isFlatWalkableLandingAtHeight(const SceneConfig& scene, int tx, int ty, float height_units) {
    if (!inBounds(scene, tx, ty)) return false;
    if (tileBlocked(scene, tx, ty)) return false;
    if (tileSpecial(scene, tx, ty) != kFlat) return false;
    return std::abs(static_cast<float>(tileHeightUnits(scene, tx, ty)) - height_units) < 0.001f;
}

void snapAttachedRampTopLandingCorner(
    const SceneConfig& scene,
    int side_tx,
    int side_ty,
    int side_special,
    int spine_tx,
    int spine_ty,
    int spine_special,
    float out_corners[4]) {
    const CardinalRampRun run = solveCardinalRampRun(scene, spine_tx, spine_ty, spine_special);
    const RampAxis axis = rampAxis(spine_special);
    const int landing_x = side_tx + axis.dx;
    const int landing_y = side_ty + axis.dy;
    if (!isFlatWalkableLandingAtHeight(scene, landing_x, landing_y, run.high_units)) {
        return;
    }

    const float landing_height = run.high_units * heightPerFloor(scene);
    if (axis.dy < 0) {
        if (side_special == kRampWest) out_corners[1] = landing_height;
        else if (side_special == kRampEast) out_corners[0] = landing_height;
    } else if (axis.dy > 0) {
        if (side_special == kRampWest) out_corners[2] = landing_height;
        else if (side_special == kRampEast) out_corners[3] = landing_height;
    } else if (axis.dx > 0) {
        if (side_special == kRampNorth) out_corners[2] = landing_height;
        else if (side_special == kRampSouth) out_corners[1] = landing_height;
    } else if (axis.dx < 0) {
        if (side_special == kRampNorth) out_corners[3] = landing_height;
        else if (side_special == kRampSouth) out_corners[0] = landing_height;
    }
}

bool applyPerpendicularRampAttachment(const SceneConfig& scene, int tx, int ty, int special, float base_height, float out_corners[4]) {
    float spine[4]{};
    if (special == kRampWest && isNorthSouthRamp(tileSpecial(scene, tx - 1, ty))) {
        const int spine_special = tileSpecial(scene, tx - 1, ty);
        fillCardinalRampCorners(scene, tx - 1, ty, spine_special, spine);
        out_corners[0] = spine[1];
        out_corners[1] = base_height;
        out_corners[2] = base_height;
        out_corners[3] = spine[2];
        snapAttachedRampTopLandingCorner(scene, tx, ty, special, tx - 1, ty, spine_special, out_corners);
        return true;
    }
    if (special == kRampEast && isNorthSouthRamp(tileSpecial(scene, tx + 1, ty))) {
        const int spine_special = tileSpecial(scene, tx + 1, ty);
        fillCardinalRampCorners(scene, tx + 1, ty, spine_special, spine);
        out_corners[0] = base_height;
        out_corners[1] = spine[0];
        out_corners[2] = spine[3];
        out_corners[3] = base_height;
        snapAttachedRampTopLandingCorner(scene, tx, ty, special, tx + 1, ty, spine_special, out_corners);
        return true;
    }
    if (special == kRampNorth && isEastWestRamp(tileSpecial(scene, tx, ty - 1))) {
        const int spine_special = tileSpecial(scene, tx, ty - 1);
        fillCardinalRampCorners(scene, tx, ty - 1, spine_special, spine);
        out_corners[0] = spine[3];
        out_corners[1] = spine[2];
        out_corners[2] = base_height;
        out_corners[3] = base_height;
        snapAttachedRampTopLandingCorner(scene, tx, ty, special, tx, ty - 1, spine_special, out_corners);
        return true;
    }
    if (special == kRampSouth && isEastWestRamp(tileSpecial(scene, tx, ty + 1))) {
        const int spine_special = tileSpecial(scene, tx, ty + 1);
        fillCardinalRampCorners(scene, tx, ty + 1, spine_special, spine);
        out_corners[0] = base_height;
        out_corners[1] = base_height;
        out_corners[2] = spine[1];
        out_corners[3] = spine[0];
        snapAttachedRampTopLandingCorner(scene, tx, ty, special, tx, ty + 1, spine_special, out_corners);
        return true;
    }
    return false;
}

float cornerLandingHeight(const SceneConfig& scene, int tx, int ty, int corner_index, float fallback_height) {
    int ax = 0;
    int ay = 0;
    int bx = 0;
    int by = 0;
    int dx = 0;
    int dy = 0;
    if (corner_index == 0) {
        ax = 0; ay = -1; bx = -1; by = 0; dx = -1; dy = -1;
    } else if (corner_index == 1) {
        ax = 0; ay = -1; bx = 1; by = 0; dx = 1; dy = -1;
    } else if (corner_index == 2) {
        ax = 0; ay = 1; bx = 1; by = 0; dx = 1; dy = 1;
    } else {
        ax = 0; ay = 1; bx = -1; by = 0; dx = -1; dy = 1;
    }

    const float floor_height = heightPerFloor(scene);
    float high = fallback_height;
    const auto consider = [&](int ox, int oy) {
        if (inBounds(scene, tx + ox, ty + oy)) {
            high = std::max(high, static_cast<float>(tileHeightUnits(scene, tx + ox, ty + oy)) * floor_height);
        }
    };
    consider(ax, ay);
    consider(bx, by);
    consider(dx, dy);
    return high;
}

void setSmartCornerHigh(const SceneConfig& scene, int tx, int ty, int corner_index, float fallback_height, float out_corners[4]) {
    out_corners[corner_index] = cornerLandingHeight(scene, tx, ty, corner_index, fallback_height);
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
    if (isCardinalRamp(special)) {
        if (applyPerpendicularRampAttachment(scene, tx, ty, special, low, out_corners)) {
            return;
        }
        fillCardinalRampCorners(scene, tx, ty, special, out_corners);
        return;
    }

    switch (special) {
        case kConvexNE:
            setSmartCornerHigh(scene, tx, ty, 1, high, out_corners);
            break;
        case kConvexSE:
            setSmartCornerHigh(scene, tx, ty, 2, high, out_corners);
            break;
        case kConvexSW:
            setSmartCornerHigh(scene, tx, ty, 3, high, out_corners);
            break;
        case kConvexNW:
            setSmartCornerHigh(scene, tx, ty, 0, high, out_corners);
            break;
        case kConcaveNE:
            setSmartCornerHigh(scene, tx, ty, 0, high, out_corners);
            setSmartCornerHigh(scene, tx, ty, 2, high, out_corners);
            setSmartCornerHigh(scene, tx, ty, 3, high, out_corners);
            break;
        case kConcaveSE:
            setSmartCornerHigh(scene, tx, ty, 0, high, out_corners);
            setSmartCornerHigh(scene, tx, ty, 1, high, out_corners);
            setSmartCornerHigh(scene, tx, ty, 3, high, out_corners);
            break;
        case kConcaveSW:
            setSmartCornerHigh(scene, tx, ty, 0, high, out_corners);
            setSmartCornerHigh(scene, tx, ty, 1, high, out_corners);
            setSmartCornerHigh(scene, tx, ty, 2, high, out_corners);
            break;
        case kConcaveNW:
            setSmartCornerHigh(scene, tx, ty, 1, high, out_corners);
            setSmartCornerHigh(scene, tx, ty, 2, high, out_corners);
            setSmartCornerHigh(scene, tx, ty, 3, high, out_corners);
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
    const bool water_transition =
        isActualWaterTile(scene, from_tx, from_ty) ||
        isActualWaterTile(scene, to_tx, to_ty) ||
        (from_ty >= 0 && from_ty < static_cast<int>(scene.water_terrain.shoreline_cells.size()) &&
         from_tx >= 0 && from_tx < static_cast<int>(scene.water_terrain.shoreline_cells[static_cast<std::size_t>(from_ty)].size()) &&
         scene.water_terrain.shoreline_cells[static_cast<std::size_t>(from_ty)][static_cast<std::size_t>(from_tx)] != 0) ||
        (to_ty >= 0 && to_ty < static_cast<int>(scene.water_terrain.shoreline_cells.size()) &&
         to_tx >= 0 && to_tx < static_cast<int>(scene.water_terrain.shoreline_cells[static_cast<std::size_t>(to_ty)].size()) &&
         scene.water_terrain.shoreline_cells[static_cast<std::size_t>(to_ty)][static_cast<std::size_t>(to_tx)] != 0);
    if (water_transition) {
        const float from_y = heightAtTileCenter(scene, from_tx, from_ty);
        const float to_y = heightAtTileCenter(scene, to_tx, to_ty);
        if (std::abs(from_y - to_y) > 0.001f) return true;
    }
    const int from_h = tileHeightUnits(scene, from_tx, from_ty);
    const int to_h = tileHeightUnits(scene, to_tx, to_ty);
    const int dh = to_h - from_h;
    if (canTraverseTerrainEdge(scene, from_tx, from_ty, to_tx, to_ty, step_dx, step_dy)) {
        const bool from_slope = isSlopeSpecial(tileSpecial(scene, from_tx, from_ty));
        const bool to_slope = isSlopeSpecial(tileSpecial(scene, to_tx, to_ty));
        if (from_slope || to_slope) {
            return true;
        }
    }
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

bool canTraverseTerrainEdge(
    const SceneConfig& scene,
    int from_tx,
    int from_ty,
    int to_tx,
    int to_ty,
    int step_dx,
    int step_dy) {
    if (std::abs(step_dx) + std::abs(step_dy) != 1) {
        return false;
    }
    if (!inBounds(scene, from_tx, from_ty) || !inBounds(scene, to_tx, to_ty)) {
        return false;
    }

    float from_c[4]{};
    float to_c[4]{};
    fillTileCornerHeightsLocal(scene, from_tx, from_ty, from_c);
    fillTileCornerHeightsLocal(scene, to_tx, to_ty, to_c);

    float a0 = 0.0f;
    float a1 = 0.0f;
    float b0 = 0.0f;
    float b1 = 0.0f;
    if (step_dx == 1) {
        a0 = from_c[1]; a1 = from_c[2];
        b0 = to_c[0]; b1 = to_c[3];
    } else if (step_dx == -1) {
        a0 = from_c[0]; a1 = from_c[3];
        b0 = to_c[1]; b1 = to_c[2];
    } else if (step_dy == 1) {
        a0 = from_c[3]; a1 = from_c[2];
        b0 = to_c[0]; b1 = to_c[1];
    } else {
        a0 = from_c[0]; a1 = from_c[1];
        b0 = to_c[3]; b1 = to_c[2];
    }

    const float epsilon = std::max(0.05f, heightPerFloor(scene) * 0.02f);
    return std::abs(a0 - b0) <= epsilon && std::abs(a1 - b1) <= epsilon;
}

float heightAtActorFeet(
    const SceneConfig& scene,
    float foot_x,
    float foot_z,
    int logical_tx,
    int logical_ty) {
    const float terrain_height = heightAtWorldPositionStitched(scene, foot_x, foot_z, logical_tx, logical_ty);
    if (!scene.water_terrain.enabled) return terrain_height;
    if (isActualWaterTile(scene, logical_tx, logical_ty)) {
        return scene.water_terrain.surface_height_world;
    }
    if (!scene.water_terrain.shoreline_ramp_enabled) {
        return terrain_height;
    }
    const float progress = shorelineProgressAtWorldPosition(
        scene, foot_x, foot_z, logical_tx, logical_ty);
    if (progress < 0.0f) return terrain_height;
    return terrain_height + ((scene.water_terrain.surface_height_world - terrain_height) * progress);
}

bool isActualWaterTile(const SceneConfig& scene, int tx, int ty) {
    if (!scene.water_terrain.enabled || ty < 0 ||
        ty >= static_cast<int>(scene.water_terrain.actual_water_cells.size())) {
        return false;
    }
    const auto& row = scene.water_terrain.actual_water_cells[static_cast<std::size_t>(ty)];
    return tx >= 0 && tx < static_cast<int>(row.size()) && row[static_cast<std::size_t>(tx)] != 0;
}

float shorelineProgressAtWorldPosition(
    const SceneConfig& scene,
    float world_x,
    float world_z,
    int logical_tx,
    int logical_ty) {
    if (!scene.water_terrain.enabled || logical_ty < 0 ||
        logical_ty >= static_cast<int>(scene.water_terrain.shoreline_cells.size())) {
        return -1.0f;
    }
    const auto& shoreline_row = scene.water_terrain.shoreline_cells[static_cast<std::size_t>(logical_ty)];
    if (logical_tx < 0 || logical_tx >= static_cast<int>(shoreline_row.size()) ||
        shoreline_row[static_cast<std::size_t>(logical_tx)] == 0 ||
        logical_ty + 1 >= static_cast<int>(scene.water_terrain.shoreline_corner_progress.size())) {
        return -1.0f;
    }
    const auto& top = scene.water_terrain.shoreline_corner_progress[static_cast<std::size_t>(logical_ty)];
    const auto& bottom = scene.water_terrain.shoreline_corner_progress[static_cast<std::size_t>(logical_ty + 1)];
    if (logical_tx + 1 >= static_cast<int>(top.size()) || logical_tx + 1 >= static_cast<int>(bottom.size())) {
        return -1.0f;
    }
    const float tile_size = std::max(1.0f, scene.grid.tile_size);
    const float u = std::clamp((world_x / tile_size) - static_cast<float>(logical_tx), 0.0f, 1.0f);
    const float v = std::clamp((world_z / tile_size) - static_cast<float>(logical_ty), 0.0f, 1.0f);
    const float north = top[static_cast<std::size_t>(logical_tx)] +
        ((top[static_cast<std::size_t>(logical_tx + 1)] - top[static_cast<std::size_t>(logical_tx)]) * u);
    const float south = bottom[static_cast<std::size_t>(logical_tx)] +
        ((bottom[static_cast<std::size_t>(logical_tx + 1)] - bottom[static_cast<std::size_t>(logical_tx)]) * u);
    return std::clamp(north + ((south - north) * v), 0.0f, 1.0f);
}

} // namespace pr::gameplay::world3d::terrain
