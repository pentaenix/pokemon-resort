#include "gameplay/world3d/characters/SpriteSheetAnimator.hpp"

#include <algorithm>

namespace pr::gameplay::world3d::characters {

namespace {

int rowForFacing(const CharacterSpriteDefinition& def, FacingDirection facing) {
    switch (facing) {
        case FacingDirection::South: return def.row_south;
        case FacingDirection::West: return def.row_west;
        case FacingDirection::East: return def.row_east;
        case FacingDirection::North: return def.row_north;
    }
    return def.row_south;
}

} // namespace

SpriteSheetAnimator::SpriteSheetAnimator(const CharacterSpriteDefinition& def) : def_(def) {}

void SpriteSheetAnimator::setMoving(bool moving) {
    if (moving_ == moving) {
        return;
    }
    if (!moving_ && moving) {
        // Alternate walk start contact frame between stops so the lead leg flips.
        const std::size_t walk_count = def_.walk.frames.size();
        if (walk_count >= 4) {
            frame_index_ = alternate_walk_start_ ? 2U : 0U;
        } else if (walk_count >= 2) {
            frame_index_ = alternate_walk_start_ ? 1U : 0U;
        } else {
            frame_index_ = 0U;
        }
        alternate_walk_start_ = !alternate_walk_start_;
        elapsed_ms_ = 0.0;
    } else if (moving_ && !moving) {
        frame_index_ = 0U;
        elapsed_ms_ = 0.0;
    }
    moving_ = moving;
}

void SpriteSheetAnimator::setFacing(FacingDirection facing) {
    facing_ = facing;
}

void SpriteSheetAnimator::update(double dt) {
    const CharacterAnimationDef& anim = moving_ ? def_.walk : def_.idle;
    if (anim.frames.empty()) {
        return;
    }
    elapsed_ms_ += dt * 1000.0;
    while (elapsed_ms_ >= anim.frame_time_ms) {
        elapsed_ms_ -= anim.frame_time_ms;
        frame_index_ = (frame_index_ + 1U) % anim.frames.size();
    }
}

SDL_Rect SpriteSheetAnimator::sourceRect() const {
    const CharacterAnimationDef& anim = moving_ ? def_.walk : def_.idle;
    const int frame = anim.frames.empty() ? 0 : anim.frames[frame_index_ % anim.frames.size()];
    const int col = std::clamp(frame, 0, std::max(0, def_.columns - 1));
    const int row = std::clamp(rowForFacing(def_, facing_), 0, std::max(0, def_.rows - 1));
    return SDL_Rect{col * def_.frame_width, row * def_.frame_height, def_.frame_width, def_.frame_height};
}

} // namespace pr::gameplay::world3d::characters
