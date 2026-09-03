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
        if (context.placeholder_model_path.empty()) {
            if (diagnostics) diagnostics->push_back("Placeholder Wishiwashi model was not found");
            return {};
        }
        AquariumSwimmerDefinition swimmer;
        swimmer.actor.id = tank.design.id + ":placeholder";
        swimmer.actor.species = "wishiwashi";
        swimmer.actor.form = "00";
        swimmer.actor.model_path = context.placeholder_model_path;
        swimmer.actor.animation = "idle_default";
        swimmer.actor.model_scale = context.model_scale;
        swimmer.actor.presentation = context.presentation;
        swimmer.movement.id = swimmer.actor.id;
        swimmer.movement.species = swimmer.actor.species;
        swimmer.movement.form = swimmer.actor.form;
        swimmer.movement.animation = swimmer.actor.animation;
        swimmer.movement.behavior = "wander";
        swimmer.movement.speed_meters_per_second = 0.34f;
        swimmer.movement.turn_degrees_per_second = 180.0f;
        swimmer.movement.body_radius_meters = 0.03f;
        std::uint32_t hash = 2166136261U;
        for (const unsigned char byte : tank.design.id) {
            hash ^= byte;
            hash *= 16777619U;
        }
        swimmer.seed = hash;
        return {std::move(swimmer)};
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
