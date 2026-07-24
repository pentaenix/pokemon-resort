#pragma once

#include <string>

namespace pr::gameplay::world3d::dialogue {

class OverworldTextboxController {
public:
    enum class TargetKind {
        None,
        NpcActor,
        FollowerPokemon,
    };

    struct Target {
        TargetKind kind = TargetKind::None;
        std::string id;
    };

    bool active() const { return active_; }
    Target activeTarget() const { return target_; }
    const std::string& text() const { return text_; }

    void show(Target target, std::string text = {});
    void hide();
    bool toggleForTarget(Target target);

private:
    bool active_ = false;
    Target target_{};
    std::string text_;
};

} // namespace pr::gameplay::world3d::dialogue
