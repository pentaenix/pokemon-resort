#include "gameplay/world3d/aquarium/construction/AquariumPlayerRuntime.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumHabitatValidator.hpp"
#include "gameplay/world3d/aquarium/AquariumExhibitPreset.hpp"
#include "gameplay/world3d/aquarium/AquariumSubstratePreset.hpp"

#include <algorithm>
#include <set>

namespace pr::gameplay::world3d::aquarium::construction {
namespace geo = pr::aquarium::geometry;

namespace {

AquariumNavigation navigationFor(const geo::NavigationVolumeSet& source);

class StockedAquariumPopulationPolicy final : public AquariumPopulationPolicy {
public:
    explicit StockedAquariumPopulationPolicy(AquariumSpeciesCatalog catalog)
        : catalog_(std::move(catalog)) {}

    std::vector<AquariumSwimmerDefinition> populationFor(
        const PlayerTankRuntime& tank,
        const AquariumPopulationContext& context,
        std::vector<std::string>* diagnostics) const override {
        if (!context.selected_population) return {};
        std::vector<AquariumSwimmerDefinition> population;
        const AquariumNavigation navigation = navigationFor(tank.build.navigation);
        for (const AquariumResidentSelection& resident : context.selected_population->residents) {
            const AquariumSpeciesEntry* species = catalog_.findApproved(resident.species_id);
            if (!species) {
                if (diagnostics) diagnostics->push_back(
                    "Stocked species is not approved: " + resident.species_id);
                continue;
            }
            const AquariumHabitatFit fit = validateAquariumHabitat(
                *species, navigation, context.model_scale);
            if (!fit.fits()) {
                if (diagnostics) diagnostics->push_back(
                    "Stocked species does not physically fit tank " + tank.design.id +
                    ": " + species->id);
                continue;
            }
            for (std::uint32_t copy = 0; copy < resident.count; ++copy) {
                AquariumSwimmerDefinition swimmer;
                swimmer.actor.id = tank.design.id + ':' + species->id + ':' + std::to_string(copy);
                swimmer.actor.species = species->species;
                swimmer.actor.form = species->form;
                const std::filesystem::path configured_model(species->model_path);
                swimmer.actor.model_path = configured_model.is_absolute()
                    ? configured_model.string()
                    : (context.project_root / configured_model).string();
                swimmer.actor.animation = species->animation;
                swimmer.actor.model_scale = context.model_scale * species->scale_multiplier;
                swimmer.actor.world_pitch_degrees = species->pitch_degrees;
                swimmer.actor.presentation = context.presentation;
                swimmer.movement.id = species->id;
                swimmer.movement.species = species->species;
                swimmer.movement.form = species->form;
                swimmer.movement.animation = species->animation;
                swimmer.movement.pitch_degrees = species->pitch_degrees;
                configureMovement(swimmer.movement, *species);
                swimmer.seed = stableSeed(swimmer.actor.id);
                swimmer.formation_index = static_cast<int>(copy);
                swimmer.formation_count = static_cast<int>(resident.count);
                population.push_back(std::move(swimmer));
            }
        }
        const AquariumSwimmerDefinition* host=nullptr;
        for(const auto& candidate:population) if(candidate.movement.continuous_cruise) {
            if(!host || (candidate.movement.habitat_tour && !host->movement.habitat_tour) ||
                (candidate.movement.habitat_tour==host->movement.habitat_tour && candidate.actor.id<host->actor.id))
                host=&candidate;
        }
        if(host) for(auto& follower:population) if(follower.movement.behavior=="escort") {
            follower.movement.follow_actor_id=host->actor.id;
            follower.movement.speed_meters_per_second=host->movement.speed_meters_per_second;
        }
        return population;
    }

private:
    static std::uint32_t stableSeed(const std::string& value) {
        std::uint32_t hash = 2166136261U;
        for (const unsigned char byte : value) {
            hash ^= byte;
            hash *= 16777619U;
        }
        return hash;
    }

