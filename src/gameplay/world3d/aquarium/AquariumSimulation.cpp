#include "gameplay/world3d/aquarium/AquariumSimulation.hpp"

#include "gameplay/attend/PokemonModelCatalog.hpp"
#include "gameplay/world3d/aquarium/AquariumCrowdSimulation.hpp"
#include "gameplay/world3d/aquarium/AquariumPokemonMetrics.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>

namespace pr::gameplay::world3d::aquarium {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kPlayerTankGlassComfortMeters = 0.12f;

std::string normalize(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        if (c == ' ' || c == '-') return '_';
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

float wrapDegrees(float degrees) {
    while (degrees > 180.0f) degrees -= 360.0f;
    while (degrees < -180.0f) degrees += 360.0f;
    return degrees;
}

AquariumPokemonMetrics pitchEnvelope(
    const AquariumPokemonMetrics& metrics,
    float base_pitch_degrees,
    float swim_pitch_degrees) {
    AquariumPokemonMetrics out = rotateAquariumPokemonMetrics(metrics, base_pitch_degrees);
    for (const float pitch : {
             base_pitch_degrees - swim_pitch_degrees,
             base_pitch_degrees + swim_pitch_degrees}) {
        const AquariumPokemonMetrics sample = rotateAquariumPokemonMetrics(metrics, pitch);
        out.min_x = std::min(out.min_x, sample.min_x);
        out.max_x = std::max(out.max_x, sample.max_x);
        out.min_y = std::min(out.min_y, sample.min_y);
        out.max_y = std::max(out.max_y, sample.max_y);
        out.min_z = std::min(out.min_z, sample.min_z);
        out.max_z = std::max(out.max_z, sample.max_z);
    }
    return out;
}

std::string resolvePokemonModel(
    const std::vector<gameplay::attend::PokemonModelCatalogEntry>& entries,
    const std::string& species) {
    const std::string wanted = normalize(species);
    const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
        return entry.id == wanted;
    });
    return found == entries.end() ? std::string{} : found->path;
}

Point3 fallbackSpawn(const AquariumNavigation& navigation) {
    if (!navigation.suggested_spawns.empty() &&
        containsPoint(navigation, navigation.suggested_spawns.front())) {
        return navigation.suggested_spawns.front();
    }
    const SwimVolumeLayer& layer = navigation.layers.front();
    const PolygonRing& ring = layer.polygons.front().front();
    Point3 center{0.0f, (layer.y_bottom + layer.y_top) * 0.5f, 0.0f};
    for (const Point2& point : ring) {
        center[0] += point[0];
        center[2] += point[1];
    }
    center[0] /= static_cast<float>(ring.size());
    center[2] /= static_cast<float>(ring.size());
    return center;
}

std::array<float, 4> horizontalBounds(const AquariumNavigation& navigation) {
    std::array<float, 4> bounds{
        std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest()};
    for (const SwimVolumeLayer& layer : navigation.layers) {
        for (const PolygonWithHoles& polygon : layer.polygons) {
            if (polygon.empty()) continue;
            for (const Point2& point : polygon.front()) {
                bounds[0] = std::min(bounds[0], point[0]);
                bounds[1] = std::max(bounds[1], point[0]);
                bounds[2] = std::min(bounds[2], point[1]);
                bounds[3] = std::max(bounds[3], point[1]);
            }
        }
    }
    return bounds;
}

std::array<float, 2> verticalBounds(const AquariumNavigation& navigation) {
    std::array<float, 2> bounds{
        std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest()};
    for (const SwimVolumeLayer& layer : navigation.layers) {
        bounds[0] = std::min(bounds[0], layer.y_bottom);
        bounds[1] = std::max(bounds[1], layer.y_top);
    }
    return bounds;
}

} // namespace

AquariumSimulation::AquariumSimulation(
    std::filesystem::path project_root,
    const SceneConfig& scene,
    const AquariumMapConfig* config)
    : project_root_(std::move(project_root)) {
    if (!config) return;
    // Catalog discovery can touch hundreds of Attend files. Do it once per map
    // construction, not once per configured species.
    const auto pokemon_models = gameplay::attend::discoverPokemonModels(
        project_root_ / "assets/pokemon_attend/pokemon_models");
    for (const AquariumTankConfig& tank_config : config->tanks) {
        const auto placement = std::find_if(scene.models.begin(), scene.models.end(), [&](const auto& model) {
            return model.id == tank_config.placement_id;
        });
        if (placement == scene.models.end()) {
            warnings_.push_back("Aquarium placement not found: " + tank_config.placement_id);
            continue;
        }
        std::string navigation_error;
        const std::filesystem::path navigation_path =
            std::filesystem::path(tank_config.navigation_path).is_absolute()
                ? std::filesystem::path(tank_config.navigation_path)
                : project_root_ / tank_config.navigation_path;
        AquariumNavigation navigation = loadAquariumNavigation(navigation_path.string(), &navigation_error);
        if (!navigation.valid) {
            warnings_.push_back("Aquarium navigation failed for " + tank_config.placement_id + ": " + navigation_error);
            continue;
        }
        TankTransform tank{placement->x, placement->y, placement->z, placement->yaw_deg, placement->scale};
        const float world_units_per_meter = navigation.export_units_per_meter * placement->scale;
        const auto bounds = horizontalBounds(navigation);
        const auto vertical = verticalBounds(navigation);
        tanks_.push_back(AquariumTankRuntime{
            tank_config.placement_id,
            toWorld(tank, navigation.export_units_per_meter, Point3{0.0f, 0.0f, 0.0f}),
            std::max(std::abs(bounds[0]), std::abs(bounds[1])) * world_units_per_meter,
            std::max(std::abs(bounds[2]), std::abs(bounds[3])) * world_units_per_meter,
            placement->yaw_deg,
            world_units_per_meter,
            placement->y,
            placement->y + vertical[0] * world_units_per_meter,
            placement->y + vertical[1] * world_units_per_meter,
            tank_config.inspection_camera});
        const TankSimulationContext tank_context{
            tank_config.placement_id, navigation, tank, bounds, vertical,
            world_units_per_meter};
        const auto append_pass = [&](bool followers) {
            std::uint32_t actor_seed = tank_config.seed;
            for (const AquariumPokemonConfig& pokemon : tank_config.pokemon) {
                const std::string model_path = resolvePokemonModel(
                    pokemon_models, pokemon.species);
                for (int copy = 0; copy < pokemon.count; ++copy) {
                    const std::uint32_t seed = actor_seed++;
                    if ((!pokemon.follow_actor_id.empty()) != followers) continue;
                    if (model_path.empty()) {
                        if (copy == 0) warnings_.push_back(
                            "Aquarium Pokemon model not found by name: " + pokemon.species);
                        continue;
                    }
                    AquariumPokemonActor actor;
                    actor.id = pokemon.id.empty()
                        ? tank_config.placement_id + ":" + normalize(pokemon.species) +
                            ":" + std::to_string(copy)
                        : pokemon.id + (pokemon.count > 1
                            ? ":" + std::to_string(copy) : "");
                    actor.species = pokemon.species;
                    actor.form = pokemon.form;
                    actor.model_path = model_path;
                    actor.animation = pokemon.animation;
                    actor.model_scale = config->pokemon_scale * pokemon.size_multiplier;
                    actor.world_pitch_degrees = pokemon.pitch_degrees;
                    actor.presentation = config->pokemon_presentation;
                    appendSwimmer(std::move(actor), pokemon, tank_context,
                        seed, copy, pokemon.count, false);
                }
            }
        };
        append_pass(false);
        append_pass(true);
    }
    authored_tank_count_ = tanks_.size();
    authored_warning_count_ = warnings_.size();
}

