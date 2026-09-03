#include "gameplay/world3d/aquarium/AquariumSimulation.hpp"

#include "gameplay/attend/PokemonModelCatalog.hpp"
#include "gameplay/world3d/aquarium/AquariumPokemonMetrics.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>

namespace pr::gameplay::world3d::aquarium {
namespace {

constexpr float kPi = 3.14159265358979323846f;

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
        std::uint32_t actor_seed = tank_config.seed;
        for (const AquariumPokemonConfig& pokemon : tank_config.pokemon) {
            const std::string model_path = resolvePokemonModel(pokemon_models, pokemon.species);
            if (model_path.empty()) {
                warnings_.push_back("Aquarium Pokemon model not found by name: " + pokemon.species);
                continue;
            }
            for (int copy = 0; copy < pokemon.count; ++copy) {
                AquariumPokemonActor actor;
                actor.id = pokemon.id.empty()
                    ? tank_config.placement_id + ":" + normalize(pokemon.species) + ":" + std::to_string(copy)
                    : pokemon.id + (pokemon.count > 1 ? ":" + std::to_string(copy) : "");
                actor.species = pokemon.species;
                actor.form = pokemon.form;
                actor.model_path = model_path;
                actor.animation = pokemon.animation;
                actor.model_scale = config->pokemon_scale * pokemon.size_multiplier;
                actor.world_pitch_degrees = pokemon.pitch_degrees;
                actor.presentation = config->pokemon_presentation;
                appendSwimmer(std::move(actor), pokemon, tank_context,
                    actor_seed++, copy, pokemon.count, false);
            }
        }
    }
    authored_tank_count_ = tanks_.size();
    authored_warning_count_ = warnings_.size();
}

void AquariumSimulation::replacePlayerTanks(
    const std::vector<AquariumPlayerTankSimulationInput>& inputs) {
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
            {}});

        for (const AquariumSwimmerDefinition& definition : input.swimmers) {
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
    swimmer.speed = movement.speed_meters_per_second;
    swimmer.turn_speed = movement.turn_degrees_per_second;
    swimmer.floor_navigation = normalize(movement.movement_plane) == "floor";
    swimmer.player_built = player_built;

    std::string metrics_error;
    const AquariumPokemonMetrics metrics = measureAquariumPokemon(
        swimmer.actor.model_path, swimmer.actor.form, &metrics_error);
    if (metrics.valid) {
        const AquariumPokemonMetrics oriented_metrics =
            rotateAquariumPokemonMetrics(metrics, swimmer.actor.world_pitch_degrees);
        const float model_units_to_meters =
            swimmer.actor.model_scale / tank.world_units_per_meter;
        const float model_radius = std::max({
            std::abs(oriented_metrics.min_x), std::abs(oriented_metrics.max_x),
            std::abs(oriented_metrics.min_z), std::abs(oriented_metrics.max_z)}) *
            model_units_to_meters;
        // Shallow-pool actors navigate by a contact footprint. A face-up model
        // may deliberately overhang it, unlike a freely swimming body.
        swimmer.radius = swimmer.floor_navigation
            ? movement.body_radius_meters
            : std::max(movement.body_radius_meters, model_radius);
        swimmer.lower_extent = oriented_metrics.min_y * model_units_to_meters;
        swimmer.upper_extent = oriented_metrics.max_y * model_units_to_meters;
    } else {
        swimmer.radius = movement.body_radius_meters;
        warnings_.push_back(
            "Aquarium Pokemon bounds unavailable for " + swimmer.actor.species +
            ": " + metrics_error);
    }

    const std::string behavior = normalize(movement.behavior);
    if (behavior == "school") {
        swimmer.behavior = Swimmer::Behavior::School;
    } else if (behavior == "stationary" || swimmer.speed <= 0.0f) {
        swimmer.behavior = Swimmer::Behavior::Stationary;
    } else {
        swimmer.behavior = Swimmer::Behavior::Wander;
    }
    swimmer.school_id = tank.id + ":" +
        (movement.id.empty() ? normalize(swimmer.actor.species) : movement.id);
    swimmer.school_phase = count > 0
        ? (2.0f * kPi * static_cast<float>(copy) / static_cast<float>(count))
        : 0.0f;
    swimmer.volume_center = {
        (tank.horizontal_bounds[0] + tank.horizontal_bounds[1]) * 0.5f,
        (tank.vertical_bounds[0] + tank.vertical_bounds[1]) * 0.5f,
        (tank.horizontal_bounds[2] + tank.horizontal_bounds[3]) * 0.5f};
    swimmer.volume_half_extent = {
        (tank.horizontal_bounds[1] - tank.horizontal_bounds[0]) * 0.5f,
        (tank.vertical_bounds[1] - tank.vertical_bounds[0]) * 0.5f,
        (tank.horizontal_bounds[3] - tank.horizontal_bounds[2]) * 0.5f};
    swimmer.rng.seed(seed);
    swimmer.local_position = resolveStartingPosition(swimmer, movement, copy, count);
    if (!containsBody(swimmer, swimmer.local_position)) {
        warnings_.push_back(
            "Aquarium Pokemon " + swimmer.actor.species + " cannot fit inside " +
            tank.id + " at the configured shared scale; actor was not spawned");
        return false;
    }
    if (swimmer.behavior == Swimmer::Behavior::Wander) chooseTarget(swimmer);
    syncActor(swimmer);
    actors_.push_back(swimmer.actor);
    swimmers_.push_back(std::move(swimmer));
    return true;
}

