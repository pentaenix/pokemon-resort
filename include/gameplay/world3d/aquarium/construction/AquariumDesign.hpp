#pragma once

#include "aquarium_geometry/Types.hpp"
#include "gameplay/world3d/aquarium/decorations/AquariumDecoration.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::aquarium::construction {

inline constexpr const char* kAquariumDesignSchema = "pokemon-resort-aquarium-design";
inline constexpr int kAquariumDocumentSchemaVersion = 7;

struct AquariumPopulationPolicyRef {
    std::string id = "placeholder-wishiwashi";
    std::uint32_t version = 1;
};

struct AquariumResidentSelection {
    std::string species_id;
    std::uint32_t count = 1;
};

struct AquariumTankPopulation {
    std::string tank_id;
    std::vector<AquariumResidentSelection> residents;
};

struct AquariumDesignDocument {
    std::string design_id;
    std::string map_id;
    std::uint64_t revision = 0;
    AquariumPopulationPolicyRef population_policy;
    std::vector<pr::aquarium::geometry::TankDesign> tanks;
    std::vector<AquariumTankPopulation> tank_populations;
    // Tank-local data survives room rebasing; absent on older documents.
    std::vector<decorations::TankDecorations> tank_decorations;
    // Coordinates of this document's local frame in its room's stable space.
    // Absent in v1-v5 documents, where the frame was always (0,0).
    std::optional<pr::aquarium::geometry::GridCell> room_frame;
};

void rebaseAquariumDesign(AquariumDesignDocument&, int column, int row);

const AquariumTankPopulation* aquariumTankPopulation(
    const AquariumDesignDocument& document, const std::string& tank_id);

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
