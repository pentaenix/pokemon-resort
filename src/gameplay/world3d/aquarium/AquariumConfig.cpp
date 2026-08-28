#include "gameplay/world3d/aquarium/AquariumConfig.hpp"

#include "core/config/Json.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

namespace pr::gameplay::world3d::aquarium {
namespace {

std::string stringOr(const JsonValue* value, std::string fallback = {}) {
    return value && value->isString() ? value->asString() : std::move(fallback);
}

double numberOr(const JsonValue* value, double fallback) {
    if (!value) return fallback;
    if (value->isNumber()) return value->asNumber();
    if (!value->isString()) return fallback;
    const std::string& text = value->asString();
    if (text.empty()) return fallback;
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(text.c_str(), &end);
    return errno == 0 && end == text.c_str() + text.size() ? parsed : fallback;
}

bool positionNumber(const JsonValue* value, float& out) {
    if (!value) return false;
    double parsed = 0.0;
    if (value->isNumber()) {
        parsed = value->asNumber();
    } else if (value->isString()) {
        const std::string& text = value->asString();
        if (text.empty()) return false;
        char* end = nullptr;
        errno = 0;
        parsed = std::strtod(text.c_str(), &end);
        if (errno != 0 || end != text.c_str() + text.size()) return false;
    } else {
        return false;
    }
    if (!std::isfinite(parsed)) return false;
    out = static_cast<float>(parsed);
    return std::isfinite(out);
}

bool parsePosition(const JsonValue* value, std::array<float, 3>& out) {
    if (!value) return false;
    if (value->isArray() && value->asArray().size() >= 3U) {
        for (std::size_t axis = 0; axis < 3U; ++axis) {
            if (!positionNumber(&value->asArray()[axis], out[axis])) {
                throw std::runtime_error(
                    "startingPositionMeters must contain three finite numbers");
            }
        }
        return true;
    }
    if (!value->isObject() || !positionNumber(value->get("x"), out[0]) ||
        !positionNumber(value->get("y"), out[1]) ||
        !positionNumber(value->get("z"), out[2])) {
        throw std::runtime_error(
            "positionMeters must contain finite numeric x, y, and z fields");
    }
    return true;
}

AquariumPokemonPresentationConfig parsePokemonPresentation(
    const JsonValue* value,
    AquariumPokemonPresentationConfig out = {}) {
    if (!value || !value->isObject()) return out;
    out.brightness = std::clamp(
        static_cast<float>(numberOr(value->get("brightness"), out.brightness)), 0.1f, 3.0f);
    out.pokemon_brightness = std::clamp(static_cast<float>(numberOr(
        value->get("pokemonBrightness"), out.pokemon_brightness)), 0.1f, 3.0f);
    out.saturation = std::clamp(
        static_cast<float>(numberOr(value->get("saturation"), out.saturation)), 0.0f, 2.0f);
    out.contrast = std::clamp(
        static_cast<float>(numberOr(value->get("contrast"), out.contrast)), 0.0f, 2.0f);
    out.ambient = std::clamp(
        static_cast<float>(numberOr(value->get("ambient"), out.ambient)), 0.0f, 1.5f);
    out.directional = std::clamp(
        static_cast<float>(numberOr(value->get("directional"), out.directional)), 0.0f, 1.5f);
    out.form_shadow = std::clamp(
        static_cast<float>(numberOr(value->get("formShadow"), out.form_shadow)), 0.0f, 1.0f);
    const auto parse_vector = [&](const char* key, std::array<float, 3>& target, float low, float high) {
        const JsonValue* vector = value->get(key);
        if (!vector || !vector->isArray() || vector->asArray().size() < 3U) return;
        for (std::size_t axis = 0; axis < 3U; ++axis) {
            target[axis] = std::clamp(static_cast<float>(
                numberOr(&vector->asArray()[axis], target[axis])), low, high);
        }
    };
    parse_vector("lightDirection", out.light_direction, -1.0f, 1.0f);
    parse_vector("tint", out.tint, 0.0f, 3.0f);
    return out;
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
    out.pitch_degrees = std::clamp(
        static_cast<float>(numberOr(value.get("pitchDegrees"), out.pitch_degrees)),
        -360.0f, 360.0f);
    const JsonValue* start = value.get("positionMeters");
    if (!start) start = value.get("startingPositionMeters");
    if (parsePosition(start, out.starting_position_meters)) {
        out.has_starting_position = true;
    }
    out.vertical_anchor = stringOr(value.get("verticalAnchor"), out.vertical_anchor);
    out.movement_plane = stringOr(value.get("movementPlane"), out.movement_plane);
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
    out.return_smooth = std::clamp(
        static_cast<float>(numberOr(value->get("returnSmooth"), out.return_smooth)),
        -1.0f, 10000.0f);
    out.interaction_reach_tiles = std::clamp(
        static_cast<float>(numberOr(
            value->get("interactionReachTiles"), out.interaction_reach_tiles)),
        0.25f, 8.0f);
    if (const JsonValue* inspection = value->get("inspectionView");
        inspection && inspection->isObject()) {
        out.has_framed_inspection_view = true;
        out.inspection_behind_player_tiles = std::clamp(
            static_cast<float>(numberOr(
                inspection->get("behindPlayerTiles"), out.inspection_behind_player_tiles)),
            1.0f, 64.0f);
        out.inspection_front_height_tiles = std::clamp(
            static_cast<float>(numberOr(
                inspection->get("frontHeightTiles"), out.inspection_front_height_tiles)),
            0.1f, 64.0f);
        out.inspection_side_height_tiles = std::clamp(
            static_cast<float>(numberOr(
                inspection->get("sideHeightTiles"), out.inspection_side_height_tiles)),
            0.1f, 64.0f);
    }
    if (const JsonValue* focused = value->get("focusedView"); focused && focused->isObject()) {
        out.focused_standoff_tiles = std::clamp(
            static_cast<float>(numberOr(
                focused->get("standoffTiles"), out.focused_standoff_tiles)),
            0.25f, 64.0f);
        out.focused_front_height_tiles = std::clamp(
            static_cast<float>(numberOr(
                focused->get("frontHeightTiles"), out.focused_front_height_tiles)),
            0.1f, 64.0f);
        out.focused_side_height_tiles = std::clamp(
            static_cast<float>(numberOr(
                focused->get("sideHeightTiles"), out.focused_side_height_tiles)),
            0.1f, 64.0f);
        out.focused_front_pitch_degrees = std::clamp(
            static_cast<float>(numberOr(
                focused->get("frontPitchDegrees"), out.focused_front_pitch_degrees)),
            -89.0f, 89.0f);
        out.focused_side_pitch_degrees = std::clamp(
            static_cast<float>(numberOr(
                focused->get("sidePitchDegrees"), out.focused_side_pitch_degrees)),
            -89.0f, 89.0f);
        out.focused_near_clip = std::clamp(
            static_cast<float>(numberOr(
                focused->get("nearClip"), out.focused_near_clip)),
            0.01f, 150.0f);
        out.focused_wall_clip_radius_tiles = std::clamp(
            static_cast<float>(numberOr(
                focused->get("wallClipRadiusTiles"),
                out.focused_wall_clip_radius_tiles)),
            0.0f, 32.0f);
    }
    return out;
}

} // namespace

AquariumCatalog loadAquariumCatalog(const std::string& project_root, std::string* error) {
    AquariumCatalog out;
    const std::filesystem::path path =
        std::filesystem::path(project_root) / "config/gameplay/world3d/aquariums.json";
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        if (error) *error = "Aquarium config does not exist: " + path.string();
        return out;
    }