bool AquariumSimulation::containsBody(const Swimmer& swimmer, Point3 origin) const {
    if (swimmer.floor_navigation) {
        origin[1] = swimmer.volume_center[1];
        return containsPoint(swimmer.navigation, origin, swimmer.radius);
    }
    if (!containsPoint(swimmer.navigation, origin, swimmer.radius)) return false;
    Point3 bottom = origin;
    Point3 top = origin;
    bottom[1] += swimmer.lower_extent;
    top[1] += swimmer.upper_extent;
    return containsPoint(swimmer.navigation, bottom, swimmer.radius) &&
        containsPoint(swimmer.navigation, top, swimmer.radius);
}

bool AquariumSimulation::segmentNavigable(
    const Swimmer& swimmer,
    Point3 from,
    Point3 to) const {
    if (swimmer.floor_navigation) {
        from[1] = swimmer.volume_center[1];
        to[1] = swimmer.volume_center[1];
    }
    return segmentIsNavigable(swimmer.navigation, from, to, swimmer.radius);
}

Point3 AquariumSimulation::resolveStartingPosition(
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
        candidate[1] += swimmer.navigation.floor_level_y - swimmer.lower_extent + 0.01f;
    } else if (vertical_anchor == "bottom") {
        candidate[1] += vertical[0] - swimmer.lower_extent + 0.01f;
    }

    if (swimmer.behavior == Swimmer::Behavior::School && !config.has_starting_position) {
        const float phase = count > 0
            ? 2.0f * kPi * static_cast<float>(copy) / static_cast<float>(count)
            : 0.0f;
        for (float radius_scale : {0.62f, 0.48f, 0.34f, 0.2f}) {
            candidate = {
                swimmer.volume_center[0] +
                    std::max(0.0f, swimmer.volume_half_extent[0] - swimmer.radius) *
                    radius_scale * std::cos(phase),
                swimmer.volume_center[1] + swimmer.volume_half_extent[1] *
                    0.22f * std::sin(phase * 1.7f),
                swimmer.volume_center[2] +
                    std::max(0.0f, swimmer.volume_half_extent[2] - swimmer.radius) *
                    radius_scale * std::sin(phase)};
            if (containsBody(swimmer, candidate)) return candidate;
        }
    }
    if (containsBody(swimmer, candidate)) return candidate;

    if (config.has_starting_position) {
        const Point3 requested = candidate;
        const float search_extent = std::max(
            swimmer.volume_half_extent[0], swimmer.volume_half_extent[2]) * 2.0f;
        for (int ring = 1; ring <= 48; ++ring) {
            const float radius = search_extent * static_cast<float>(ring) / 48.0f;
            for (int step = 0; step < 64; ++step) {
                const float angle = 2.0f * kPi * static_cast<float>(step) / 64.0f;
                Point3 adjusted{
                    requested[0] + radius * std::cos(angle),
                    requested[1],
                    requested[2] + radius * std::sin(angle)};
                if (!containsBody(swimmer, adjusted)) continue;
                warnings_.push_back(
                    "Aquarium position for " + swimmer.actor.id +
                    " intersected glass or an obstacle; adjusted locally to [" +
                    std::to_string(adjusted[0]) + ", " +
                    std::to_string(adjusted[1]) + ", " +
                    std::to_string(adjusted[2]) + "]");
                return adjusted;
            }
        }
    }

    std::uniform_real_distribution<float> x_pick(
        swimmer.volume_center[0] - swimmer.volume_half_extent[0],
        swimmer.volume_center[0] + swimmer.volume_half_extent[0]);
    const float minimum_y = vertical[0] - swimmer.lower_extent;
    const float maximum_y = swimmer.floor_navigation
        ? minimum_y
        : vertical[1] - swimmer.upper_extent;
    std::uniform_real_distribution<float> y_pick(minimum_y, maximum_y);
    std::uniform_real_distribution<float> z_pick(
        swimmer.volume_center[2] - swimmer.volume_half_extent[2],
        swimmer.volume_center[2] + swimmer.volume_half_extent[2]);
    for (int attempt = 0; attempt < 240; ++attempt) {
        Point3 sampled{x_pick(swimmer.rng), y_pick(swimmer.rng), z_pick(swimmer.rng)};
        if (containsBody(swimmer, sampled)) return sampled;
    }
    warnings_.push_back(
        "Aquarium start position for " + swimmer.actor.species +
        " did not fit its rendered bounds; using the navigation fallback");
    return fallbackSpawn(swimmer.navigation);
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
    std::uniform_int_distribution<std::size_t> layer_pick(0, swimmer.navigation.layers.size() - 1U);
    for (int attempt = 0; attempt < 160; ++attempt) {
        const SwimVolumeLayer& layer = swimmer.navigation.layers[layer_pick(swimmer.rng)];
        float min_x = std::numeric_limits<float>::max();
        float max_x = std::numeric_limits<float>::lowest();
        float min_z = std::numeric_limits<float>::max();
        float max_z = std::numeric_limits<float>::lowest();
        for (const PolygonWithHoles& polygon : layer.polygons) {
            for (const Point2& point : polygon.front()) {
                min_x = std::min(min_x, point[0]); max_x = std::max(max_x, point[0]);
                min_z = std::min(min_z, point[1]); max_z = std::max(max_z, point[1]);
            }
        }
        const float vertical_clearance = std::min(swimmer.radius, (layer.y_top - layer.y_bottom) * 0.2f);
        std::uniform_real_distribution<float> x_pick(min_x, max_x);
        const float target_y = swimmer.floor_navigation
            ? swimmer.local_position[1]
            : std::uniform_real_distribution<float>(
                layer.y_bottom + vertical_clearance, layer.y_top - vertical_clearance)(swimmer.rng);
        std::uniform_real_distribution<float> z_pick(min_z, max_z);
        const Point3 candidate{x_pick(swimmer.rng), target_y, z_pick(swimmer.rng)};
        if (containsBody(swimmer, candidate) &&
            segmentNavigable(swimmer, swimmer.local_position, candidate)) {
            swimmer.local_target = candidate;
            return true;
        }
    }
    swimmer.local_target = swimmer.local_position;
    return false;
}