void AquariumSimulation::replacePlayerTanks(
    const std::vector<AquariumPlayerTankSimulationInput>& inputs) {
    body_navigation_cache_.clear();
    schools_.clear();
    swimmers_.erase(std::remove_if(swimmers_.begin(), swimmers_.end(),
        [](const Swimmer& swimmer) { return swimmer.player_built; }), swimmers_.end());
    actors_.clear();
    actors_.reserve(swimmers_.size());
    for (std::size_t index = 0; index < swimmers_.size(); ++index) {
        swimmers_[index].actor_index = index;
        actors_.push_back(swimmers_[index].actor);
    }
    tanks_.resize(std::min(authored_tank_count_, tanks_.size()));
    warnings_.resize(std::min(authored_warning_count_, warnings_.size()));

    for (const AquariumPlayerTankSimulationInput& input : inputs) {
        if (!input.navigation.valid || input.navigation.layers.empty()) {
            warnings_.push_back(
                "Player aquarium navigation is invalid for " + input.tank_id);
            continue;
        }
        const auto bounds = horizontalBounds(input.navigation);
        const auto vertical = verticalBounds(input.navigation);
        const float world_units_per_meter = input.navigation.export_units_per_meter;
        const TankTransform tank{
            input.world_origin[0], input.world_origin[1], input.world_origin[2],
            input.yaw_degrees, 1.0f};
        const TankSimulationContext tank_context{
            input.tank_id, input.navigation, tank, bounds, vertical,
            world_units_per_meter};
        tanks_.push_back(AquariumTankRuntime{
            input.tank_id,
            toWorld(tank, input.navigation.export_units_per_meter, Point3{}),
            std::max(std::abs(bounds[0]), std::abs(bounds[1])) * world_units_per_meter,
            std::max(std::abs(bounds[2]), std::abs(bounds[3])) * world_units_per_meter,
            input.yaw_degrees,
            world_units_per_meter,
            input.world_origin[1],
            input.world_origin[1] + vertical[0] * world_units_per_meter,
            input.world_origin[1] + vertical[1] * world_units_per_meter,
            input.inspection_camera,
            input.light_corner_radius_world,
            input.light_boundary_local_meters,
            input.exhibit_preset_id,
            input.brightness_level});

        std::vector<const AquariumSwimmerDefinition*> ordered;
        for (const auto& definition : input.swimmers) ordered.push_back(&definition);
        std::sort(ordered.begin(), ordered.end(), [](auto a, auto b) { return a->actor.id < b->actor.id; });
        for (const bool followers : {false, true}) {
            for (const auto* entry : ordered) {
                const auto& definition = *entry;
                if ((!definition.movement.follow_actor_id.empty()) != followers) continue;
                if (definition.actor.model_path.empty()) {
                    warnings_.push_back(
                        "Player aquarium Pokemon model is missing for " + input.tank_id);
                    continue;
                }
                appendSwimmer(definition.actor, definition.movement, tank_context,
                    definition.seed, definition.formation_index,
                    definition.formation_count, true);
            }
        }
    }
}

bool AquariumSimulation::appendSwimmer(
    AquariumPokemonActor actor,
    const AquariumPokemonConfig& movement,
    const TankSimulationContext& tank,
    std::uint32_t seed,
    int copy,
    int count,
    bool player_built) {
    Swimmer swimmer;
    swimmer.actor_index = actors_.size();
    swimmer.actor = std::move(actor);
    swimmer.navigation = tank.navigation;
    swimmer.tank = tank.transform;
    swimmer.tank_id = tank.id;
    swimmer.speed = movement.speed_meters_per_second;
    swimmer.turn_speed = movement.turn_degrees_per_second;
    swimmer.vertical_movement_scale = movement.vertical_movement_scale;
    swimmer.base_pitch_degrees = swimmer.actor.world_pitch_degrees;
    swimmer.swim_pitch_degrees = movement.swim_pitch_degrees;
    swimmer.motion_smoothing_seconds = movement.motion_smoothing_seconds;
    swimmer.pitch_turn_speed = movement.pitch_turn_degrees_per_second;
    swimmer.forward_only = movement.forward_only;
    swimmer.continuous_cruise = movement.continuous_cruise;
    swimmer.habitat_tour = movement.habitat_tour;
    swimmer.follow_actor_id = movement.follow_actor_id;
    swimmer.follow_distance = movement.follow_distance_meters;
    swimmer.follow_vertical_gap = movement.follow_vertical_gap_meters;
    swimmer.floor_navigation = normalize(movement.movement_plane) == "floor";
    if (!swimmer.floor_navigation) swimmer.convex_water = AquariumConvexWater::compile(tank.navigation);
    swimmer.player_built = player_built;
    swimmer.random_start = movement.random_start;
    swimmer.idle_seconds_minimum = movement.idle_seconds_minimum;
    swimmer.idle_seconds_maximum = movement.idle_seconds_maximum;
    swimmer.local_move_distance = movement.local_move_distance_meters;
    swimmer.flee_radius = movement.flee_radius_meters;
    swimmer.flee_distance = movement.flee_distance_meters;
    swimmer.flee_speed_multiplier = movement.flee_speed_multiplier;
    swimmer.threat_species = movement.threat_species;
    swimmer.crowd_body_scale = std::clamp(movement.crowd_body_scale, 0.35f, 1.0f);
    swimmer.movement_animation = swimmer.actor.animation;
    swimmer.idle_animation = movement.idle_animation.empty()
        ? swimmer.actor.animation : movement.idle_animation;
    swimmer.idle_pitch_degrees = movement.idle_pitch_degrees;
    swimmer.move_seconds_minimum = movement.move_seconds_minimum;
    swimmer.move_seconds_maximum = movement.move_seconds_maximum;
    swimmer.rest_seconds_minimum = movement.rest_seconds_minimum;
    swimmer.rest_seconds_maximum = movement.rest_seconds_maximum;
    swimmer.roaming_height = movement.roaming_height_meters;
    swimmer.rest_at_bottom = movement.rest_at_bottom;

    std::string metrics_error;
    AquariumPokemonMetrics metrics;
    if (movement.has_baked_physical_envelope) {
        metrics.min_x = movement.baked_physical_envelope[0];
        metrics.max_x = movement.baked_physical_envelope[1];
        metrics.min_y = movement.baked_physical_envelope[2];
        metrics.max_y = movement.baked_physical_envelope[3];
        metrics.min_z = movement.baked_physical_envelope[4];
        metrics.max_z = movement.baked_physical_envelope[5];
        metrics.valid = metrics.min_x < metrics.max_x && metrics.min_y < metrics.max_y &&
            metrics.min_z < metrics.max_z;
    } else {
        metrics = measureAquariumPokemon(
            swimmer.actor.model_path, swimmer.actor.form, &metrics_error);
    }
    if (metrics.valid) {
        const AquariumPokemonMetrics oriented_metrics = pitchEnvelope(
            metrics,
            movement.has_baked_physical_envelope ? 0.0f : swimmer.base_pitch_degrees,
            swimmer.swim_pitch_degrees);
        const float model_units_to_meters =
            swimmer.actor.model_scale / tank.world_units_per_meter;
        const float model_radius = std::max({
            std::abs(oriented_metrics.min_x), std::abs(oriented_metrics.max_x),
            std::abs(oriented_metrics.min_z), std::abs(oriented_metrics.max_z)}) *
            model_units_to_meters;
        // Shallow-pool actors navigate by a contact footprint. A face-up model
        // may deliberately overhang it, unlike a freely swimming body.
        // Authored shallow pools may intentionally let a posed model overhang
        // its small contact footprint. Player-built tanks have no such bespoke
        // staging, so every swimmer—including floor crawlers—uses its complete
        // measured horizontal envelope plus a small visual comfort band.
        swimmer.radius = swimmer.floor_navigation && !swimmer.player_built
            ? movement.body_radius_meters
            : std::max(movement.body_radius_meters, model_radius) +
                (swimmer.player_built ? kPlayerTankGlassComfortMeters : 0.0f);
        swimmer.lower_extent = oriented_metrics.min_y * model_units_to_meters;
        swimmer.upper_extent = oriented_metrics.max_y * model_units_to_meters;
        swimmer.body_half_width = std::max(
            0.01f, (oriented_metrics.max_x - oriented_metrics.min_x) *
                model_units_to_meters * 0.5f);
        swimmer.body_half_height = std::max(
            0.01f, (oriented_metrics.max_y - oriented_metrics.min_y) *
                model_units_to_meters * 0.5f);
        swimmer.body_half_length = std::max(
            0.01f, (oriented_metrics.max_z - oriented_metrics.min_z) *
                model_units_to_meters * 0.5f);
    } else {
        swimmer.radius = movement.body_radius_meters +
            (swimmer.player_built ? kPlayerTankGlassComfortMeters : 0.0f);
        swimmer.body_half_width = swimmer.radius;
        swimmer.body_half_height = swimmer.radius;
        swimmer.body_half_length = swimmer.radius;
        warnings_.push_back(
            "Aquarium Pokemon bounds unavailable for " + swimmer.actor.species +
            ": " + metrics_error);
    }

    const std::string behavior = normalize(movement.behavior);
    if (behavior == "escort" && !swimmer.follow_actor_id.empty()) {
        swimmer.behavior = Swimmer::Behavior::Escort;
    } else if (behavior == "school" || behavior == "escort") {
        swimmer.behavior = Swimmer::Behavior::School;
    } else if (behavior == "timid") {
        swimmer.behavior = Swimmer::Behavior::Timid;
    } else if (behavior == "jelly") {
        swimmer.behavior = Swimmer::Behavior::Jelly;
    } else if (behavior == "stationary" || swimmer.speed <= 0.0f) {
        swimmer.behavior = Swimmer::Behavior::Stationary;
    } else {
        swimmer.behavior = Swimmer::Behavior::Wander;
    }
    swimmer.school_id = tank.id + ":" + normalize(swimmer.actor.species) + ":" + swimmer.actor.form;
    swimmer.school_phase = count > 0
        ? (2.0f * kPi * static_cast<float>(copy) / static_cast<float>(count))
        : 0.0f;
    if (swimmer.behavior == Swimmer::Behavior::School) {
        const auto phase = aquariumSchoolHash(swimmer.actor.id);
        swimmer.actor.animation_time_seconds = double(phase % 1009U) / 1009.0;
        swimmer.actor.animation_playback_rate = 0.94f + float(phase % 13U) * 0.01f;
        swimmer.motion_smoothing_seconds = std::max(0.3f, swimmer.motion_smoothing_seconds);
    } else if (swimmer.behavior == Swimmer::Behavior::Escort && count > 1) {
        // A populated room should already look active on its first frame.
        // Stable phase/rate offsets avoid a newly spawned synchronized school.
        swimmer.actor.animation_time_seconds =
            std::fmod(static_cast<double>(copy) * 0.271828, 1.0);
        swimmer.actor.animation_playback_rate =
            0.94f + static_cast<float>((copy * 3) % 7) * 0.02f;
    } else if (swimmer.behavior == Swimmer::Behavior::Jelly) {
        // Independently stocked jellyfish should not pulse as one synchronized
        // wall. Seed-stable offsets preserve deterministic room entry.
        swimmer.actor.animation_time_seconds =
            static_cast<double>(seed % 997U) / 997.0;
        swimmer.actor.animation_playback_rate =
            0.90f + static_cast<float>(seed % 9U) * 0.015f;
    }
    swimmer.volume_center = {
        (tank.horizontal_bounds[0] + tank.horizontal_bounds[1]) * 0.5f,
        (tank.vertical_bounds[0] + tank.vertical_bounds[1]) * 0.5f,
        (tank.horizontal_bounds[2] + tank.horizontal_bounds[3]) * 0.5f};
    swimmer.volume_half_extent = {
        (tank.horizontal_bounds[1] - tank.horizontal_bounds[0]) * 0.5f,
        (tank.vertical_bounds[1] - tank.vertical_bounds[0]) * 0.5f,
        (tank.horizontal_bounds[3] - tank.horizontal_bounds[2]) * 0.5f};
    if (swimmer.floor_navigation && !swimmer.navigation.layers.empty()) {
        const auto lowest_layer = std::min_element(
            swimmer.navigation.layers.begin(), swimmer.navigation.layers.end(),
            [](const SwimVolumeLayer& lhs, const SwimVolumeLayer& rhs) {
                return lhs.y_bottom < rhs.y_bottom;
            });
        swimmer.floor_navigation_y =
            (lowest_layer->y_bottom + lowest_layer->y_top) * 0.5f;
    }
    swimmer.rng.seed(seed);
    const std::optional<Point3> starting_position =
        resolveStartingPosition(swimmer, movement, copy, count);
    if (!starting_position) {
        warnings_.push_back(
            "Aquarium Pokemon " + swimmer.actor.species + " has no separated "
            "spawn inside " + tank.id + "; actor was not spawned");
        return false;
    }
    swimmer.local_position = *starting_position;
    swimmer.jelly_anchor = swimmer.local_position;
    swimmer.jelly_next_ascending = (copy % 2) == 0;
    const auto initial_companion = std::find_if(swimmers_.begin(), swimmers_.end(),
        [&](const Swimmer& other) {
            if (other.tank_id != swimmer.tank_id) return false;
            if (swimmer.behavior == Swimmer::Behavior::Escort) {
                return other.actor.id == swimmer.follow_actor_id;
            }
            return swimmer.behavior == Swimmer::Behavior::School &&
                other.school_id == swimmer.school_id;
        });
    if (initial_companion != swimmers_.end()) {
        swimmer.actor.world_yaw_degrees =
            initial_companion->actor.world_yaw_degrees;
    } else {
        std::uniform_real_distribution<float> heading_pick(-180.0f, 180.0f);
        swimmer.actor.world_yaw_degrees = heading_pick(swimmer.rng) +
            swimmer.tank.yaw_degrees;
    }
    const bool intermittent = swimmer.move_seconds_maximum > 0.0f &&
        swimmer.rest_seconds_maximum > 0.0f &&
        swimmer.behavior == Swimmer::Behavior::Wander;
    if (intermittent) {
        if (seed % 4U == 0U) beginActivityMove(swimmer);
        else beginActivityRest(swimmer);
    } else if (swimmer.behavior == Swimmer::Behavior::Wander) {
        chooseTarget(swimmer);
    }
    if (swimmer.behavior == Swimmer::Behavior::Jelly) chooseJellyTarget(swimmer);
    if (swimmer.behavior == Swimmer::Behavior::Timid) {
        std::uniform_real_distribution<float> idle_pick(
            swimmer.idle_seconds_minimum, swimmer.idle_seconds_maximum);
        swimmer.behavior_timer_seconds = idle_pick(swimmer.rng);
        swimmer.local_target = swimmer.local_position;
    }
    swimmer.previous_local_position = swimmer.local_position;
    swimmer.previous_world_yaw_degrees = swimmer.actor.world_yaw_degrees;
    swimmer.previous_world_pitch_degrees = swimmer.actor.world_pitch_degrees;
    syncActor(swimmer);
    actors_.push_back(swimmer.actor);
    swimmers_.push_back(std::move(swimmer));
    return true;
}