    static void configureMovement(
        AquariumPokemonConfig& movement,
        const AquariumSpeciesEntry& species) {
        const std::string& profile = species.movement_profile;
        movement.continuous_cruise = profile == "large-cruiser";
        movement.habitat_tour = species.dex == 382;
        movement.behavior = profile == "schooling" ? "school"
            : profile == "escort" ? "escort"
            : profile == "timid-reef" ? "timid"
            : profile == "jelly-drift" ? "jelly"
            : profile == "anchored" || profile == "bottom-stationary" ? "stationary"
            : "wander";
        movement.speed_meters_per_second = movement.behavior == "stationary" ? 0.0f
            : profile == "timid-reef" ? 0.10f
            : profile == "jelly-drift" ? 0.11f
            : profile == "benthic-rest-swimmer" ? 0.38f
            : profile == "large-cruiser" ? 0.45f : 0.55f;
        movement.turn_degrees_per_second = profile == "timid-reef" ? 55.0f
            : profile == "jelly-drift" ? 32.0f
            : profile == "large-cruiser" ? 64.0f : 110.0f;
        if(species.dex==382) movement.speed_meters_per_second*=1.3f;
        movement.body_radius_meters = 0.04f;
        movement.vertical_movement_scale = species.vertical_zone == "bottom" ? 0.12f
            : species.vertical_zone == "surface" ? 0.08f : 0.34f;
        movement.swim_pitch_degrees = profile == "jelly-drift" || profile == "hover" ? 0.0f
            : profile == "large-cruiser" ? 10.0f : 6.0f;
        movement.motion_smoothing_seconds = profile == "jelly-drift" ? 0.72f
            : profile == "large-cruiser" ? 0.38f : 0.16f;
        movement.forward_only = profile != "jelly-drift" && profile != "hover" &&
            (profile == "large-cruiser" || species.travel_direction != "sideways");
        movement.vertical_anchor = species.vertical_zone == "bottom" ||
            profile == "benthic-rest-swimmer" ? "bottom" : "authored";
        const bool floor_actor = profile == "bottom-crawler" ||
            profile == "bottom-stationary" || profile == "bottom-burrower" ||
            profile == "anchored" || profile == "surface-walker" ||
            profile == "timid-reef";
        movement.movement_plane = floor_actor ? "floor" : "volume";
        movement.random_start = species.random_start;
        movement.idle_seconds_minimum = species.idle_seconds_minimum;
        movement.idle_seconds_maximum = species.idle_seconds_maximum;
        movement.local_move_distance_meters = species.local_move_distance_meters;
        movement.flee_radius_meters = species.flee_radius_meters;
        movement.flee_distance_meters = species.flee_distance_meters;
        movement.flee_speed_multiplier = species.flee_speed_multiplier;
        movement.threat_species = species.threat_species;
        movement.idle_animation = species.idle_animation;
        movement.idle_pitch_degrees = species.idle_pitch_degrees;
        movement.move_seconds_minimum = species.activity.move_seconds_minimum;
        movement.move_seconds_maximum = species.activity.move_seconds_maximum;
        movement.rest_seconds_minimum = species.activity.rest_seconds_minimum;
        movement.rest_seconds_maximum = species.activity.rest_seconds_maximum;
        movement.roaming_height_meters = species.activity.roaming_height_meters;
        movement.crowd_body_scale = species.activity.crowd_body_scale;
        movement.rest_at_bottom = species.activity.rest_at_bottom;
        if (species.physical_envelope.valid) {
            movement.has_baked_physical_envelope = true;
            movement.baked_physical_envelope = {
                species.physical_envelope.min_x, species.physical_envelope.max_x,
                species.physical_envelope.min_y, species.physical_envelope.max_y,
                species.physical_envelope.min_z, species.physical_envelope.max_z};
        }
    }

