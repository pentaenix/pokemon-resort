#pragma once

#include <SDL.h>
#include <string>
#include <vector>

namespace pr {

SDL_Keycode keycodeFromBinding(const std::string& binding);
bool matchesBinding(SDL_Keycode key, const std::vector<std::string>& bindings);

} // namespace pr