AquariumBodyClearance AquariumSimulation::bodyClearance(const Swimmer& swimmer) const {
    return {swimmer.radius, swimmer.lower_extent, swimmer.upper_extent,
        swimmer.floor_navigation, swimmer.floor_navigation_y, swimmer.local_position[1]};
}

bool AquariumSimulation::containsBody(const Swimmer& swimmer, Point3 origin) const {
    if (!swimmer.convex_water.planes.empty())
        return swimmer.convex_water.contains(bodyClearance(swimmer), origin);
    return containsAquariumBody(swimmer.navigation, bodyClearance(swimmer), origin);
}

bool AquariumSimulation::initialSpawnAvailable(
    const Swimmer& swimmer, Point3 origin) const {
    constexpr float kSpawnGapMeters = 0.04f;
    const Point3 center{origin[0],
        origin[1] + (swimmer.lower_extent + swimmer.upper_extent) * 0.5f,
        origin[2]};
    for (const Swimmer& other : swimmers_) {
        if (other.tank_id != swimmer.tank_id) continue;
        const Point3 other_center{other.local_position[0],
            other.local_position[1] +
                (other.lower_extent + other.upper_extent) * 0.5f,
            other.local_position[2]};
        const std::array<float, 3> clearance{
            swimmer.body_half_width + other.body_half_width + kSpawnGapMeters,
            swimmer.body_half_height + other.body_half_height + kSpawnGapMeters,
            swimmer.body_half_length + other.body_half_length + kSpawnGapMeters};
        float normalized_distance_squared = 0.0f;
        for (int axis = 0; axis < 3; ++axis) {
            const float normalized =
                (center[axis] - other_center[axis]) / clearance[axis];
            normalized_distance_squared += normalized * normalized;
        }
        // Ellipsoid envelopes allow legitimate diagonal formations while
        // still forbidding any intersection between sampled model bounds.
        if (normalized_distance_squared < 1.0f) return false;
    }
    return true;
}

Point3 AquariumSimulation::crowdAdjustedDirection(
    const Swimmer& swimmer, Point3 direction) {
    const auto crowd_body = [](const Swimmer& value, Point3 origin) {
        return AquariumCrowdBody{
            {origin[0], origin[1] +
                (value.lower_extent + value.upper_extent) * 0.5f, origin[2]},
            value.velocity,
            std::max(value.body_half_width, value.body_half_length),
            value.body_half_height,
            value.crowd_body_scale};
    };
    const AquariumCrowdBody self = crowd_body(swimmer, swimmer.local_position);
    crowd_neighbours_scratch_.clear();
    crowd_neighbours_scratch_.reserve(swimmers_.size());
    for (const Swimmer& other : swimmers_) {
        if (&other == &swimmer || other.tank_id != swimmer.tank_id) continue;
        // Escorts yield to their host, never steer or stop the host itself.
        if (other.behavior == Swimmer::Behavior::Escort && other.follow_actor_id == swimmer.actor.id) continue;
        if (swimmer.school_grouped && other.school_grouped && other.school_id == swimmer.school_id) continue;
        auto body = crowd_body(other, swimmer.school_grouped ? other.previous_local_position : other.local_position);
        if (swimmer.school_grouped) body.velocity = other.snapshot_velocity;
        crowd_neighbours_scratch_.push_back(body);
    }
    return steerAquariumCrowd(self, crowd_neighbours_scratch_, direction);
}

