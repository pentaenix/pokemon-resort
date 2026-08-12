#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"

namespace pr::gameplay::world3d::rendering {

// A shell model is the complete authored floor/wall/entrance surface imported
// from RAE. Drawing procedural terrain beneath it causes coplanar depth fighting
// and leaks the fallback grid beyond irregular room boundaries.
inline bool shouldRenderFallbackTerrain(const SceneConfig& scene) {
    return scene.map_type != "interior" || scene.interior.shell_model_id.empty();
}

} // namespace pr::gameplay::world3d::rendering