    AquariumSpeciesCatalog catalog_;
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

AquariumInspectionCameraConfig playerTankInspectionCamera() {
    AquariumInspectionCameraConfig camera;
    camera.has_framed_inspection_view = true;
    camera.inspection_behind_player_tiles = 11.0f;
    camera.inspection_front_height_tiles = 5.15f;
    camera.inspection_side_height_tiles = 4.43f;
    camera.smooth = 640.0f;
    camera.return_smooth = 2400.0f;
    camera.interaction_reach_tiles = 1.5f;
    camera.focused_standoff_tiles = 3.5f;
    camera.focused_front_height_tiles = 4.15f;
    camera.focused_side_height_tiles = 3.43f;
    camera.focused_front_pitch_degrees = -10.48f;
    camera.focused_side_pitch_degrees = -10.72f;
    camera.focused_near_clip = 12.0f;
    camera.focused_wall_clip_radius_tiles = 1.5f;
    return camera;
}

} // namespace

std::unique_ptr<AquariumPopulationPolicy> makeStockedAquariumPopulationPolicy(
    AquariumSpeciesCatalog catalog) {
    return std::make_unique<StockedAquariumPopulationPolicy>(std::move(catalog));
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
    for (std::size_t tank_index = 0; tank_index < document.tanks.size(); ++tank_index) {
        const geo::TankDesign& design = document.tanks[tank_index];
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
            aquariumTankPopulation(document, design.id),
        };
        AquariumPlayerTankSimulationInput simulation;
        simulation.tank_id = runtime.design.id;
        simulation.navigation = navigationFor(runtime.build.navigation);
        simulation.world_origin = {
            runtime.world_center_x,
            runtime.world_floor_y,
            runtime.world_center_z};
        simulation.inspection_camera = playerTankInspectionCamera();
        simulation.exhibit_preset_id = runtime.design.exhibit_preset;
        simulation.brightness_level = runtime.design.brightness_level;
        int maximum_radius_steps = runtime.design.corner_radius_steps;
        for (const auto& corner : runtime.design.corner_radii) {
            maximum_radius_steps = std::max(maximum_radius_steps, corner.radius_steps);
        }
        simulation.light_corner_radius_world = static_cast<float>(
            maximum_radius_steps * geo::kRadiusStepWorldUnits);
        for (const geo::Vec2 point : geo::footprintBoundaryLocalWorld(
                 runtime.design.footprint,
                 runtime.design.corner_radius_steps,
                 runtime.design.corner_radii)) {
            simulation.light_boundary_local_meters.push_back({
                point.x / static_cast<float>(geo::kWorldUnitsPerCell),
                point.y / static_cast<float>(geo::kWorldUnitsPerCell)});
        }
        simulation.swimmers = population_policy.populationFor(
            runtime, context, diagnostics);
        const AquariumExhibitPreset& exhibit = aquariumExhibitPreset(
            runtime.design.exhibit_preset);
        constexpr float kResidentInteriorBalance = 0.78f;
        const float tank_brightness = aquariumBrightnessMultiplier(
            runtime.design.brightness_level) * kResidentInteriorBalance;
        for (AquariumSwimmerDefinition& swimmer : simulation.swimmers) {
            swimmer.actor.presentation.brightness = std::clamp(
                swimmer.actor.presentation.brightness *
                    exhibit.pokemon_brightness_multiplier * tank_brightness,
                0.0f, 3.0f);
            for (std::size_t channel = 0; channel < 3U; ++channel) {
                swimmer.actor.presentation.tint[channel] = std::clamp(
                    swimmer.actor.presentation.tint[channel] *
                        exhibit.pokemon_tint[channel],
                    0.0f, 3.0f);
            }
        }
        if (context.selected_population) {
            std::size_t expected_population = 0;
            for (const auto& resident : context.selected_population->residents) {
                expected_population += resident.count;
            }
            if (simulation.swimmers.size() != expected_population) {
                out.population_valid = false;
            }
        }
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
