#pragma once

#include "core/config/Json.hpp"
#include "gameplay/world3d/Overworld3DConfig.hpp"

#include <string>

namespace pr::gameplay::world3d::data {

SceneConfig parseSceneMetadata(
    const JsonValue& root,
    const std::string& project_root);

} // namespace pr::gameplay::world3d::data