bool AquariumSimulation::crowdAllowsMove(
    const Swimmer& swimmer, Point3 candidate) {
    const auto crowd_body = [](const Swimmer& value, Point3 origin) {
        return AquariumCrowdBody{
            {origin[0], origin[1] +
                (value.lower_extent + value.upper_extent) * 0.5f, origin[2]},
            value.velocity,
            std::max(value.body_half_width, value.body_half_length),
            value.body_half_height,
            value.crowd_body_scale};
    };
    const AquariumCrowdBody self = crowd_body(swimmer, swimmer.local_position);
    crowd_neighbours_scratch_.clear();
    crowd_neighbours_scratch_.reserve(swimmers_.size());
    for (const Swimmer& other : swimmers_) {
        if (&other == &swimmer || other.tank_id != swimmer.tank_id) continue;
        if (other.behavior == Swimmer::Behavior::Escort && other.follow_actor_id == swimmer.actor.id) continue;
        const auto origin = swimmer.school_grouped ? other.previous_local_position : other.local_position;
        const float reach = self.horizontal_radius + std::max(other.body_half_width, other.body_half_length) + 0.04f;
        if (std::abs(candidate[0] - origin[0]) > reach || std::abs(candidate[2] - origin[2]) > reach) continue;
        auto body = crowd_body(other, origin);
        if (swimmer.school_grouped) body.velocity = other.snapshot_velocity;
        crowd_neighbours_scratch_.push_back(body);
    }
    const Point3 candidate_center{candidate[0], candidate[1] +
        (swimmer.lower_extent + swimmer.upper_extent) * 0.5f, candidate[2]};
    return aquariumCrowdMoveAllowed(
        self, candidate_center, crowd_neighbours_scratch_);
}

bool AquariumSimulation::shouldRetargetBlockedMovement(
    Swimmer& swimmer, float dt) {
    constexpr float kStableAvoidanceSeconds = 2.0f;
    swimmer.movement_blocked_seconds += dt;
    if (swimmer.movement_blocked_seconds < kStableAvoidanceSeconds) return false;
    swimmer.movement_blocked_seconds = 0.0f;
    swimmer.motion_direction_initialized = false;
    return true;
}

bool AquariumSimulation::segmentNavigable(
    const Swimmer& swimmer, Point3 from, Point3 to) const {
    if (!swimmer.convex_water.planes.empty()) return containsBody(swimmer, from) && containsBody(swimmer, to);
    return aquariumBodySegmentNavigable(swimmer.navigation, bodyClearance(swimmer), from, to);
}

std::optional<Point3> AquariumSimulation::resolveStartingPosition(
    Swimmer& swimmer,
    const AquariumPokemonConfig& config,
    int copy,
    int count) {
    Point3 candidate = config.has_starting_position
        ? config.starting_position_meters
        : fallbackSpawn(swimmer.navigation);
    const auto vertical = verticalBounds(swimmer.navigation);
    const std::string vertical_anchor = normalize(config.vertical_anchor);
    if (vertical_anchor == "floor") {
        const float authored_offset = config.has_starting_position ? candidate[1] : 0.0f;
        candidate[1] = authored_offset + swimmer.navigation.floor_level_y -
            swimmer.lower_extent + 0.01f;
    } else if (vertical_anchor == "bottom") {
        const float authored_offset = config.has_starting_position ? candidate[1] : 0.0f;
        candidate[1] = authored_offset + vertical[0] - swimmer.lower_extent + 0.01f;
    }

    const auto available = [&](Point3 point) {
        return containsBody(swimmer, point) && initialSpawnAvailable(swimmer, point);
    };

    if (config.has_starting_position) {
        if (available(candidate)) return candidate;
        const Point3 requested = candidate;
        const float search_extent = std::max(
            swimmer.volume_half_extent[0], swimmer.volume_half_extent[2]) * 2.0f;
        for (int ring = 1; ring <= 48; ++ring) {
            const float radius = search_extent * static_cast<float>(ring) / 48.0f;
            for (int step = 0; step < 64; ++step) {
                const float angle = 2.0f * kPi * static_cast<float>(step) / 64.0f;
                Point3 adjusted{requested[0] + radius * std::cos(angle),
                    requested[1], requested[2] + radius * std::sin(angle)};
                if (!available(adjusted)) continue;
                warnings_.push_back(
                    "Aquarium position for " + swimmer.actor.id +
                    " intersected glass, an obstacle, or another Pokemon; adjusted locally");
                return adjusted;
            }
        }
    }

    if (swimmer.behavior == Swimmer::Behavior::School && !config.has_starting_position) {
        Point3 group_center{};
        int group_size = 0;
        float companion_extent = swimmer.radius;
        for (const Swimmer& other : swimmers_) {
            if (other.tank_id != swimmer.tank_id ||
                other.school_id != swimmer.school_id) continue;
            for (int axis = 0; axis < 3; ++axis) {
                group_center[axis] += other.local_position[axis];
            }
            companion_extent = std::max(companion_extent, other.radius);
            ++group_size;
        }
        if (group_size > 0) {
            for (float& axis : group_center) axis /= static_cast<float>(group_size);
            constexpr float kGoldenAngle = 2.39996323f;
            const float base_spacing = swimmer.radius + companion_extent + 0.08f;
            const float phase = count > 0
                ? 2.0f * kPi * static_cast<float>(copy) / static_cast<float>(count)
                : 0.0f;
            for (int attempt = 0; attempt < 144; ++attempt) {
                const int shell = 1 + attempt / 24;
                const float angle = phase + kGoldenAngle * static_cast<float>(attempt);
                const float radius = base_spacing * (1.0f + 0.38f * (shell - 1));
                Point3 grouped{
                    group_center[0] + std::cos(angle) * radius,
                    group_center[1] + std::sin(angle * 1.37f) *
                        (swimmer.body_half_height + 0.04f) *
                        std::min(2, shell),
                    group_center[2] + std::sin(angle) * radius};
                if (available(grouped)) return grouped;
            }
        }
    }
    if (swimmer.behavior == Swimmer::Behavior::Escort && !config.has_starting_position) {
        const auto leader = std::find_if(swimmers_.begin(), swimmers_.end(),
            [&](const Swimmer& other) {
                return other.actor.id == swimmer.follow_actor_id &&
                    other.tank_id == swimmer.tank_id;
            });
        if (leader != swimmers_.end()) {
            const auto body = [](const Swimmer& value) {
                AquariumFormationBody out;
                out.id = value.actor.id;
                out.tank_id = value.tank_id;
                out.origin = value.local_position;
                out.velocity = value.velocity;
                out.yaw_degrees = value.actor.world_yaw_degrees - value.tank.yaw_degrees;
                out.pitch_degrees = value.actor.world_pitch_degrees;
                out.base_pitch_degrees = value.base_pitch_degrees;
                out.center_y_offset =
                    (value.lower_extent + value.upper_extent) * 0.5f;
                out.half_width = value.body_half_width;
                out.half_height = value.body_half_height;
                out.half_length = value.body_half_length;
                return out;
            };
            AquariumFormationInput input;
            input.follower = body(swimmer);
            input.leader = body(*leader);
            input.previous_leader = input.leader;
            input.role_phase_radians = swimmer.school_phase;
            input.follow_distance = swimmer.follow_distance;
            input.body_gap = std::max(0.035f, swimmer.follow_vertical_gap);
            const Point3 formation_spawn = steerAquariumFormation(input).anchor_origin;
            if (available(formation_spawn)) return formation_spawn;
            constexpr float kGoldenAngle = 2.39996323f;
            const float base_spacing = swimmer.radius + 0.06f;
            for (int attempt = 1; attempt <= 96; ++attempt) {
                const int shell = 1 + attempt / 16;
                const float angle = swimmer.school_phase +
                    kGoldenAngle * static_cast<float>(attempt);
                Point3 adjusted{
                    formation_spawn[0] + std::cos(angle) * base_spacing * shell,
                    formation_spawn[1] + std::sin(angle * 1.31f) *
                        (swimmer.body_half_height + 0.03f) * std::min(2, shell),
                    formation_spawn[2] + std::sin(angle) * base_spacing * shell};
                if (available(adjusted)) return adjusted;
            }
        }
    }
    if (!config.has_starting_position && !swimmer.random_start &&
        swimmer.behavior != Swimmer::Behavior::School && available(candidate)) {
        return candidate;
    }

    std::uniform_real_distribution<float> x_pick(
        swimmer.volume_center[0] - swimmer.volume_half_extent[0],
        swimmer.volume_center[0] + swimmer.volume_half_extent[0]);
    const float minimum_y = vertical[0] - swimmer.lower_extent;
    const float maximum_y = swimmer.floor_navigation
        ? minimum_y
        : vertical[1] - swimmer.upper_extent;
    if (maximum_y < minimum_y) return std::nullopt;
    std::uniform_real_distribution<float> y_pick(minimum_y, maximum_y);
    std::uniform_real_distribution<float> z_pick(
        swimmer.volume_center[2] - swimmer.volume_half_extent[2],
        swimmer.volume_center[2] + swimmer.volume_half_extent[2]);
    for (int attempt = 0; attempt < 480; ++attempt) {
        Point3 sampled{x_pick(swimmer.rng), y_pick(swimmer.rng), z_pick(swimmer.rng)};
        if (available(sampled)) return sampled;
    }
    warnings_.push_back(
        "Aquarium start position for " + swimmer.actor.species +
        " could not be separated from existing tank residents");
    return std::nullopt;
}

