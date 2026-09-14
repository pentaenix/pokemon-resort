#pragma once

#include "gameplay/world3d/aquarium/AquariumNavigation.hpp"
#include "gameplay/world3d/aquarium/AquariumSpeciesCatalog.hpp"

namespace pr::gameplay::world3d::aquarium::construction {

enum class AquariumHabitatFitReason {
    Fits,
    MissingEnvelope,
    HorizontalClearance,
    VerticalClearance,
    TurningSpace,
};

struct AquariumHabitatFit {
    AquariumHabitatFitReason reason = AquariumHabitatFitReason::MissingEnvelope;
    Point3 preview_position{};
    float body_radius_meters = 0.0f;
    float body_height_meters = 0.0f;

    bool fits() const { return reason == AquariumHabitatFitReason::Fits; }
};

AquariumHabitatFit validateAquariumHabitat(
    const AquariumSpeciesEntry& species,
    const AquariumNavigation& navigation,
    float pokemon_model_scale);

} // namespace pr::gameplay::world3d::aquarium::construction
