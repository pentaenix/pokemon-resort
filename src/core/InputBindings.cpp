#include "core/InputBindings.hpp"

namespace pr {

SDL_Keycode keycodeFromBinding(const std::string& binding) {
    if (binding == "UP") return SDLK_UP;
    if (binding == "DOWN") return SDLK_DOWN;
    if (binding == "LEFT") return SDLK_LEFT;
    if (binding == "RIGHT") return SDLK_RIGHT;
    if (binding == "RETURN" || binding == "ENTER") return SDLK_RETURN;
    if (binding == "SPACE") return SDLK_SPACE;
    if (binding == "ESCAPE" || binding == "ESC") return SDLK_ESCAPE;
    if (binding == "BACKSPACE") return SDLK_BACKSPACE;
    if (binding.size() == 1) {
        return SDL_GetKeyFromName(binding.c_str());
    }
    return SDL_GetKeyFromName(binding.c_str());
}

bool matchesBinding(SDL_Keycode key, const std::vector<std::string>& bindings) {
    for (const std::string& binding : bindings) {
        if (key == keycodeFromBinding(binding)) {
            return true;
        }
    }
    return false;
}

} // namespace pr
