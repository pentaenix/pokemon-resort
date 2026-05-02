#pragma once

#include "core/Types.hpp"

#include <filesystem>
#include <string>

namespace pr {

std::string findProjectRoot();
std::filesystem::path resolveSaveDirectory(
    const PersistenceConfig& persistence,
    const std::string& project_root);

} // namespace pr
