#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"

#include <string>

namespace pr::gameplay::world3d::data {

bool isOwmapFile(const std::string& path);
SceneConfig loadOwmapScene(const std::string& project_root, const std::string& owmap_path);

} // namespace pr::gameplay::world3d::data