    JsonValue root;
    try {
        root = parseJsonFile(path.string());
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return out;
    }
    const JsonValue* maps = root.get("maps");
    if (!maps || !maps->isArray()) {
        if (error) *error = "Aquarium config requires a maps array";
        return out;
    }
    try {
        out.pokemon_scale = std::clamp(
            static_cast<float>(numberOr(root.get("pokemonScale"), out.pokemon_scale)),
            0.0001f, 10.0f);
        out.pokemon_presentation = parsePokemonPresentation(
            root.get("pokemonPresentation"), out.pokemon_presentation);
        for (const JsonValue& map_value : maps->asArray()) {
            if (!map_value.isObject()) continue;
            AquariumMapConfig map;
            map.map_id = stringOr(map_value.get("mapId"));
            map.pokemon_scale = out.pokemon_scale;
            map.pokemon_presentation = parsePokemonPresentation(
                map_value.get("pokemonPresentation"), out.pokemon_presentation);
            const JsonValue* tanks = map_value.get("tanks");
            if (tanks && tanks->isArray()) {
                for (const JsonValue& tank_value : tanks->asArray()) {
                    if (!tank_value.isObject()) continue;
                    AquariumTankConfig tank;
                    tank.placement_id = stringOr(tank_value.get("placementId"));
                    tank.navigation_path = stringOr(tank_value.get("navigation"));
                    tank.seed = static_cast<std::uint32_t>(std::max(
                        0.0, numberOr(tank_value.get("seed"), static_cast<double>(tank.seed))));
                    tank.inspection_camera =
                        parseInspectionCamera(tank_value.get("inspectionCamera"));
                    const JsonValue* pokemon = tank_value.get("pokemon");
                    if (pokemon && pokemon->isArray()) {
                        for (const JsonValue& pokemon_value : pokemon->asArray()) {
                            if (!pokemon_value.isObject()) continue;
                            AquariumPokemonConfig parsed = parsePokemon(pokemon_value);
                            if (!parsed.species.empty()) {
                                tank.pokemon.push_back(std::move(parsed));
                            }
                        }
                    }
                    if (!tank.placement_id.empty() && !tank.navigation_path.empty()) {
                        map.tanks.push_back(std::move(tank));
                    }
                }
            }
            if (!map.map_id.empty()) out.maps.push_back(std::move(map));
        }
    } catch (const std::exception& exception) {
        if (error) *error = "Invalid aquarium config: " + std::string(exception.what());
        return {};
    }
    if (error) error->clear();
    return out;
}

const AquariumMapConfig* aquariumMapConfig(const AquariumCatalog& catalog, const std::string& map_id) {
    const auto found = std::find_if(catalog.maps.begin(), catalog.maps.end(), [&](const auto& map) {
        return map.map_id == map_id;
    });
    return found == catalog.maps.end() ? nullptr : &*found;
}

} // namespace pr::gameplay::world3d::aquarium
