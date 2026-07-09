#include "gameplay/world3d/dialogue/OverworldTextboxController.hpp"

#include <utility>

namespace pr::gameplay::world3d::dialogue {

void OverworldTextboxController::show(Target target) {
    if (target.kind == TargetKind::None) {
        hide();
        return;
    }
    target_ = std::move(target);
    active_ = true;
}

void OverworldTextboxController::hide() {
    active_ = false;
    target_ = Target{};
}

bool OverworldTextboxController::toggleForTarget(Target target) {
    if (active_) {
        hide();
        return false;
    }
    show(std::move(target));
    return active_;
}

} // namespace pr::gameplay::world3d::dialogue
