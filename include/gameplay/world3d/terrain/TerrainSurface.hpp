#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"

namespace pr::gameplay::world3d::terrain {

struct TileCoord {
    int x = 0;
    int y = 0;
};

// Corner order matches OverworldMapRenderer / map-editor preview:
// 0 = (x0, z0) NW, 1 = (x1, z0) NE, 2 = (x1, z1) SE, 3 = (x0, z1) SW.
void fillTileCornerHeights(const SceneConfig& scene, int tx, int ty, float out_corners[4]);

bool isSlopeSpecial(int special);

float heightPerFloor(const SceneConfig& scene);

// Cardinal ramp 2=N, 3=E, 4=S, 5=W — step direction along or against ascend.
void rampAscendVector(int ramp_direction, int& out_dx, int& out_dy);

// Which tile owns height sampling for a smooth ramp step (matches traversal rules).
TileCoord resolveRampSampleTile(
    const SceneConfig& scene,
    int from_tx,
    int from_ty,
    int to_tx,
    int to_ty,
    int step_dx,
    int step_dy,
    int height_delta_units);

// Secondary sample tile for the second half of a step (when both endpoints are sloped).
TileCoord resolveRampSampleTileEnd(
    const SceneConfig& scene,
    int from_tx,
    int from_ty,
    int to_tx,
    int to_ty,
    int step_dx,
    int step_dy,
    int height_delta_units);

// Sample height using a specific tile's corner field (not floor(world / tile_size)).
float heightAtWorldPositionOnTile(
    const SceneConfig& scene,
    float world_x,
    float world_z,
    int sample_tx,
    int sample_ty,
    bool clamp_uv = true);

// Bilinear on primary tile; blends across shared edges when (u,v) leave [0,1].
float heightAtWorldPositionStitched(
    const SceneConfig& scene,
    float world_x,
    float world_z,
    int primary_tx,
    int primary_ty);

float heightAtWorldPosition(
    const SceneConfig& scene,
    float world_x,
    float world_z,
    int fallback_tx,
    int fallback_ty);

float heightAtTileCenter(const SceneConfig& scene, int tx, int ty);

// True when a step should follow ramp corner heights (not flat Y lerp).
bool isSmoothRampHeightStep(
    const SceneConfig& scene,
    int from_tx,
    int from_ty,
    int to_tx,
    int to_ty,
    int step_dx,
    int step_dy);

// True when neighboring tiles share a continuous solved surface edge.
bool canTraverseTerrainEdge(
    const SceneConfig& scene,
    int from_tx,
    int from_ty,
    int to_tx,
    int to_ty,
    int step_dx,
    int step_dy);

// Stitched height at a footprint; primary tile is the actor's logical grid cell.
float heightAtActorFeet(
    const SceneConfig& scene,
    float foot_x,
    float foot_z,
    int logical_tx,
    int logical_ty);

bool isActualWaterTile(const SceneConfig& scene, int tx, int ty);

// Normalized land (0) -> water (1) position inside an authored shoreline tile.
// Returns -1 when the position is not on shoreline terrain.
float shorelineProgressAtWorldPosition(
    const SceneConfig& scene,
    float world_x,
    float world_z,
    int logical_tx,
    int logical_ty);

} // namespace pr::gameplay::world3d::terrain