Point3 AquariumSimulation::toWorld(
    const TankTransform& tank,
    float units_per_meter,
    Point3 local) {
    const float radians = tank.yaw_degrees * kPi / 180.0f;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const float scale = tank.scale * units_per_meter;
    const float x = local[0] * scale;
    const float z = local[2] * scale;
    return {tank.x + x * c + z * s, tank.y + local[1] * scale, tank.z - x * s + z * c};
}

bool AquariumSimulation::chooseTarget(Swimmer& swimmer) {
    if (swimmer.navigation.layers.empty()) return false;
    const auto bounds = verticalBounds(swimmer.navigation);
    const float bottom = bounds[0] - std::min(0.0f, swimmer.lower_extent);
    const float top = bounds[1] - std::max(0.0f, swimmer.upper_extent);
    if (!swimmer.floor_navigation && top < bottom) return false;
    std::uniform_int_distribution<std::size_t> layer_pick(0, swimmer.navigation.layers.size()-1);
    for (int attempt=0; attempt<48; ++attempt) {
        const auto& layer = swimmer.navigation.layers[layer_pick(swimmer.rng)];
        float min_x=std::numeric_limits<float>::max(), max_x=std::numeric_limits<float>::lowest();
        float min_z=min_x, max_z=max_x;
        for (const auto& polygon : layer.polygons) {
            if (polygon.empty()) continue;
            for (auto p : polygon.front()) {
                min_x=std::min(min_x,p[0]); max_x=std::max(max_x,p[0]);
                min_z=std::min(min_z,p[1]); max_z=std::max(max_z,p[1]);
            }
        }
        if (min_x>=max_x || min_z>=max_z) continue;
        float y=swimmer.floor_navigation ? swimmer.local_position[1] :
            std::uniform_real_distribution<float>(bottom,top)(swimmer.rng);
        if(swimmer.habitat_tour && !swimmer.floor_navigation && attempt<36) {
            constexpr float bands[]{0.16f,0.82f,0.45f};
            const float fraction=std::clamp(bands[swimmer.tour_destination%3]+
                std::uniform_real_distribution<float>(-.06f,.06f)(swimmer.rng),.08f,.92f);
            y=bottom+(top-bottom)*fraction;
        }
        const Point3 target{
            std::uniform_real_distribution<float>(min_x,max_x)(swimmer.rng), y,
            std::uniform_real_distribution<float>(min_z,max_z)(swimmer.rng)};
        if (!containsBody(swimmer,target)) continue;
        // Destination validity and route validity are distinct. The route
        // owner finds a way around tunnels/concavities, instead of rejecting
        // every destination without a straight line of sight.
        swimmer.local_target=target;
        if(swimmer.habitat_tour) ++swimmer.tour_destination;
        swimmer.route_waypoints.clear();
        swimmer.route_query.reset();
        swimmer.route_direct_valid=false;
        swimmer.route_visibility_seconds=0.0f;
        swimmer.route_retry_seconds=0.0f;
        return true;
    }
    swimmer.local_target=swimmer.local_position;
    return false;
}

bool AquariumSimulation::chooseTimidTarget(Swimmer& swimmer) {
    if (swimmer.local_move_distance <= 0.0f) return false;
    std::uniform_real_distribution<float> angle_pick(0.0f, 2.0f * kPi);
    std::uniform_real_distribution<float> distance_pick(
        std::min(0.08f, swimmer.local_move_distance), swimmer.local_move_distance);
    for (int attempt = 0; attempt < 48; ++attempt) {
        const float angle = angle_pick(swimmer.rng);
        const float distance = distance_pick(swimmer.rng);
        const Point3 candidate{
            swimmer.local_position[0] + std::cos(angle) * distance,
            swimmer.local_position[1],
            swimmer.local_position[2] + std::sin(angle) * distance};
        if (containsBody(swimmer, candidate) &&
            segmentNavigable(swimmer, swimmer.local_position, candidate)) {
            swimmer.local_target = candidate;
            return true;
        }
    }
    return false;
}

void AquariumSimulation::updateTimid(Swimmer& swimmer, float dt) {
    const Swimmer* nearest_threat = nullptr;
    float nearest_distance_squared = swimmer.flee_radius * swimmer.flee_radius;
    for (const Swimmer& other : swimmers_) {
        if (&other == &swimmer || other.tank_id != swimmer.tank_id) continue;
        const std::string species = normalize(other.actor.species);
        if (std::none_of(swimmer.threat_species.begin(), swimmer.threat_species.end(),
                [&](const std::string& threat) { return normalize(threat) == species; })) {
            continue;
        }
        const float dx = swimmer.local_position[0] - other.local_position[0];
        const float dz = swimmer.local_position[2] - other.local_position[2];
        const float distance_squared = dx * dx + dz * dz;
        if (distance_squared <= nearest_distance_squared) {
            nearest_distance_squared = distance_squared;
            nearest_threat = &other;
        }
    }

    const auto resetIdleTimer = [&] {
        std::uniform_real_distribution<float> idle_pick(
            swimmer.idle_seconds_minimum, swimmer.idle_seconds_maximum);
        swimmer.behavior_timer_seconds = idle_pick(swimmer.rng);
        swimmer.timid_moving = false;
    };

    if (nearest_threat && swimmer.flee_distance > 0.0f) {
        float away_x = swimmer.local_position[0] - nearest_threat->local_position[0];
        float away_z = swimmer.local_position[2] - nearest_threat->local_position[2];
        float away_length = std::hypot(away_x, away_z);
        if (away_length <= 0.0001f) {
            std::uniform_real_distribution<float> angle_pick(0.0f, 2.0f * kPi);
            const float angle = angle_pick(swimmer.rng);
            away_x = std::cos(angle);
            away_z = std::sin(angle);
        } else {
            away_x /= away_length;
            away_z /= away_length;
        }
        const float base_angle = std::atan2(away_z, away_x);
        bool found = false;
        float best_distance_squared = nearest_distance_squared;
        Point3 best = swimmer.local_position;
        constexpr std::array<float, 7> kAngleOffsets{
            0.0f, 0.35f, -0.35f, 0.75f, -0.75f, 1.2f, -1.2f};
        for (const float distance_scale : {1.0f, 0.65f, 0.35f}) {
            for (const float angle_offset : kAngleOffsets) {
                const float angle = base_angle + angle_offset;
                const Point3 candidate{
                    swimmer.local_position[0] + std::cos(angle) *
                        swimmer.flee_distance * distance_scale,
                    swimmer.local_position[1],
                    swimmer.local_position[2] + std::sin(angle) *
                        swimmer.flee_distance * distance_scale};
                if (!containsBody(swimmer, candidate) ||
                    !segmentNavigable(swimmer, swimmer.local_position, candidate)) continue;
                const float dx = candidate[0] - nearest_threat->local_position[0];
                const float dz = candidate[2] - nearest_threat->local_position[2];
                const float distance_squared = dx * dx + dz * dz;
                if (distance_squared <= best_distance_squared) continue;
                best_distance_squared = distance_squared;
                best = candidate;
                found = true;
            }
            if (found) break;
        }
        if (found) {
            const float ordinary_speed = swimmer.speed;
            swimmer.speed *= swimmer.flee_speed_multiplier;
            moveTowardTarget(swimmer, best, dt);
            swimmer.speed = ordinary_speed;
        }
        swimmer.behavior_timer_seconds = swimmer.idle_seconds_maximum;
        swimmer.timid_moving = false;
        return;
    }

    if (swimmer.timid_moving) {
        const float dx = swimmer.local_target[0] - swimmer.local_position[0];
        const float dz = swimmer.local_target[2] - swimmer.local_position[2];
        if (std::hypot(dx, dz) < std::max(0.025f, swimmer.speed * dt * 1.5f) ||
            !moveTowardTarget(swimmer, swimmer.local_target, dt)) {
            resetIdleTimer();
        }
        return;
    }

    swimmer.behavior_timer_seconds -= dt;
    if (swimmer.behavior_timer_seconds <= 0.0f) {
        swimmer.timid_moving = chooseTimidTarget(swimmer);
        if (!swimmer.timid_moving) resetIdleTimer();
    }
}

bool AquariumSimulation::chooseJellyTarget(Swimmer& swimmer) {
    const auto vertical = verticalBounds(swimmer.navigation);
    const float minimum_y = vertical[0] - swimmer.lower_extent;
    const float maximum_y = vertical[1] - swimmer.upper_extent;
    if (maximum_y <= minimum_y) return false;
    const float half_vertical_travel = std::min(
        0.38f, std::max(0.10f, (maximum_y - minimum_y) * 0.16f));
    const float horizontal_travel = std::min(0.20f, std::max(
        0.04f, std::min(swimmer.volume_half_extent[0],
            swimmer.volume_half_extent[2]) * 0.08f));
    std::uniform_real_distribution<float> angle_pick(0.0f, 2.0f * kPi);
    std::uniform_real_distribution<float> radius_pick(0.25f, 1.0f);
    for (int direction_attempt = 0; direction_attempt < 2; ++direction_attempt) {
        const bool ascending = direction_attempt == 0
            ? swimmer.jelly_next_ascending : !swimmer.jelly_next_ascending;
        const float target_y = std::clamp(
            swimmer.jelly_anchor[1] + (ascending ? half_vertical_travel : -half_vertical_travel),
            minimum_y, maximum_y);
        for (int attempt = 0; attempt < 32; ++attempt) {
            const float angle = angle_pick(swimmer.rng);
            const float radius = horizontal_travel * radius_pick(swimmer.rng);
            const Point3 candidate{
                swimmer.jelly_anchor[0] + std::cos(angle) * radius,
                target_y,
                swimmer.jelly_anchor[2] + std::sin(angle) * radius};
            if (!containsBody(swimmer, candidate) ||
                !segmentNavigable(swimmer, swimmer.local_position, candidate)) continue;
            swimmer.local_target = candidate;
            swimmer.jelly_next_ascending = !ascending;
            return true;
        }
    }
    swimmer.local_target = swimmer.local_position;
    return false;
}

