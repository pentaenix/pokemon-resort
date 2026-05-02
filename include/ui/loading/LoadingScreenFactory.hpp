#pragma once

#include "core/Types.hpp"
#include "ui/loading/LoadingScreenBase.hpp"

#include <SDL.h>

#include <memory>
#include <string>

namespace pr {

std::unique_ptr<LoadingScreenBase> createLoadingScreen(
    LoadingScreenType type,
    SDL_Renderer* renderer,
    const WindowConfig& window_config,
    const std::string& fallback_font_path,
    const std::string& project_root);

} // namespace pr
