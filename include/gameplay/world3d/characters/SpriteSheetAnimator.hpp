#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"

#include <SDL.h>

namespace pr::gameplay::world3d::characters {

class SpriteSheetAnimator {
public:
    explicit SpriteSheetAnimator(const CharacterSpriteDefinition& def);

    void setMoving(bool moving);
    void setFacing(FacingDirection facing);
    void update(double dt);
    SDL_Rect sourceRect() const;

private:
    const CharacterSpriteDefinition def_;
    FacingDirection facing_ = FacingDirection::South;
    bool moving_ = false;
    std::size_t frame_index_ = 0;
    double elapsed_ms_ = 0.0;
    bool alternate_walk_start_ = false;
};

} // namespace pr::gameplay::world3d::characters
