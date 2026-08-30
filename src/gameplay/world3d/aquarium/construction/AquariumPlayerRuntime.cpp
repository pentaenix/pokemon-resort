#include "gameplay/world3d/aquarium/construction/AquariumPlayerRuntime.hpp"

#include "gameplay/attend/PokemonModelCatalog.hpp"

#include <algorithm>
#include <cctype>

namespace pr::gameplay::world3d::aquarium::construction {
namespace geo = pr::aquarium::geometry;

namespace {

std::string normalized(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return c == ' ' || c == '-' ? '_' : static_cast<char>(std::tolower(c));
    });
    return value;
}

class PlaceholderWishiwashiPolicy final : public AquariumPopulationPolicy {
public:
    std::vector<AquariumPokemonActor> populationFor(
        const PlayerTankRuntime& tank,
        const AquariumPopulationContext& context,
        std::vector<std::string>* diagnostics) const override {
        if (context.placeholder_model_path.empty()) {
            if (diagnostics) diagnostics->push_back("Placeholder Wishiwashi model was not found");
            return {};
        }
        AquariumPokemonActor actor;
        actor.id = tank.design.id + ":placeholder";
        actor.species = "wishiwashi";
        actor.form = "00";
        actor.model_path = context.placeholder_model_path;
        actor.animation = "idle_default";
        actor.model_scale = context.model_scale;
        actor.presentation = context.presentation;
        geo::Vec3 spawn{};
        if (!tank.build.navigation.suggested_spawns.empty()) {
            spawn = tank.build.navigation.suggested_spawns.front();
        }
        actor.world_position = {
            tank.world_center_x + spawn.x,
            tank.world_floor_y + spawn.y,
            tank.world_center_z + spawn.z,
        };
        return {std::move(actor)};
    }
};

} // namespace

std::unique_ptr<AquariumPopulationPolicy> makePlaceholderWishiwashiPolicy() {
    return std::make_unique<PlaceholderWishiwashiPolicy>();
}

PlayerAquariumRuntimeSet buildPlayerAquariumRuntime(
    const AquariumDesignDocument& document,
    const SceneConfig& scene,
    const AquariumMapConfig& map_config,
    const std::filesystem::path& project_root,
    const AquariumPopulationPolicy& population_policy,
    std::vector<std::string>* diagnostics) {
    PlayerAquariumRuntimeSet out;
    out.revision = document.revision;
    std::string wishiwashi_model_path;
    const auto models = gameplay::attend::discoverPokemonModels(
        project_root / "assets/pokemon_attend/pokemon_models");
    const auto wishiwashi = std::find_if(models.begin(), models.end(), [](const auto& model) {
        return model.id == normalized("wishiwashi");
    });
    if (wishiwashi != models.end()) wishiwashi_model_path = wishiwashi->path;
    for (const geo::TankDesign& design : document.tanks) {
        geo::AquariumBuildRequest request;
        request.tank = design;
        geo::AquariumBuildResult build = geo::buildAquarium(request);
        if (!build.validation.valid()) {
            if (diagnostics) diagnostics->push_back("Tank " + design.id + " failed kernel validation");
            continue;
        }
        PlayerTankRuntime runtime;
        runtime.design = design;
        runtime.world_center_x = geo::footprintCentreWorld(
            design.footprint.origin_cell.column, geo::occupiedWidthCells(design.footprint));
        runtime.world_center_z = geo::footprintCentreWorld(
            design.footprint.origin_cell.row, geo::occupiedDepthCells(design.footprint));
        const int sample_row = std::clamp(
            design.footprint.origin_cell.row, 0, std::max(0, scene.grid.height - 1));
        const int sample_column = std::clamp(
            design.footprint.origin_cell.column, 0, std::max(0, scene.grid.width - 1));
        if (sample_row < static_cast<int>(scene.terrain.heights.size()) &&
            sample_column < static_cast<int>(scene.terrain.heights[sample_row].size())) {
            runtime.world_floor_y = static_cast<float>(scene.terrain.heights[sample_row][sample_column]) *
                (scene.terrain.height_per_floor > 0.0f
                    ? scene.terrain.height_per_floor : scene.grid.tile_size);
        }
        runtime.build = std::move(build);
        out.collision_cells.insert(out.collision_cells.end(),
            runtime.build.collision.blocked_cells.begin(),
            runtime.build.collision.blocked_cells.end());
        AquariumPopulationContext context{
            project_root,
            map_config.pokemon_scale,
            map_config.pokemon_presentation,
            wishiwashi_model_path,
        };
        std::vector<AquariumPokemonActor> actors = population_policy.populationFor(
            runtime, context, diagnostics);
        out.actors.insert(out.actors.end(),
            std::make_move_iterator(actors.begin()), std::make_move_iterator(actors.end()));
        out.tanks.push_back(std::move(runtime));
    }
    return out;
}

} // namespace pr::gameplay::world3d::aquarium::construction
