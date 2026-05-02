#pragma once

#include <SDL_ttf.h>
#include <memory>
#include <string>

namespace pr {

using FontHandle = std::shared_ptr<TTF_Font>;
FontHandle loadFont(const std::string& configured_path, int pt_size, const std::string& project_root);

/// Prefer a system font with broad Unicode coverage (e.g. ♂/♀); falls back to `loadFont`.
FontHandle loadFontPreferringUnicode(const std::string& configured_path, int pt_size, const std::string& project_root);

} // namespace pr
