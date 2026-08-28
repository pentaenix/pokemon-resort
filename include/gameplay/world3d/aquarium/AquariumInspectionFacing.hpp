#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"

#include <optional>

namespace pr::gameplay::world3d::aquarium {

// Keeps the player's authored approach direction separate from the fixed
// presentation direction used while inspecting a tank.
class AquariumInspectionFacing {
public:
    FacingDirection begin(FacingDirection current_facing);
    std::optional<FacingDirection> end();
    void clear();

    bool active() const { return previous_facing_.has_value(); }

private:
    std::optional<FacingDirection> previous_facing_;
};

} // namespace pr::gameplay::world3d::aquarium