void AquariumSimulation::moveTowardTarget(Swimmer& swimmer, Point3 target, float dt) {
    const float dx = target[0] - swimmer.local_position[0];
    const float dy = target[1] - swimmer.local_position[1];
    const float dz = target[2] - swimmer.local_position[2];
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (distance <= 0.0001f) return;
    const float step = std::min(distance, swimmer.speed * dt);
    Point3 candidate{
        swimmer.local_position[0] + dx / distance * step,
        swimmer.local_position[1] + dy / distance * step,
        swimmer.local_position[2] + dz / distance * step};
    if (!containsBody(swimmer, candidate) ||
        !segmentNavigable(swimmer, swimmer.local_position, candidate)) return;
    swimmer.local_position = candidate;
    const float target_yaw = std::atan2(dx, dz) * 180.0f / kPi + swimmer.tank.yaw_degrees;
    const float yaw_delta = wrapDegrees(target_yaw - swimmer.actor.world_yaw_degrees);
    const float max_turn = swimmer.turn_speed * dt;
    swimmer.actor.world_yaw_degrees = wrapDegrees(
        swimmer.actor.world_yaw_degrees + std::clamp(yaw_delta, -max_turn, max_turn));
}

void AquariumSimulation::updateSchool(Swimmer& swimmer, float dt) {
    swimmer.school_elapsed += dt;
    const float orbit_x = std::max(
        0.05f, (swimmer.volume_half_extent[0] - swimmer.radius) * 0.62f);
    const float orbit_z = std::max(
        0.05f, (swimmer.volume_half_extent[2] - swimmer.radius) * 0.62f);
    const float mean_radius = std::max(0.1f, (orbit_x + orbit_z) * 0.5f);
    const float angle = swimmer.school_phase +
        swimmer.school_elapsed * swimmer.speed / mean_radius;
    Point3 target{
        swimmer.volume_center[0] + orbit_x * std::cos(angle),
        swimmer.volume_center[1] +
            std::max(0.0f, swimmer.volume_half_extent[1] -
                std::max(-swimmer.lower_extent, swimmer.upper_extent)) *
                0.34f * std::sin(angle * 1.7f + swimmer.school_phase * 0.37f),
        swimmer.volume_center[2] + orbit_z * std::sin(angle)};

    // Reusable local separation: schoolmates keep the authored formation but
    // repel inside their combined rendered clearance, preventing intersections.
    float repel_x = 0.0f;
    float repel_y = 0.0f;
    float repel_z = 0.0f;
    for (const Swimmer& other : swimmers_) {
        if (&other == &swimmer || other.school_id != swimmer.school_id) continue;
        const float dx = swimmer.local_position[0] - other.local_position[0];
        const float dy = swimmer.local_position[1] - other.local_position[1];
        const float dz = swimmer.local_position[2] - other.local_position[2];
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float clearance = (swimmer.radius + other.radius) * 1.15f;
        if (distance <= 0.0001f || distance >= clearance) continue;
        const float strength = (clearance - distance) / clearance;
        repel_x += dx / distance * strength;
        repel_y += dy / distance * strength * 0.35f;
        repel_z += dz / distance * strength;
    }
    Point3 separated{
        target[0] + repel_x * swimmer.radius,
        target[1] + repel_y * swimmer.radius,
        target[2] + repel_z * swimmer.radius};
    if (containsBody(swimmer, separated)) target = separated;
    moveTowardTarget(swimmer, target, dt);
}

