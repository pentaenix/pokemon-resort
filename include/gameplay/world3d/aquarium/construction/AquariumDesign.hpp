#pragma once

#include "aquarium_geometry/Types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::aquarium::construction {

inline constexpr const char* kAquariumDesignSchema = "pokemon-resort-aquarium-design";

struct AquariumPopulationPolicyRef {
    std::string id = "placeholder-wishiwashi";
    std::uint32_t version = 1;
};

struct AquariumDesignDocument {
    std::string design_id;
    std::string map_id;
    std::uint64_t revision = 0;
    AquariumPopulationPolicyRef population_policy;
    std::vector<pr::aquarium::geometry::TankDesign> tanks;
};

enum class AquariumDesignLoadStatus {
    Loaded,
    Invalid,
    NewerVersion,
};

struct AquariumDesignLoadResult {
    AquariumDesignLoadStatus status = AquariumDesignLoadStatus::Invalid;
    std::optional<AquariumDesignDocument> document;
    std::vector<std::string> diagnostics;
};

AquariumDesignLoadResult parseAquariumDesign(const std::string& text);
std::string serializeAquariumDesignCanonical(const AquariumDesignDocument& document);
std::vector<std::string> validateAquariumDesign(const AquariumDesignDocument& document);

} // namespace pr::gameplay::world3d::aquarium::construction