void AquariumSimulation::updateJelly(Swimmer& swimmer, float dt) {
    const float dx = swimmer.local_target[0] - swimmer.local_position[0];
    const float dy = swimmer.local_target[1] - swimmer.local_position[1];
    const float dz = swimmer.local_target[2] - swimmer.local_position[2];
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (distance < std::max(0.025f, swimmer.speed * dt * 1.5f)) {
        swimmer.movement_blocked_seconds = 0.0f;
        chooseJellyTarget(swimmer);
        return;
    }
    if (moveTowardTarget(swimmer, swimmer.local_target, dt)) {
        swimmer.movement_blocked_seconds = 0.0f;
    } else if (shouldRetargetBlockedMovement(swimmer, dt)) {
        chooseJellyTarget(swimmer);
    }
}

bool AquariumSimulation::chooseActivityTarget(Swimmer& swimmer) {
    if (swimmer.floor_navigation || !swimmer.rest_at_bottom ||
        swimmer.roaming_height <= 0.0f) {
        return chooseTarget(swimmer);
    }
    const auto vertical = verticalBounds(swimmer.navigation);
    const float bottom = vertical[0] - swimmer.lower_extent + 0.01f;
    const float top = vertical[1] - swimmer.upper_extent;
    if (top <= bottom) return false;
    std::uniform_real_distribution<float> height_pick(0.55f, 1.0f);
    for (int attempt = 0; attempt < 24; ++attempt) {
        if (!chooseTarget(swimmer)) return false;
        Point3 candidate = swimmer.local_target;
        candidate[1] = std::min(
            top, bottom + swimmer.roaming_height * height_pick(swimmer.rng));
        if (containsBody(swimmer, candidate) &&
            segmentNavigable(swimmer, swimmer.local_position, candidate)) {
            swimmer.local_target = candidate;
            return true;
        }
    }
    return false;
}

bool AquariumSimulation::chooseActivityRestTarget(Swimmer& swimmer) {
    const auto vertical = verticalBounds(swimmer.navigation);
    const float resting_y = vertical[0] - swimmer.lower_extent + 0.01f;
    Point3 candidate = swimmer.local_position;
    candidate[1] = resting_y;
    if (containsBody(swimmer, candidate) &&
        segmentNavigable(swimmer, swimmer.local_position, candidate)) {
        swimmer.local_target = candidate;
        return true;
    }

    std::uniform_real_distribution<float> x_pick(
        swimmer.volume_center[0] - swimmer.volume_half_extent[0],
        swimmer.volume_center[0] + swimmer.volume_half_extent[0]);
    std::uniform_real_distribution<float> z_pick(
        swimmer.volume_center[2] - swimmer.volume_half_extent[2],
        swimmer.volume_center[2] + swimmer.volume_half_extent[2]);
    for (int attempt = 0; attempt < 48; ++attempt) {
        candidate = {x_pick(swimmer.rng), resting_y, z_pick(swimmer.rng)};
        if (!containsBody(swimmer, candidate) ||
            !segmentNavigable(swimmer, swimmer.local_position, candidate)) continue;
        swimmer.local_target = candidate;
        return true;
    }
    return false;
}

void AquariumSimulation::beginActivityRest(Swimmer& swimmer) {
    swimmer.motion_progress.reset();
    swimmer.recovering=false;
    swimmer.route_query.reset();
    swimmer.route_waypoints.clear();
    swimmer.activity_phase = Swimmer::ActivityPhase::Resting;
    std::uniform_real_distribution<float> timer(
        swimmer.rest_seconds_minimum, swimmer.rest_seconds_maximum);
    swimmer.activity_timer_seconds = timer(swimmer.rng);
    swimmer.actor.animation = swimmer.idle_animation;
    std::uniform_real_distribution<double> phase(0.0, 1.0);
    swimmer.actor.animation_time_seconds = phase(swimmer.rng);
    swimmer.actor.world_pitch_degrees = swimmer.idle_pitch_degrees;
    swimmer.velocity = {};
    swimmer.motion_direction_initialized = false;
    swimmer.movement_blocked_seconds = 0.0f;
}

void AquariumSimulation::beginActivityMove(Swimmer& swimmer) {
    swimmer.activity_phase = Swimmer::ActivityPhase::Moving;
    std::uniform_real_distribution<float> timer(
        swimmer.move_seconds_minimum, swimmer.move_seconds_maximum);
    swimmer.activity_timer_seconds = timer(swimmer.rng);
    swimmer.actor.animation = swimmer.movement_animation;
    std::uniform_real_distribution<double> phase(0.0, 1.0);
    swimmer.actor.animation_time_seconds = phase(swimmer.rng);
    swimmer.actor.world_pitch_degrees = swimmer.base_pitch_degrees;
    swimmer.motion_direction_initialized = false;
    swimmer.movement_blocked_seconds = 0.0f;
    if (!chooseActivityTarget(swimmer)) beginActivityRest(swimmer);
}

void AquariumSimulation::updateActivity(Swimmer& swimmer, float dt) {
    if (swimmer.activity_phase == Swimmer::ActivityPhase::Resting) {
        swimmer.velocity = {};
        swimmer.activity_timer_seconds -= dt;
        if (swimmer.activity_timer_seconds <= 0.0f) beginActivityMove(swimmer);
        return;
    }

    const auto distanceToTarget = [&] {
        const float dx = swimmer.local_target[0] - swimmer.local_position[0];
        const float dy = swimmer.local_target[1] - swimmer.local_position[1];
        const float dz = swimmer.local_target[2] - swimmer.local_position[2];
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    };
    if (swimmer.activity_phase == Swimmer::ActivityPhase::ReturningToRest) {
        if (distanceToTarget() < std::max(0.025f, swimmer.speed * dt * 1.5f)) {
            beginActivityRest(swimmer);
        } else if (moveTowardTarget(swimmer, swimmer.local_target, dt)) {
            swimmer.movement_blocked_seconds = 0.0f;
        } else if (shouldRetargetBlockedMovement(swimmer, dt) &&
                   !chooseActivityRestTarget(swimmer)) {
            // A disconnected dry volume may make every floor route unreachable.
            // Rest in place rather than spinning or retrying forever.
            beginActivityRest(swimmer);
        }
        return;
    }

    swimmer.activity_timer_seconds -= dt;
    if (swimmer.activity_timer_seconds <= 0.0f) {
        if (!swimmer.floor_navigation && swimmer.rest_at_bottom) {
            swimmer.activity_phase = Swimmer::ActivityPhase::ReturningToRest;
            swimmer.motion_direction_initialized = false;
            if (!chooseActivityRestTarget(swimmer)) beginActivityRest(swimmer);
        } else {
            beginActivityRest(swimmer);
        }
        return;
    }
    if (distanceToTarget() < std::max(0.04f, swimmer.speed * dt * 1.5f)) {
        chooseActivityTarget(swimmer);
    } else if (moveTowardTarget(swimmer, swimmer.local_target, dt)) {
        swimmer.movement_blocked_seconds = 0.0f;
    } else if (shouldRetargetBlockedMovement(swimmer, dt)) {
        chooseActivityTarget(swimmer);
    }
}


