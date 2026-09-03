#include "gameplay/world3d/aquarium/construction/AquariumPlayerRuntime.hpp"

#include "gameplay/attend/PokemonModelCatalog.hpp"

#include <algorithm>
#include <cctype>
#include <set>

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
    std::vector<AquariumSwimmerDefinition> populationFor(
        const PlayerTankRuntime& tank,
        const AquariumPopulationContext& context,
        std::vector<std::string>* diagnostics) const override {
        std::vector<AquariumSwimmerDefinition> population;
        if (context.wishiwashi_model_path.empty()) {
            if (diagnostics) diagnostics->push_back(
                "Testing population Wishiwashi model was not found");
        } else {
            constexpr int kWishiwashiCount = 6;
            for (int copy = 0; copy < kWishiwashiCount; ++copy) {
                AquariumSwimmerDefinition swimmer = makeSwimmer(
                    tank, context, "wishiwashi", "00",
                    context.wishiwashi_model_path,
                    "wishiwashi:" + std::to_string(copy));
                swimmer.movement.id = tank.design.id + ":wishiwashi-school";
                swimmer.movement.behavior = "school";
                swimmer.movement.speed_meters_per_second = 0.34f;
                swimmer.movement.turn_degrees_per_second = 180.0f;
                swimmer.movement.body_radius_meters = 0.03f;
                swimmer.formation_index = copy;
                swimmer.formation_count = kWishiwashiCount;
                population.push_back(std::move(swimmer));
            }
        }

        if (context.clamperl_model_path.empty()) {
            if (diagnostics) diagnostics->push_back(
                "Testing population Clamperl model was not found");
        } else {
            AquariumSwimmerDefinition clamperl = makeSwimmer(
                tank, context, "clamperl", "",
                context.clamperl_model_path, "clamperl");
            clamperl.movement.behavior = "stationary";
            clamperl.movement.vertical_anchor = "bottom";
            clamperl.movement.speed_meters_per_second = 0.0f;
            clamperl.movement.body_radius_meters = 0.10f;
            clamperl.movement.has_starting_position = true;
            clamperl.movement.starting_position_meters = {-0.5f, 0.0f, -0.5f};
            population.push_back(std::move(clamperl));
        }

        if (context.pyukumuku_model_path.empty()) {
            if (diagnostics) diagnostics->push_back(
                "Testing population Pyukumuku model was not found");
        } else {
            AquariumSwimmerDefinition pyukumuku = makeSwimmer(
                tank, context, "pyukumuku", "",
                context.pyukumuku_model_path, "pyukumuku");
            pyukumuku.actor.animation = "walk";
            pyukumuku.movement.animation = "walk";
            pyukumuku.movement.behavior = "wander";
            pyukumuku.movement.movement_plane = "floor";
            pyukumuku.movement.vertical_anchor = "bottom";
            pyukumuku.movement.speed_meters_per_second = 0.28f;
            pyukumuku.movement.turn_degrees_per_second = 180.0f;
            pyukumuku.movement.body_radius_meters = 0.18f;
            pyukumuku.movement.has_starting_position = true;
            pyukumuku.movement.starting_position_meters = {0.5f, 0.0f, 0.5f};
            population.push_back(std::move(pyukumuku));
        }
        return population;
    }

private:
    static AquariumSwimmerDefinition makeSwimmer(
        const PlayerTankRuntime& tank,
        const AquariumPopulationContext& context,
        std::string species,
        std::string form,
        const std::string& model_path,
        const std::string& stable_suffix) {
        AquariumSwimmerDefinition swimmer;
        swimmer.actor.id = tank.design.id + ':' + stable_suffix;
        swimmer.actor.species = std::move(species);
        swimmer.actor.form = std::move(form);
        swimmer.actor.model_path = model_path;
        swimmer.actor.animation = "idle_default";
        swimmer.actor.model_scale = context.model_scale;
        swimmer.actor.presentation = context.presentation;
        swimmer.movement.id = swimmer.actor.id;
        swimmer.movement.species = swimmer.actor.species;
        swimmer.movement.form = swimmer.actor.form;
        swimmer.movement.animation = swimmer.actor.animation;
        std::uint32_t hash = 2166136261U;
        for (const unsigned char byte : swimmer.actor.id) {
            hash ^= byte;
            hash *= 16777619U;
        }
        swimmer.seed = hash;
        return swimmer;
    }
};

