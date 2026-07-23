#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"

#include <SDL.h>

#include <string>

namespace pr::gameplay::world3d::characters {

class SpriteSheetAnimator {
public:
    explicit SpriteSheetAnimator(const CharacterSpriteDefinition& def);

    void setMoving(bool moving);
    void setRunning(bool running);
    void setSwimming(bool swimming, bool animate_while_idle = true);
    void setPlaybackSpeedMultiplier(double multiplier);
    void setFacing(FacingDirection facing);
    void update(double dt);
    SDL_Rect sourceRect() const;
    bool running() const { return running_; }
    bool swimming() const { return swimming_ && def_.has_swim; }
    bool hasActivitySession(const std::string& action_id) const;
    bool startActivitySession(const std::string& action_id);
    void cancelActivitySession();
    void requestActivityExit();
    bool activitySessionActive() const;
    bool activityStayActive() const;
    bool activityFinished() const;
    std::string activeActivityId() const;
    std::string textureSheetId() const;

private:
    enum class ActivityPhase {
        None,
        Enter,
        Stay,
        Exit,
        Finished,
    };

    const CharacterAnimationDef& activeAnimation() const;
    void resetFrameClock();

    const CharacterSpriteDefinition def_;
    FacingDirection facing_ = FacingDirection::South;
    bool moving_ = false;
    bool running_ = false;
    bool swimming_ = false;
    bool swim_animates_while_idle_ = true;
    std::size_t frame_index_ = 0;
    double elapsed_ms_ = 0.0;
    double playback_speed_multiplier_ = 1.0;
    bool alternate_walk_start_ = false;
    std::string activity_id_;
    ActivityPhase activity_phase_ = ActivityPhase::None;
};

} // namespace pr::gameplay::world3d::characters
