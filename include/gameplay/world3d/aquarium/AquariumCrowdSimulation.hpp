#pragma once

#include "gameplay/world3d/aquarium/AquariumNavigation.hpp"

#include <vector>

namespace pr::gameplay::world3d::aquarium {

// A conservative, animation-baked body used only for local aquarium crowd
// avoidance. Horizontal radius encloses every yaw; vertical radius encloses
// every sampled pose, so the solver does not depend on render geometry.
struct AquariumCrowdBody {
    Point3 center{};
    Point3 velocity{};
    float horizontal_radius = 0.1f;
    float vertical_radius = 0.1f;
    // Fraction of the maximum animation envelope treated as another animal's
    // living body. Fins, tails, and tentacles remain soft visual space.
    float body_scale = 0.68f;
};

struct AquariumCrowdCorrection {
    Point3 first_delta{};
    Point3 second_delta{};
    bool penetrating = false;
};

Point3 steerAquariumCrowd(
    const AquariumCrowdBody& self,
    const std::vector<AquariumCrowdBody>& neighbours,
    Point3 desired_direction,
    float body_gap_meters = 0.04f);

// Contact is allowed only when a body is already intersecting and the proposed
// step strictly improves its separation. This lets old/crowded saves recover
// instead of permanently pinning an actor in place.
bool aquariumCrowdMoveAllowed(
    const AquariumCrowdBody& self,
    Point3 candidate_center,
    const std::vector<AquariumCrowdBody>& neighbours,
    float body_gap_meters = 0.04f);

AquariumCrowdCorrection separateAquariumCrowdCores(
    const AquariumCrowdBody& first,
    const AquariumCrowdBody& second);

} // namespace pr::gameplay::world3d::aquarium