void AquariumSimulation::syncActor(Swimmer& swimmer) {
    swimmer.actor.world_position = toWorld(
        swimmer.tank, swimmer.navigation.export_units_per_meter, swimmer.local_position);
    if (swimmer.actor_index < actors_.size()) actors_[swimmer.actor_index] = swimmer.actor;
}

void AquariumSimulation::update(double dt_seconds) {
    const float dt = static_cast<float>(std::clamp(dt_seconds, 0.0, 0.1));
    for (Swimmer& swimmer : swimmers_) {
        if (swimmer.behavior == Swimmer::Behavior::Stationary) {
            swimmer.actor.animation_time_seconds += dt_seconds;
            syncActor(swimmer);
            continue;
        }
        if (swimmer.behavior == Swimmer::Behavior::School) {
            updateSchool(swimmer, dt);
            swimmer.actor.animation_time_seconds += dt_seconds;
            syncActor(swimmer);
            continue;
        }
        const float dx = swimmer.local_target[0] - swimmer.local_position[0];
        const float dy = swimmer.local_target[1] - swimmer.local_position[1];
        const float dz = swimmer.local_target[2] - swimmer.local_position[2];
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance < std::max(0.04f, swimmer.speed * dt * 1.5f)) {
            chooseTarget(swimmer);
            continue;
        }
        moveTowardTarget(swimmer, swimmer.local_target, dt);
        swimmer.actor.animation_time_seconds += dt_seconds;
        syncActor(swimmer);
    }
}

} // namespace pr::gameplay::world3d::aquarium
