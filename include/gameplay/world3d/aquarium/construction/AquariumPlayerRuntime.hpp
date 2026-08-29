#pragma once

#include "aquarium_geometry/Kernel.hpp"
#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/aquarium/AquariumConfig.hpp"
#include "gameplay/world3d/aquarium/AquariumSimulation.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumDesign.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::aquarium::construction {

struct PlayerTankRuntime {
    pr::aquarium::geometry::TankDesign design;
    pr::aquarium::geometry::AquariumBuildResult build;
    float world_center_x = 0.0f;
    float world_floor_y = 0.0f;
    float world_center_z = 0.0f;
};

struct AquariumPopulationContext {
    std::filesystem::path project_root;
    float model_scale = 0.27f;
    AquariumPokemonPresentationConfig presentation;
    std::string placeholder_model_path;
};

class AquariumPopulationPolicy {
public:
    virtual ~AquariumPopulationPolicy() = default;
    virtual std::vector<AquariumPokemonActor> populationFor(
        const PlayerTankRuntime& tank,
        const AquariumPopulationContext& context,
        std::vector<std::string>* diagnostics) const = 0;
};

std::unique_ptr<AquariumPopulationPolicy> makePlaceholderWishiwashiPolicy();

struct PlayerAquariumRuntimeSet {
    std::uint64_t revision = 0;
    std::vector<PlayerTankRuntime> tanks;
    std::vector<pr::aquarium::geometry::GridCell> collision_cells;
    std::vector<AquariumPokemonActor> actors;
};

PlayerAquariumRuntimeSet buildPlayerAquariumRuntime(
    const AquariumDesignDocument& document,
    const SceneConfig& scene,
    const AquariumMapConfig& map_config,
    const std::filesystem::path& project_root,
    const AquariumPopulationPolicy& population_policy,
    std::vector<std::string>* diagnostics = nullptr);

} // namespace pr::gameplay::world3d::aquarium::construction