bool AquariumSimulation::moveWithFormationVelocity(
    Swimmer& swimmer,
    Point3 desired_velocity,
    float dt) {
    Point3 velocity_delta{
        desired_velocity[0] - swimmer.velocity[0],
        desired_velocity[1] - swimmer.velocity[1],
        desired_velocity[2] - swimmer.velocity[2]};
    const float delta_length = std::sqrt(
        velocity_delta[0] * velocity_delta[0] +
        velocity_delta[1] * velocity_delta[1] +
        velocity_delta[2] * velocity_delta[2]);
    const float desired_speed = std::sqrt(
        desired_velocity[0] * desired_velocity[0] +
        desired_velocity[1] * desired_velocity[1] +
        desired_velocity[2] * desired_velocity[2]);
    const float maximum_delta = std::max(2.4f, desired_speed * 5.0f) * dt;
    if (delta_length > maximum_delta && delta_length > 0.0001f) {
        for (float& component : velocity_delta) component *= maximum_delta / delta_length;
    }
    Point3 next_velocity{
        swimmer.velocity[0] + velocity_delta[0],
        swimmer.velocity[1] + velocity_delta[1],
        swimmer.velocity[2] + velocity_delta[2]};
    if (desired_speed > 0.0001f) {
        const Point3 desired_direction{
            desired_velocity[0] / desired_speed,
            desired_velocity[1] / desired_speed,
            desired_velocity[2] / desired_speed};
        const float opposing_speed =
            next_velocity[0] * desired_direction[0] +
            next_velocity[1] * desired_direction[1] +
            next_velocity[2] * desired_direction[2];
        if (opposing_speed < 0.0f) {
            for (std::size_t axis = 0; axis < next_velocity.size(); ++axis) {
                next_velocity[axis] -= desired_direction[axis] * opposing_speed;
            }
        }
    }
    Point3 candidate{
        swimmer.local_position[0] + next_velocity[0] * dt,
        swimmer.local_position[1] + next_velocity[1] * dt,
        swimmer.local_position[2] + next_velocity[2] * dt};
    if (!containsBody(swimmer, candidate) ||
        !segmentNavigable(swimmer, swimmer.local_position, candidate) ||
        !crowdAllowsMove(swimmer, candidate)) return false;

    swimmer.local_position = candidate;
    swimmer.velocity = next_velocity;
    const float horizontal = std::sqrt(
        next_velocity[0] * next_velocity[0] + next_velocity[2] * next_velocity[2]);
    if (horizontal > 0.0001f || std::abs(next_velocity[1]) > 0.0001f) {
        const float desired_yaw = std::atan2(next_velocity[0], next_velocity[2]) *
            180.0f / kPi + swimmer.tank.yaw_degrees;
        const float desired_pitch = swimmer.base_pitch_degrees -
            std::atan2(next_velocity[1], std::max(0.0001f, horizontal)) * 180.0f / kPi;
        const float yaw_step = std::max(120.0f, swimmer.turn_speed) * dt;
        const float pitch_step = std::max(90.0f, swimmer.pitch_turn_speed) * dt;
        swimmer.actor.world_yaw_degrees = wrapDegrees(
            swimmer.actor.world_yaw_degrees + std::clamp(
                wrapDegrees(desired_yaw - swimmer.actor.world_yaw_degrees),
                -yaw_step, yaw_step));
        swimmer.actor.world_pitch_degrees = wrapDegrees(
            swimmer.actor.world_pitch_degrees + std::clamp(
                wrapDegrees(desired_pitch - swimmer.actor.world_pitch_degrees),
                -pitch_step, pitch_step));
    }
    return true;
}

bool AquariumSimulation::chooseEscortDetour(
    Swimmer& swimmer,
    Point3 desired_velocity,
    Point3 anchor) {
    const float speed = std::sqrt(
        desired_velocity[0] * desired_velocity[0] +
        desired_velocity[1] * desired_velocity[1] +
        desired_velocity[2] * desired_velocity[2]);
    Point3 forward = speed > 0.0001f
        ? Point3{desired_velocity[0] / speed, desired_velocity[1] / speed,
            desired_velocity[2] / speed}
        : Point3{0.0f, 0.0f, 1.0f};
    Point3 right{forward[2], 0.0f, -forward[0]};
    const float right_length = std::sqrt(right[0] * right[0] + right[2] * right[2]);
    if (right_length > 0.0001f) {
        right[0] /= right_length;
        right[2] /= right_length;
    } else {
        right = {1.0f, 0.0f, 0.0f};
    }
    const std::array<Point3, 8> probes{
        Point3{forward[0] + right[0] * 0.85f, forward[1], forward[2] + right[2] * 0.85f},
        Point3{forward[0] - right[0] * 0.85f, forward[1], forward[2] - right[2] * 0.85f},
        Point3{forward[0], forward[1] + 0.85f, forward[2]},
        Point3{forward[0], forward[1] - 0.85f, forward[2]},
        right, Point3{-right[0], 0.0f, -right[2]},
        Point3{0.0f, 1.0f, 0.0f}, Point3{0.0f, -1.0f, 0.0f}};
    bool found = false;
    float best_score = std::numeric_limits<float>::max();
    Point3 best{};
    for (Point3 direction : probes) {
        const float magnitude = std::sqrt(
            direction[0] * direction[0] + direction[1] * direction[1] +
            direction[2] * direction[2]);
        if (magnitude <= 0.0001f) continue;
        for (float& component : direction) component /= magnitude;
        Point3 candidate{
            swimmer.local_position[0] + direction[0] * 0.65f,
            swimmer.local_position[1] + direction[1] * 0.65f,
            swimmer.local_position[2] + direction[2] * 0.65f};
        if (!containsBody(swimmer, candidate) ||
            !segmentNavigable(swimmer, swimmer.local_position, candidate)) continue;
        const float dx = anchor[0] - candidate[0];
        const float dy = anchor[1] - candidate[1];
        const float dz = anchor[2] - candidate[2];
        const float score = dx * dx + dy * dy + dz * dz;
        if (score >= best_score) continue;
        best_score = score;
        best = candidate;
        found = true;
    }
    if (!found) return chooseTarget(swimmer);
    swimmer.local_target = best;
    return true;
}

