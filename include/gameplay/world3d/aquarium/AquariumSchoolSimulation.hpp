#pragma once

#include "gameplay/world3d/aquarium/AquariumNavigation.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::aquarium {

struct AquariumSchoolMember {
    std::string id;
    Point3 position{};
    Point3 velocity{};
    float half_length = 0.1f;
    float half_height = 0.1f;
    float cruise_speed = 0.5f;
};

struct AquariumSchoolSteering {
    Point3 direction{};
    float speed = 0.0f;
};

// One reusable snapshot per species/form and tank. Spatial ordering bounds local
// neighbour searches; outputs retain input ordering. No actor owns the school.
struct AquariumSchoolState {
    Point3 destination{};
    Point3 heading{0,0,1};
    Point3 center{};
    float destination_seconds = 0.0f;
    float elapsed_seconds = 0.0f;
    float comfortable_radius = 0.5f;
    bool has_destination = false;
    std::vector<std::size_t> members;
    std::vector<AquariumSchoolMember> snapshot;
    std::vector<std::size_t> spatial_order;
    std::vector<AquariumSchoolSteering> steering;
};

std::uint32_t aquariumSchoolHash(const std::string& id);
void steerAquariumSchool(AquariumSchoolState& school, float dt);

} // namespace pr::gameplay::world3d::aquarium
