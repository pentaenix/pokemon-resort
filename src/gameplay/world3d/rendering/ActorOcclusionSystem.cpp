#include "gameplay/world3d/rendering/ActorOcclusionSystem.hpp"

#include <algorithm>

namespace pr::gameplay::world3d::rendering {

float actorOcclusionDepth(
    const camera::Gen4FollowCamera& camera,
    const camera::Vec3& anchor,
    int viewport_w,
    int viewport_h,
    float toward_camera_bias_world) {
    float sx = 0.0f;
    float sy = 0.0f;
    float depth = 0.0f;
    if (!camera.worldToScreen(anchor, std::max(1, viewport_w), std::max(1, viewport_h), sx, sy, depth)) {
        return std::numeric_limits<float>::max();
    }
    return depth - toward_camera_bias_world;
}

bool actorOccluderIsInFront(float occluder_depth, float actor_depth) {
    return occluder_depth < actor_depth;
}

} // namespace pr::gameplay::world3d::rendering