AquariumNavigation navigationFor(const geo::NavigationVolumeSet& source) {
    AquariumNavigation navigation;
    constexpr float kMetresPerWorldUnit = 1.0f / geo::kWorldUnitsPerCell;
    navigation.export_units_per_meter = geo::kWorldUnitsPerCell;
    navigation.floor_level_y = 0.0f;
    for (std::size_t index = 0; index < source.layers.size(); ++index) {
        const auto& source_layer = source.layers[index];
        SwimVolumeLayer layer;
        layer.id = "player-layer-" + std::to_string(index);
        layer.y_bottom = source_layer.floor_y * kMetresPerWorldUnit;
        layer.y_top = source_layer.ceiling_y * kMetresPerWorldUnit;
        PolygonWithHoles polygon;
        PolygonRing outer;
        outer.reserve(source_layer.area.outer.size());
        for (const geo::Vec2 point : source_layer.area.outer) {
            outer.push_back({
                point.x * kMetresPerWorldUnit,
                point.y * kMetresPerWorldUnit});
        }
        polygon.push_back(std::move(outer));
        for (const auto& source_hole : source_layer.area.holes) {
            PolygonRing hole;
            hole.reserve(source_hole.size());
            for (const geo::Vec2 point : source_hole) {
                hole.push_back({
                    point.x * kMetresPerWorldUnit,
                    point.y * kMetresPerWorldUnit});
            }
            polygon.push_back(std::move(hole));
        }
        if (layer.y_top > layer.y_bottom && !polygon.front().empty()) {
            layer.polygons.push_back(std::move(polygon));
            navigation.layers.push_back(std::move(layer));
        }
    }
    navigation.suggested_spawns.reserve(source.suggested_spawns.size());
    for (const geo::Vec3 spawn : source.suggested_spawns) {
        navigation.suggested_spawns.push_back({
            spawn.x * kMetresPerWorldUnit,
            spawn.y * kMetresPerWorldUnit,
            spawn.z * kMetresPerWorldUnit});
    }
    navigation.valid = !navigation.layers.empty();
    return navigation;
}

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
    std::set<std::pair<int, int>> collision_cells;
    const auto models = gameplay::attend::discoverPokemonModels(
        project_root / "assets/pokemon_attend/pokemon_models");
    const auto modelPath = [&](const std::string& species) {
        const auto found = std::find_if(models.begin(), models.end(), [&](const auto& model) {
            return model.id == normalized(species);
        });
        return found == models.end() ? std::string{} : found->path;
    };
    const std::string wishiwashi_model_path = modelPath("wishiwashi");
    const std::string clamperl_model_path = modelPath("clamperl");
    const std::string pyukumuku_model_path = modelPath("pyukumuku");
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
            design.footprint.origin_cell.column, geo::occupiedWidthCells(design.footprint)) +
            static_cast<float>(geo::kPlacementOffsetWorldUnits);
        runtime.world_center_z = geo::footprintCentreWorld(
            design.footprint.origin_cell.row, geo::occupiedDepthCells(design.footprint)) +
            static_cast<float>(geo::kPlacementOffsetWorldUnits);
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
        runtime.water_volume_litres = build.statistics.water_volume_litres;
        runtime.build = std::move(build);
        // Installed geometry is shifted by the canonical half-cell transform.
        // A source cell therefore overlaps its map cell plus the cells to its
        // east, south, and south-east; collision must conservatively cover all
        // four instead of stopping one cell short on the right/bottom edges.
        for (const auto cell : runtime.build.collision.blocked_cells) {
            collision_cells.emplace(cell.column, cell.row);
            collision_cells.emplace(cell.column + 1, cell.row);
            collision_cells.emplace(cell.column, cell.row + 1);
            collision_cells.emplace(cell.column + 1, cell.row + 1);
        }
        // Tunnel nodes use the ordinary walking-cell lattice, independently
        // of the tank footprint's half-cell installation transform. Clearing
        // exactly these cells preserves a single-cell-wide dry corridor.
        for (const auto cell : runtime.build.collision.dry_corridor_cells) {
            collision_cells.erase({cell.column, cell.row});
        }
        AquariumPopulationContext context{
            project_root,
            map_config.pokemon_scale,
            map_config.pokemon_presentation,
            wishiwashi_model_path,
            clamperl_model_path,
            pyukumuku_model_path,
        };
        AquariumPlayerTankSimulationInput simulation;
        simulation.tank_id = runtime.design.id;
        simulation.navigation = navigationFor(runtime.build.navigation);
        simulation.world_origin = {
            runtime.world_center_x,
            runtime.world_floor_y,
            runtime.world_center_z};
        simulation.swimmers = population_policy.populationFor(
            runtime, context, diagnostics);
        out.simulation_tanks.push_back(std::move(simulation));
        out.tanks.push_back(std::move(runtime));
    }
    out.collision_cells.reserve(collision_cells.size());
    for (const auto [column, row] : collision_cells) {
        out.collision_cells.push_back({column, row});
    }
    return out;
}

} // namespace pr::gameplay::world3d::aquarium::construction