void AquariumSimulation::updateEscort(
    Swimmer& swimmer,
    const AquariumFormationSteering& steering,
    float dt) {
    swimmer.formation_mode = steering.mode;
    if (swimmer.school_detouring) {
        if (segmentNavigable(swimmer, swimmer.local_position, steering.anchor_origin)) {
            swimmer.school_detouring = false;
        } else {
            Point3 delta{
                swimmer.local_target[0] - swimmer.local_position[0],
                swimmer.local_target[1] - swimmer.local_position[1],
                swimmer.local_target[2] - swimmer.local_position[2]};
            const float distance = std::sqrt(
                delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
            if (distance > 0.05f) {
                const float detour_speed = std::max(0.35f, std::sqrt(
                    steering.desired_velocity[0] * steering.desired_velocity[0] +
                    steering.desired_velocity[1] * steering.desired_velocity[1] +
                    steering.desired_velocity[2] * steering.desired_velocity[2]));
                for (float& component : delta) component *= detour_speed / distance;
                if (moveWithFormationVelocity(swimmer, delta, dt)) {
                    swimmer.movement_blocked_seconds = 0.0f;
                    return;
                }
            }
            if (!shouldRetargetBlockedMovement(swimmer, dt)) return;
            swimmer.school_detouring = false;
        }
    }
    if (moveWithFormationVelocity(swimmer, steering.desired_velocity, dt)) {
        swimmer.movement_blocked_seconds = 0.0f;
        return;
    }
    swimmer.velocity = {};
    if (!shouldRetargetBlockedMovement(swimmer, dt)) return;
    swimmer.school_detouring = chooseEscortDetour(
        swimmer, steering.desired_velocity, steering.anchor_origin);
}


void AquariumSimulation::syncActor(Swimmer& swimmer) {
    swimmer.actor.world_position = toWorld(
        swimmer.tank, swimmer.navigation.export_units_per_meter, swimmer.local_position);
    if (swimmer.actor_index < actors_.size()) actors_[swimmer.actor_index] = swimmer.actor;
}

void AquariumSimulation::resolveCrowdPenetrations() {
    const auto crowdBody = [](const Swimmer& swimmer) {
        return AquariumCrowdBody{
            {swimmer.local_position[0], swimmer.local_position[1] +
                (swimmer.lower_extent + swimmer.upper_extent) * 0.5f,
                swimmer.local_position[2]},
            swimmer.velocity,
            std::max(swimmer.body_half_width, swimmer.body_half_length),
            swimmer.body_half_height,
            swimmer.crowd_body_scale};
    };
    for (int pass = 0; pass < 2; ++pass) {
        for (std::size_t first_index = 0; first_index < swimmers_.size(); ++first_index) {
            for (std::size_t second_index = first_index + 1;
                 second_index < swimmers_.size(); ++second_index) {
                Swimmer& first = swimmers_[first_index];
                Swimmer& second = swimmers_[second_index];
                if (first.tank_id != second.tank_id) continue;
                const float reach = std::max(first.body_half_width, first.body_half_length) +
                    std::max(second.body_half_width, second.body_half_length);
                if (std::abs(first.local_position[0] - second.local_position[0]) > reach ||
                    std::abs(first.local_position[2] - second.local_position[2]) > reach) continue;
                AquariumCrowdCorrection correction = separateAquariumCrowdCores(
                    crowdBody(first), crowdBody(second));
                if (!correction.penetrating) continue;
                if (first.floor_navigation && second.floor_navigation) {
                    const float push = std::sqrt(
                        correction.first_delta[0] * correction.first_delta[0] +
                        correction.first_delta[1] * correction.first_delta[1] +
                        correction.first_delta[2] * correction.first_delta[2]);
                    float dx = first.local_position[0] - second.local_position[0];
                    float dz = first.local_position[2] - second.local_position[2];
                    const float distance = std::hypot(dx, dz);
                    if (distance <= 0.0001f) {
                        dx = first.actor.id < second.actor.id ? -1.0f : 1.0f;
                        dz = 0.0f;
                    } else {
                        dx /= distance;
                        dz /= distance;
                    }
                    const float horizontal_push = std::max(push, 0.003f);
                    correction.first_delta = {
                        dx * horizontal_push, 0.0f, dz * horizontal_push};
                    correction.second_delta = {
                        -dx * horizontal_push, 0.0f, -dz * horizontal_push};
                } else {
                    if (first.floor_navigation) correction.first_delta[1] = 0.0f;
                    if (second.floor_navigation) correction.second_delta[1] = 0.0f;
                }
                // Resolve host/escort penetration entirely on the escort side.
                // Small companions must not shove a cruising host off its path.
                if (second.behavior == Swimmer::Behavior::Escort && second.follow_actor_id == first.actor.id) {
                    for (int axis = 0; axis < 3; ++axis) {
                        correction.second_delta[axis] -= correction.first_delta[axis];
                        correction.first_delta[axis] = 0.0f;
                    }
                } else if (first.behavior == Swimmer::Behavior::Escort && first.follow_actor_id == second.actor.id) {
                    for (int axis = 0; axis < 3; ++axis) {
                        correction.first_delta[axis] -= correction.second_delta[axis];
                        correction.second_delta[axis] = 0.0f;
                    }
                }
                Point3 first_candidate{
                    first.local_position[0] + correction.first_delta[0],
                    first.local_position[1] + correction.first_delta[1],
                    first.local_position[2] + correction.first_delta[2]};
                Point3 second_candidate{
                    second.local_position[0] + correction.second_delta[0],
                    second.local_position[1] + correction.second_delta[1],
                    second.local_position[2] + correction.second_delta[2]};
                const bool first_valid = containsBody(first, first_candidate) &&
                    segmentNavigable(first, first.local_position, first_candidate);
                const bool second_valid = containsBody(second, second_candidate) &&
                    segmentNavigable(second, second.local_position, second_candidate);
                if (first_valid) first.local_position = first_candidate;
                if (second_valid) second.local_position = second_candidate;
            }
        }
    }
}

void AquariumSimulation::simulateStep(float dt) {
    advanceNavigationQueries();
    for (Swimmer& swimmer : swimmers_) {
        swimmer.previous_local_position = swimmer.local_position;
        swimmer.snapshot_velocity = swimmer.velocity;
        swimmer.previous_world_yaw_degrees = swimmer.actor.world_yaw_degrees;
        swimmer.previous_world_pitch_degrees = swimmer.actor.world_pitch_degrees;
    }
    prepareSchools(dt);

    // Leaders and independent swimmers advance first. Escorts are then solved
    // from one shared snapshot so follower ordering cannot become cohesion.
    for (Swimmer& swimmer : swimmers_) {
        if (swimmer.behavior == Swimmer::Behavior::Escort) continue;
        if (swimmer.activity_phase != Swimmer::ActivityPhase::Continuous) {
            updateActivity(swimmer, dt);
        } else if (swimmer.behavior == Swimmer::Behavior::Stationary) {
            swimmer.velocity = {};
        } else if (swimmer.behavior == Swimmer::Behavior::School) {
            updateSchool(swimmer, dt);
        } else if (swimmer.behavior == Swimmer::Behavior::Timid) {
            updateTimid(swimmer, dt);
        } else if (swimmer.behavior == Swimmer::Behavior::Jelly) {
            updateJelly(swimmer, dt);
        } else {
            const float dx = swimmer.local_target[0] - swimmer.local_position[0];
            const float dy = swimmer.local_target[1] - swimmer.local_position[1];
            const float dz = swimmer.local_target[2] - swimmer.local_position[2];
            const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float arrival=swimmer.continuous_cruise
                ? std::max(0.5f,swimmer.radius*1.25f) : std::max(0.04f, swimmer.speed * dt * 1.5f);
            const bool reached=swimmer.habitat_tour
                ? std::abs(dy)<0.5f : distance<arrival;
            if (reached) {
                swimmer.movement_blocked_seconds = 0.0f;
                chooseTarget(swimmer);
            } else if (moveTowardTarget(swimmer, swimmer.local_target, dt)) {
                swimmer.movement_blocked_seconds = 0.0f;
            } else if (shouldRetargetBlockedMovement(swimmer, dt)) {
                chooseTarget(swimmer);
            }
        }
        swimmer.velocity = {
            (swimmer.local_position[0] - swimmer.previous_local_position[0]) / dt,
            (swimmer.local_position[1] - swimmer.previous_local_position[1]) / dt,
            (swimmer.local_position[2] - swimmer.previous_local_position[2]) / dt};
        swimmer.actor.animation_time_seconds += dt;
    }

    const auto formation_body = [](const Swimmer& swimmer) {
        AquariumFormationBody body;
        body.id = swimmer.actor.id;
        body.tank_id = swimmer.tank_id;
        body.origin = swimmer.local_position;
        body.velocity = swimmer.velocity;
        body.yaw_degrees = swimmer.actor.world_yaw_degrees - swimmer.tank.yaw_degrees;
        body.pitch_degrees = swimmer.actor.world_pitch_degrees;
        body.base_pitch_degrees = swimmer.base_pitch_degrees;
        body.center_y_offset = (swimmer.lower_extent + swimmer.upper_extent) * 0.5f;
        body.half_width = swimmer.body_half_width;
        body.half_height = swimmer.body_half_height;
        body.half_length = swimmer.body_half_length;
        return body;
    };
    std::vector<AquariumFormationBody> bodies;
    bodies.reserve(swimmers_.size());
    for (const Swimmer& swimmer : swimmers_) bodies.push_back(formation_body(swimmer));
    std::vector<AquariumFormationSteering> steering(swimmers_.size());
    std::vector<bool> has_steering(swimmers_.size(), false);

    for (std::size_t index = 0; index < swimmers_.size(); ++index) {
        Swimmer& follower = swimmers_[index];
        if (follower.behavior != Swimmer::Behavior::Escort) continue;
        follower.school_elapsed += dt;
        const auto leader = std::find_if(swimmers_.begin(), swimmers_.end(),
            [&](const Swimmer& candidate) {
                return candidate.actor.id == follower.follow_actor_id &&
                    candidate.tank_id == follower.tank_id;
            });
        if (leader == swimmers_.end() || &*leader == &follower) continue;
        const std::size_t leader_index = static_cast<std::size_t>(
            std::distance(swimmers_.begin(), leader));
        AquariumFormationBody previous_leader = bodies[leader_index];
        previous_leader.origin = leader->previous_local_position;
        previous_leader.yaw_degrees =
            leader->previous_world_yaw_degrees - leader->tank.yaw_degrees;
        previous_leader.pitch_degrees = leader->previous_world_pitch_degrees;
        AquariumFormationInput input;
        input.follower = bodies[index];
        input.leader = bodies[leader_index];
        input.previous_leader = std::move(previous_leader);
        input.neighbours = &bodies;
        input.role_phase_radians = follower.school_phase;
        input.elapsed_seconds = follower.school_elapsed;
        input.follow_distance = follower.follow_distance;
        input.body_gap = std::max(0.035f, follower.follow_vertical_gap);
        input.dt_seconds = dt;
        input.previous_mode = follower.formation_mode;
        steering[index] = steerAquariumFormation(input);
        has_steering[index] = true;
    }

    for (std::size_t index = 0; index < swimmers_.size(); ++index) {
        Swimmer& follower = swimmers_[index];
        if (follower.behavior != Swimmer::Behavior::Escort) continue;
        if (has_steering[index]) {
            updateEscort(follower, steering[index], dt);
        } else {
            if (moveTowardTarget(follower, follower.local_target, dt)) {
                follower.movement_blocked_seconds = 0.0f;
            } else if (shouldRetargetBlockedMovement(follower, dt)) {
                chooseTarget(follower);
            }
            follower.velocity = {
                (follower.local_position[0] - follower.previous_local_position[0]) / dt,
                (follower.local_position[1] - follower.previous_local_position[1]) / dt,
                (follower.local_position[2] - follower.previous_local_position[2]) / dt};
        }
        follower.actor.animation_time_seconds += dt;
    }

    resolveCrowdPenetrations();
    for (Swimmer& swimmer : swimmers_) syncActor(swimmer);
}

void AquariumSimulation::update(double dt_seconds) {
    constexpr double kFixedStep = 1.0 / 60.0;
    simulation_accumulator_seconds_ += std::clamp(dt_seconds, 0.0, 0.1333333333);
    int steps = 0;
    while (simulation_accumulator_seconds_ + 1.0e-9 >= kFixedStep && steps < 8) {
        simulateStep(static_cast<float>(kFixedStep));
        simulation_accumulator_seconds_ -= kFixedStep;
        ++steps;
    }
}

} // namespace pr::gameplay::world3d::aquarium
