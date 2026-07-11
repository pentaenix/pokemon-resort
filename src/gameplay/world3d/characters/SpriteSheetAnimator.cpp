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

const CharacterAnimationDef& activeMoveAnimation(const CharacterSpriteDefinition& def, bool running) {
    return (running && def.has_run) ? def.run : def.walk;
}

void SpriteSheetAnimator::resetFrameClock() {
    frame_index_ = 0U;
    elapsed_ms_ = 0.0;
}

const CharacterAnimationDef& SpriteSheetAnimator::activeAnimation() const {
    if (activity_phase_ != ActivityPhase::None && activity_phase_ != ActivityPhase::Finished) {
        const auto it = def_.activity_sessions.find(activity_id_);
        if (it != def_.activity_sessions.end() && it->second.valid) {
            switch (activity_phase_) {
                case ActivityPhase::Enter: return it->second.enter;
                case ActivityPhase::Stay: return it->second.stay;
                case ActivityPhase::Exit: return it->second.exit;
                case ActivityPhase::None:
                case ActivityPhase::Finished:
                    break;
            }
        }
    }
    return moving_ ? activeMoveAnimation(def_, running_) : def_.idle;
}

void SpriteSheetAnimator::setMoving(bool moving) {
    if (moving_ == moving) {
        return;
    }
    if (!moving_ && moving) {
        // Alternate walk start contact frame between stops so the lead leg flips.
        const std::size_t walk_count = activeMoveAnimation(def_, running_).frames.size();
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
        resetFrameClock();
    }
    moving_ = moving;
}

void SpriteSheetAnimator::setRunning(bool running) {
    running = running && def_.has_run;
    if (running_ == running) {
        return;
    }
    running_ = running;
    resetFrameClock();
}

void SpriteSheetAnimator::setPlaybackSpeedMultiplier(double multiplier) {
    playback_speed_multiplier_ = std::clamp(multiplier, 0.1, 4.0);
}

void SpriteSheetAnimator::setFacing(FacingDirection facing) {
    facing_ = facing;
}

void SpriteSheetAnimator::update(double dt) {
    const CharacterAnimationDef& anim = activeAnimation();
    if (anim.frames.empty()) {
        return;
    }
    elapsed_ms_ += dt * playback_speed_multiplier_ * 1000.0;
    while (elapsed_ms_ >= anim.frame_time_ms) {
        elapsed_ms_ -= anim.frame_time_ms;
        if (activity_phase_ == ActivityPhase::Enter || activity_phase_ == ActivityPhase::Exit) {
            if (frame_index_ + 1U < anim.frames.size()) {
                ++frame_index_;
            } else if (activity_phase_ == ActivityPhase::Enter) {
                activity_phase_ = ActivityPhase::Stay;
                resetFrameClock();
            } else {
                activity_phase_ = ActivityPhase::Finished;
                activity_id_.clear();
                resetFrameClock();
            }
        } else {
            frame_index_ = (frame_index_ + 1U) % anim.frames.size();
        }
    }
}

SDL_Rect SpriteSheetAnimator::sourceRect() const {
    const CharacterAnimationDef& anim = activeAnimation();
    const int frame = anim.frames.empty() ? 0 : anim.frames[frame_index_ % anim.frames.size()];
    const int col = std::clamp(frame, 0, std::max(0, def_.columns - 1));
    const int row = std::clamp(rowForFacing(def_, facing_), 0, std::max(0, def_.rows - 1));
    return SDL_Rect{col * def_.frame_width, row * def_.frame_height, def_.frame_width, def_.frame_height};
}

bool SpriteSheetAnimator::hasActivitySession(const std::string& action_id) const {
    const auto it = def_.activity_sessions.find(action_id);
    return it != def_.activity_sessions.end() && it->second.valid;
}

bool SpriteSheetAnimator::startActivitySession(const std::string& action_id) {
    if (!hasActivitySession(action_id)) {
        return false;
    }
    activity_id_ = action_id;
    activity_phase_ = ActivityPhase::Enter;
    moving_ = false;
    running_ = false;
    resetFrameClock();
    return true;
}

void SpriteSheetAnimator::requestActivityExit() {
    if (activity_phase_ == ActivityPhase::Enter || activity_phase_ == ActivityPhase::Stay) {
        activity_phase_ = ActivityPhase::Exit;
        resetFrameClock();
    }
}

bool SpriteSheetAnimator::activitySessionActive() const {
    return activity_phase_ == ActivityPhase::Enter ||
        activity_phase_ == ActivityPhase::Stay ||
        activity_phase_ == ActivityPhase::Exit;
}

bool SpriteSheetAnimator::activityStayActive() const {
    return activity_phase_ == ActivityPhase::Stay;
}

bool SpriteSheetAnimator::activityFinished() const {
    return activity_phase_ == ActivityPhase::Finished || activity_phase_ == ActivityPhase::None;
}

std::string SpriteSheetAnimator::activeActivityId() const {
    return activitySessionActive() ? activity_id_ : std::string{};
}

} // namespace pr::gameplay::world3d::characters
