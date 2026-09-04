#pragma once

#include "gameplay/world3d/interiors/DefaultRoom.hpp"

namespace pr::gameplay::world3d::rendering {

inline bool shouldRenderDefaultInteriorRoom(const SceneConfig& scene) {
    return interiors::shouldRenderDefaultRoom(scene);
}

inline bool defaultInteriorOpeningCovers(
    const SceneConfig& scene,
    std::string_view edge,
    int along) {
    return interiors::openingCovers(scene, edge, along);
}

inline float defaultInteriorWallHeightTiles(
    const SceneConfig& scene,
    std::string_view edge) {
    return interiors::wallHeightTiles(scene, edge);
}

inline float defaultInteriorOpeningHeightTiles(
    const SceneConfig& scene,
    std::string_view edge) {
    return interiors::openingHeightTiles(scene, edge);
}

} // namespace pr::gameplay::world3d::rendering
