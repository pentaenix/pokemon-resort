#include "gameplay/world3d/aquarium/AquariumConfig.hpp"

#include "core/config/Json.hpp"

#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace pr::gameplay::world3d::aquarium {
namespace {

std::string stringOr(const JsonValue* value, std::string fallback = {}) {
    return value && value->isString() ? value->asString() : std::move(fallback);
}

double numberOr(const JsonValue* value, double fallback) {
    return value && value->isNumber() ? value->asNumber() : fallback;
}

AquariumPokemonConfig parsePokemon(const JsonValue& value) {
    AquariumPokemonConfig out;
    out.id = stringOr(value.get("id"));
    out.species = stringOr(value.get("species"));
    out.form = stringOr(value.get("form"));
    out.animation = stringOr(value.get("animation"), out.animation);
    out.count = std::clamp(static_cast<int>(numberOr(value.get("count"), out.count)), 1, 32);
    out.size_multiplier = std::clamp(
        static_cast<float>(numberOr(value.get("sizeMultiplier"), out.size_multiplier)),
        0.0001f, 10.0f);
    out.speed_meters_per_second = std::clamp(
        static_cast<float>(numberOr(value.get("speedMetersPerSecond"), out.speed_meters_per_second)),
        0.0f, 20.0f);
    out.turn_degrees_per_second = std::clamp(
        static_cast<float>(numberOr(value.get("turnDegreesPerSecond"), out.turn_degrees_per_second)),
        1.0f, 1440.0f);
    out.body_radius_meters = std::clamp(
        static_cast<float>(numberOr(value.get("bodyRadiusMeters"), out.body_radius_meters)),
        0.0f, 10.0f);
    if (const JsonValue* start = value.get("startingPositionMeters");
        start && start->isArray() && start->asArray().size() >= 3U) {
        for (std::size_t axis = 0; axis < 3U; ++axis) {
            out.starting_position_meters[axis] =
                static_cast<float>(numberOr(&start->asArray()[axis], 0.0));
        }
        out.has_starting_position = true;
    }
    out.vertical_anchor = stringOr(value.get("verticalAnchor"), out.vertical_anchor);
    out.behavior = stringOr(value.get("behavior"));
    return out;
}

AquariumInspectionCameraConfig parseInspectionCamera(const JsonValue* value) {
    AquariumInspectionCameraConfig out;
    if (!value || !value->isObject()) return out;
    if (const JsonValue* enabled = value->get("enabled"); enabled && enabled->isBool()) {
        out.enabled = enabled->asBool();
    }
    if (const JsonValue* offset = value->get("positionOffsetTiles");
        offset && offset->isArray() && offset->asArray().size() >= 3U) {
        out.side_tiles = static_cast<float>(numberOr(&offset->asArray()[0], out.side_tiles));
        out.lower_tiles = static_cast<float>(numberOr(&offset->asArray()[1], out.lower_tiles));
        out.closer_tiles = static_cast<float>(numberOr(&offset->asArray()[2], out.closer_tiles));
    }
    if (const JsonValue* look_at = value->get("lookAtOffsetMeters");
        look_at && look_at->isArray() && look_at->asArray().size() >= 3U) {
        out.look_at_x_meters = static_cast<float>(numberOr(&look_at->asArray()[0], out.look_at_x_meters));
        out.look_at_y_meters = static_cast<float>(numberOr(&look_at->asArray()[1], out.look_at_y_meters));
        out.look_at_z_meters = static_cast<float>(numberOr(&look_at->asArray()[2], out.look_at_z_meters));
    }
    out.smooth = std::clamp(
        static_cast<float>(numberOr(value->get("smooth"), out.smooth)), -1.0f, 10000.0f);
    out.interaction_reach_tiles = std::clamp(
        static_cast<float>(numberOr(
            value->get("interactionReachTiles"), out.interaction_reach_tiles)),
        0.25f, 8.0f);
    return out;
}

} // namespace

AquariumCatalog loadAquariumCatalog(const std::string& project_root) {
    AquariumCatalog out;
    const std::filesystem::path path =
        std::filesystem::path(project_root) / "config/gameplay/world3d/aquariums.json";
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return out;

    JsonValue root;
    try {
        root = parseJsonFile(path.string());
    } catch (const std::exception&) {
        return out;
    }
    const JsonValue* maps = root.get("maps");
    if (!maps || !maps->isArray()) return out;
    out.pokemon_scale = std::clamp(
        static_cast<float>(numberOr(root.get("pokemonScale"), out.pokemon_scale)),
        0.0001f, 10.0f);
    for (const JsonValue& map_value : maps->asArray()) {
        if (!map_value.isObject()) continue;
        AquariumMapConfig map;
        map.map_id = stringOr(map_value.get("mapId"));
        map.pokemon_scale = out.pokemon_scale;
        const JsonValue* tanks = map_value.get("tanks");
        if (tanks && tanks->isArray()) {
            for (const JsonValue& tank_value : tanks->asArray()) {
                if (!tank_value.isObject()) continue;
                AquariumTankConfig tank;
                tank.placement_id = stringOr(tank_value.get("placementId"));
                tank.navigation_path = stringOr(tank_value.get("navigation"));
                tank.seed = static_cast<std::uint32_t>(std::max(
                    0.0, numberOr(tank_value.get("seed"), static_cast<double>(tank.seed))));
                tank.inspection_camera = parseInspectionCamera(tank_value.get("inspectionCamera"));
                const JsonValue* pokemon = tank_value.get("pokemon");
                if (pokemon && pokemon->isArray()) {
                    for (const JsonValue& pokemon_value : pokemon->asArray()) {
                        if (!pokemon_value.isObject()) continue;
                        AquariumPokemonConfig parsed = parsePokemon(pokemon_value);
                        if (!parsed.species.empty()) tank.pokemon.push_back(std::move(parsed));
                    }
                }
                if (!tank.placement_id.empty() && !tank.navigation_path.empty()) {
                    map.tanks.push_back(std::move(tank));
                }
            }
        }
        if (!map.map_id.empty()) out.maps.push_back(std::move(map));
    }
    return out;
}

const AquariumMapConfig* aquariumMapConfig(const AquariumCatalog& catalog, const std::string& map_id) {
    const auto found = std::find_if(catalog.maps.begin(), catalog.maps.end(), [&](const auto& map) {
        return map.map_id == map_id;
    });
    return found == catalog.maps.end() ? nullptr : &*found;
}

} // namespace pr::gameplay::world3d::aquarium
