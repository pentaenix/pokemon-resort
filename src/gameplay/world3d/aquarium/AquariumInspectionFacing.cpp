#include "gameplay/world3d/aquarium/AquariumInspectionFacing.hpp"

#include <utility>

namespace pr::gameplay::world3d::aquarium {

FacingDirection AquariumInspectionFacing::begin(FacingDirection current_facing) {
    if (!previous_facing_) previous_facing_ = current_facing;
    return FacingDirection::North;
}

std::optional<FacingDirection> AquariumInspectionFacing::end() {
    return std::exchange(previous_facing_, std::nullopt);
}

void AquariumInspectionFacing::clear() {
    previous_facing_.reset();
}

} // namespace pr::gameplay::world3d::aquarium
