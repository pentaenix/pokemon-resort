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

    void show(Target target);
    void hide();
    bool toggleForTarget(Target target);

private:
    bool active_ = false;
    Target target_{};
};

} // namespace pr::gameplay::world3d::dialogue
