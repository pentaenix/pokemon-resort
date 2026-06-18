#pragma once

#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"

#include <cstddef>
#include <limits>

namespace pr::gameplay::world3d::rendering {

enum class ActorOcclusionKind {
    ForegroundModel,
    ForegroundTerrainWall,
    ForegroundTile,
    Character,
    Texture,
};

struct ActorOcclusionItem {
    float sort_depth = std::numeric_limits<float>::max();
    float sprite_priority_bias = 0.0f;
    ActorOcclusionKind kind = ActorOcclusionKind::Character;
    std::size_t index = 0;
};

float actorOcclusionDepth(
    const camera::Gen4FollowCamera& camera,
    const camera::Vec3& anchor,
    int viewport_w,
    int viewport_h,
    float toward_camera_bias_world = 0.0f);

bool actorOccluderIsInFront(float occluder_depth, float actor_depth);

} // namespace pr::gameplay::world3d::rendering
