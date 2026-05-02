#pragma once

#include "core/Types.hpp"
#include <string>

namespace pr {

AppConfig loadAppConfigFromJson(const std::string& path);
TitleScreenConfig loadConfigFromJson(const std::string& path);

} // namespace pr
