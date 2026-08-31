#pragma once

#include "gameplay/world3d/aquarium/construction/AquariumConstructionSession.hpp"
#include "core/assets/Assets.hpp"
#include "core/assets/Font.hpp"

#include <SDL.h>

#include <string>

namespace pr::gameplay::world3d::aquarium::construction {

enum class ConstructionHudAction;

class AquariumConstructionOverlay {
public:
    void configure(const AquariumConstructionConfig& config, std::string project_root);
    void render(SDL_Renderer* renderer, int logical_width, int logical_height,
        const AquariumConstructionSession& session,
        ConstructionHudAction focused_action) const;
private:
    std::string project_root_;
    mutable FontHandle font_;
    mutable std::string cached_hint_;
    mutable TextureHandle hint_texture_;
};

} // namespace pr::gameplay::world3d::aquarium::construction
