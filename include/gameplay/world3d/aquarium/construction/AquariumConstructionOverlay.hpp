#pragma once

#include "gameplay/world3d/aquarium/construction/AquariumConstructionSession.hpp"

#include <SDL.h>

#include <optional>

namespace pr::gameplay::world3d::aquarium::construction {

class AquariumConstructionOverlay {
public:
    void configure(const AquariumConstructionConfig& config);
    void render(SDL_Renderer* renderer, int logical_width, int logical_height,
        const AquariumConstructionSession& session) const;
    std::optional<pr::aquarium::geometry::GridCell> cellAt(
        int logical_x, int logical_y, int logical_width, int logical_height) const;

private:
    SDL_Rect gridRect(int logical_width, int logical_height) const;
    SDL_Rect cellRect(pr::aquarium::geometry::GridCell cell,
        int logical_width, int logical_height) const;

    AquariumConstructionConfig config_;
    int min_column_ = 0;
    int max_column_ = 0;
    int min_row_ = 0;
    int max_row_ = 0;
};

} // namespace pr::gameplay::world3d::aquarium::construction
