#include "gameplay/attend/AttendSceneConfig.hpp"

#include "core/config/Json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace pr::gameplay::attend {

namespace fs = std::filesystem;

namespace {

const JsonValue* child(const JsonValue* obj, const char* key) {
    return obj && obj->isObject() ? obj->get(key) : nullptr;
}

std::string strOr(const JsonValue* value, const std::string& fallback) {
    return value && value->isString() ? value->asString() : fallback;
}

float numOr(const JsonValue* value, float fallback) {
    return value && value->isNumber() ? static_cast<float>(value->asNumber()) : fallback;
}

int intOr(const JsonValue* value, int fallback) {
    return value && value->isNumber() ? static_cast<int>(std::round(value->asNumber())) : fallback;
}

bool boolOr(const JsonValue* value, bool fallback) {
    return value && value->isBool() ? value->asBool() : fallback;
}

std::string resolvePath(const std::string& root, const std::string& path);
Color3 parseColor(const JsonValue* value, Color3 fallback);
Color4 parseColor4(const JsonValue* value, Color4 fallback);

std::string replaceAll(std::string text, const std::string& needle, const std::string& replacement) {
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        text.replace(pos, needle.size(), replacement);
        pos += replacement.size();
    }
    return text;
}

std::string normalizeSpeciesId(std::string text) {
    for (char& c : text) {
        if (c == ' ' || c == '-') {
            c = '_';
        } else {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return text;
}

std::string pokemonIdFromModelStem(std::string stem) {
    const std::string lowered = normalizeSpeciesId(stem);
    if (lowered.size() > 10 &&
        lowered.rfind("pm", 0) == 0 &&
        std::isdigit(static_cast<unsigned char>(lowered[2])) &&
        std::isdigit(static_cast<unsigned char>(lowered[3])) &&
        std::isdigit(static_cast<unsigned char>(lowered[4])) &&
        std::isdigit(static_cast<unsigned char>(lowered[5])) &&
        lowered[6] == '_') {
        const std::size_t species_name_sep = lowered.find('_', 7);
        if (species_name_sep != std::string::npos && species_name_sep + 1 < stem.size()) {
            stem = stem.substr(species_name_sep + 1);
        }
    }
    return normalizeSpeciesId(stem);
}

std::string findModelFileForSpecies(
    const std::string& project_root,
    const std::string& model_dir,
    const std::string& species_id) {
    if (model_dir.empty() || species_id.empty()) return {};
    const fs::path dir = fs::path(resolvePath(project_root, model_dir));
    std::error_code ec;
    if (!fs::exists(dir, ec)) return {};
    for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
        if (ec || !entry.is_regular_file()) continue;
        if (entry.path().extension() != ".glb") continue;
        if (pokemonIdFromModelStem(entry.path().stem().string()) == species_id) {
            return entry.path().string();
        }
    }
    return {};
}

std::string dex4(int dex_number) {
    if (dex_number <= 0) return {};
    std::ostringstream out;
    out << std::setw(4) << std::setfill('0') << dex_number;
    return out.str();
}

const JsonValue* catalogEntry(const JsonValue* catalog, const std::string& id) {
    if (!catalog || id.empty()) return nullptr;
    if (catalog->isObject()) {
        return catalog->get(id);
    }
    if (catalog->isArray()) {
        for (const JsonValue& entry : catalog->asArray()) {
            if (entry.isObject() && strOr(child(&entry, "id"), "") == id) {
                return &entry;
            }
        }
    }
    return nullptr;
}

std::vector<std::string> stringArrayOr(const JsonValue* value, std::vector<std::string> fallback) {
    if (!value || !value->isArray()) return fallback;
    std::vector<std::string> out;
    for (const JsonValue& item : value->asArray()) {
        if (item.isString()) out.push_back(item.asString());
    }
    return out.empty() ? fallback : out;
}

void parseAxis(const JsonValue* value, float (&out)[3]) {
    if (!value || !value->isArray()) return;
    const auto& arr = value->asArray();
    for (std::size_t i = 0; i < 3 && i < arr.size(); ++i) {
        if (arr[i].isNumber()) out[i] = static_cast<float>(arr[i].asNumber());
    }
}

std::vector<WeatherModeConfig> parseWeatherModes(const JsonValue* value, std::vector<WeatherModeConfig> fallback) {
    if (!value || !value->isArray()) return fallback;
    std::vector<WeatherModeConfig> out;
    for (const JsonValue& item : value->asArray()) {
        if (!item.isObject()) continue;
        WeatherModeConfig mode;
        mode.id = strOr(child(&item, "id"), mode.id);
        mode.label = strOr(child(&item, "label"), mode.id);
        mode.visible_material_substrings =
            stringArrayOr(child(&item, "visibleMaterialSubstrings"), mode.visible_material_substrings);
        out.push_back(std::move(mode));
    }
    return out.empty() ? fallback : out;
}

void parseInteractionAdapter(const JsonValue* adapter, AttendInteractionAdapterConfig& out) {
    if (!adapter) return;
    out.source = strOr(child(adapter, "source"), out.source);
    out.head_node_names = stringArrayOr(child(adapter, "headNodeNames"), out.head_node_names);
    out.eyelid_node_substrings = stringArrayOr(child(adapter, "eyelidNodeSubstrings"), out.eyelid_node_substrings);
    out.eye_close_animation = strOr(child(adapter, "eyeCloseAnimation"), out.eye_close_animation);
    if (const JsonValue* slots = child(adapter, "semanticAnimationSlots"); slots && slots->isObject()) {
        for (const auto& [name, value] : slots->asObject()) {
            out.semantic_animation_slots[name] = stringArrayOr(&value, {});
        }
    }
    if (const JsonValue* combos = child(adapter, "reactionCombos"); combos && combos->isObject()) {
        for (const auto& [name, value] : combos->asObject()) {
            if (!value.isObject()) continue;
            AttendInteractionAdapterConfig::ReactionCombo combo;
            combo.animation_semantic = strOr(child(&value, "animation"), combo.animation_semantic);
            combo.eye_expression = strOr(child(&value, "eye"), combo.eye_expression);
            combo.mouth_expression = strOr(child(&value, "mouth"), combo.mouth_expression);
            combo.duration_seconds = std::clamp(numOr(child(&value, "durationSeconds"), combo.duration_seconds), 0.05f, 10.0f);
            combo.fade_in_seconds = std::clamp(numOr(child(&value, "fadeInSeconds"), combo.fade_in_seconds), 0.0f, 5.0f);
            combo.fade_out_seconds = std::clamp(numOr(child(&value, "fadeOutSeconds"), combo.fade_out_seconds), 0.0f, 5.0f);
            combo.eye_linger_seconds = std::clamp(numOr(child(&value, "eyeLingerSeconds"), combo.eye_linger_seconds), 0.0f, 5.0f);
            combo.min_pet_seconds = std::clamp(numOr(child(&value, "minPetSeconds"), combo.min_pet_seconds), 0.0f, 30.0f);
            combo.large_pokemon_height = std::max(0.0f, numOr(child(&value, "largePokemonHeight"), combo.large_pokemon_height));
            combo.large_pokemon_fade_scale = std::clamp(numOr(child(&value, "largePokemonFadeScale"), combo.large_pokemon_fade_scale), 0.1f, 5.0f);
            combo.ready_animation_semantic = strOr(child(&value, "readyAnimation"), combo.ready_animation_semantic);
            combo.ready_eye_expression = strOr(child(&value, "readyEye"), combo.ready_eye_expression);
            combo.ready_mouth_expression = strOr(child(&value, "readyMouth"), combo.ready_mouth_expression);
            combo.ready_weight = std::clamp(numOr(child(&value, "readyWeight"), combo.ready_weight), 0.0f, 1.0f);
            combo.ready_fade_seconds = std::clamp(numOr(child(&value, "readyFadeSeconds"), combo.ready_fade_seconds), 0.01f, 5.0f);
            out.reaction_combos[name] = std::move(combo);
        }
    }
    if (const JsonValue* frames = child(adapter, "eyeExpressionFrames"); frames && frames->isObject()) {
        for (const auto& [name, value] : frames->asObject()) {
            if (value.isNumber()) {
                out.eye_expression_frames[name] =
                    std::max(0, static_cast<int>(std::round(value.asNumber())));
            }
        }
    }
    if (const JsonValue* frames = child(adapter, "mouthExpressionFrames"); frames && frames->isObject()) {
        for (const auto& [name, value] : frames->asObject()) {
            if (value.isNumber()) {
                out.mouth_expression_frames[name] =
                    std::max(0, static_cast<int>(std::round(value.asNumber())));
            }
        }
    }
    out.eye_close_time_seconds = numOr(child(adapter, "eyeCloseTimeSeconds"), out.eye_close_time_seconds);
    out.pet_eye_close_delay_seconds =
        std::clamp(numOr(child(adapter, "petEyeCloseDelaySeconds"), out.pet_eye_close_delay_seconds), 0.0f, 2.0f);
    out.pet_eye_close_cooldown_seconds =
        std::clamp(numOr(child(adapter, "petEyeCloseCooldownSeconds"), out.pet_eye_close_cooldown_seconds), 0.0f, 3.0f);
    out.spontaneous_blink_min_seconds =
        std::max(0.5f, numOr(child(adapter, "spontaneousBlinkMinSeconds"), out.spontaneous_blink_min_seconds));
    out.spontaneous_blink_max_seconds =
        std::max(out.spontaneous_blink_min_seconds,
                 numOr(child(adapter, "spontaneousBlinkMaxSeconds"), out.spontaneous_blink_max_seconds));
    out.spontaneous_blink_duration_seconds =
        std::max(0.05f,
                 numOr(child(adapter, "spontaneousBlinkDurationSeconds"),
                       out.spontaneous_blink_duration_seconds));
    out.head_look_strength =
        std::clamp(numOr(child(adapter, "headLookStrength"), out.head_look_strength), 0.0f, 4.0f);
    parseAxis(child(adapter, "headYawAxis"), out.head_yaw_axis);
    parseAxis(child(adapter, "headPitchAxis"), out.head_pitch_axis);
}

void parsePokemon(const JsonValue* pokemon, const std::string& project_root, PokemonAttendModelConfig& out) {
    if (!pokemon) return;
    out.id = strOr(child(pokemon, "id"), out.id);
    out.model_path = resolvePath(project_root, strOr(child(pokemon, "model"), out.model_path));
    out.animation_name = strOr(child(pokemon, "animation"), out.animation_name);
    out.yaw_degrees = numOr(child(pokemon, "yawDegrees"), out.yaw_degrees);
    out.pitch_degrees = numOr(child(pokemon, "pitchDegrees"), out.pitch_degrees);
    out.scale = numOr(child(pokemon, "scale"), out.scale);
    if (const JsonValue* pos = child(pokemon, "position")) {
        out.x = numOr(child(pos, "x"), out.x);
        out.y = numOr(child(pos, "y"), out.y);
        out.z = numOr(child(pos, "z"), out.z);
    }
    if (const JsonValue* idle = child(pokemon, "idleMotion")) {
        out.idle_motion.enabled = boolOr(child(idle, "enabled"), out.idle_motion.enabled);
        out.idle_motion.bob_amplitude = numOr(child(idle, "bobAmplitude"), out.idle_motion.bob_amplitude);
        out.idle_motion.bob_seconds = numOr(child(idle, "bobSeconds"), out.idle_motion.bob_seconds);
        out.idle_motion.rock_degrees = numOr(child(idle, "rockDegrees"), out.idle_motion.rock_degrees);
        out.idle_motion.rock_seconds = numOr(child(idle, "rockSeconds"), out.idle_motion.rock_seconds);
    }
}

void parsePokemonProviderProfile(
    const JsonValue* defaults,
    const JsonValue* profile,
    const std::string& active_id,
    const std::string& project_root,
    PokemonAttendModelConfig& pokemon,
    AttendInteractionAdapterConfig& adapter) {
    if (!defaults && !profile) return;
    const std::string id = normalizeSpeciesId(strOr(child(profile, "id"), active_id.empty() ? pokemon.id : active_id));
    const int dex_number = intOr(child(profile, "dexNumber"), intOr(child(defaults, "dexNumber"), 0));
    const std::string dex = dex4(dex_number);

    const auto patternString = [&](const char* key) {
        return strOr(child(profile, key), strOr(child(defaults, key), ""));
    };
    const auto expand = [&](std::string value) {
        if (value.find("{dex4}") != std::string::npos && dex.empty()) return std::string{};
        value = replaceAll(value, "{id}", id);
        value = replaceAll(value, "{dex4}", dex);
        return value;
    };

    pokemon.id = id;
    const std::string model_path = strOr(child(profile, "model"), "");
    if (!model_path.empty()) {
        pokemon.model_path = resolvePath(project_root, model_path);
    } else {
        const std::string model_dir = strOr(child(defaults, "modelDirectory"), "");
        const std::string model_file = expand(patternString("modelFilePattern"));
        if (!model_dir.empty() && !model_file.empty()) {
            pokemon.model_path = resolvePath(project_root, (fs::path(model_dir) / model_file).string());
        }
        std::error_code ec;
        if (!model_dir.empty() && !fs::exists(pokemon.model_path, ec)) {
            const std::string discovered = findModelFileForSpecies(project_root, model_dir, id);
            if (!discovered.empty()) {
                pokemon.model_path = discovered;
            }
        }
    }
    const std::string animation = strOr(child(profile, "animation"), "");
    if (!animation.empty()) {
        pokemon.animation_name = animation;
    } else {
        const std::string pattern = expand(patternString("animationPattern"));
        if (!pattern.empty()) pokemon.animation_name = pattern;
    }
    pokemon.yaw_degrees = numOr(child(defaults, "yawDegrees"), pokemon.yaw_degrees);
    pokemon.pitch_degrees = numOr(child(defaults, "pitchDegrees"), pokemon.pitch_degrees);
    pokemon.scale = numOr(child(defaults, "scale"), pokemon.scale);
    pokemon.yaw_degrees = numOr(child(profile, "yawDegrees"), pokemon.yaw_degrees);
    pokemon.pitch_degrees = numOr(child(profile, "pitchDegrees"), pokemon.pitch_degrees);
    pokemon.scale = numOr(child(profile, "scale"), pokemon.scale);

    parsePokemon(defaults, project_root, pokemon);
    parsePokemon(profile, project_root, pokemon);
    parseInteractionAdapter(child(defaults, "interactionAdapter"), adapter);
    parseInteractionAdapter(child(profile, "interactionAdapter"), adapter);
    if (adapter.eye_close_animation.empty()) {
        const std::string eye_close = expand(patternString("eyeCloseAnimationPattern"));
        if (!eye_close.empty()) adapter.eye_close_animation = eye_close;
    }
}

float hexChannel(char hi, char lo) {
    const auto v = [](char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + c - 'a';
        if (c >= 'A' && c <= 'F') return 10 + c - 'A';
        return 0;
    };
    return static_cast<float>((v(hi) << 4) | v(lo)) / 255.0f;
}

Color3 parseColor(const JsonValue* value, Color3 fallback) {
    if (value && value->isString()) {
        const std::string s = value->asString();
        if (s.size() == 7 && s[0] == '#') {
            return Color3{
                hexChannel(s[1], s[2]),
                hexChannel(s[3], s[4]),
                hexChannel(s[5], s[6])};
        }
    }
    if (value && value->isArray()) {
        const auto& arr = value->asArray();
        if (arr.size() >= 3) {
            return Color3{
                numOr(&arr[0], fallback.r),
                numOr(&arr[1], fallback.g),
                numOr(&arr[2], fallback.b)};
        }
    }
    return fallback;
}

Color4 parseColor4(const JsonValue* value, Color4 fallback) {
    if (value && value->isString()) {
        const std::string s = value->asString();
        if ((s.size() == 7 || s.size() == 9) && s[0] == '#') {
            return Color4{
                hexChannel(s[1], s[2]),
                hexChannel(s[3], s[4]),
                hexChannel(s[5], s[6]),
                s.size() == 9 ? hexChannel(s[7], s[8]) : fallback.a};
        }
    }
    if (value && value->isArray()) {
        const auto& arr = value->asArray();
        if (arr.size() >= 3) {
            return Color4{
                numOr(&arr[0], fallback.r),
                numOr(&arr[1], fallback.g),
                numOr(&arr[2], fallback.b),
                arr.size() >= 4 ? numOr(&arr[3], fallback.a) : fallback.a};
        }
    }
    return fallback;
}

void parseOverlayButton(const JsonValue* value, AttendOverlayButtonConfig& out) {
    if (!value || !value->isObject()) return;
    out.enabled = boolOr(child(value, "enabled"), out.enabled);
    out.anchor = strOr(child(value, "anchor"), out.anchor);
    out.label_prefix = strOr(child(value, "labelPrefix"), out.label_prefix);
    out.width = std::max(24, intOr(child(value, "width"), out.width));
    out.height = std::max(20, intOr(child(value, "height"), out.height));
    out.margin_x = std::max(0, intOr(child(value, "marginX"), out.margin_x));
    out.margin_y = std::max(0, intOr(child(value, "marginY"), out.margin_y));
    out.padding_x = std::max(0, intOr(child(value, "paddingX"), out.padding_x));
    out.corner_radius = std::max(0, intOr(child(value, "cornerRadius"), out.corner_radius));
    out.stroke_width = std::max(0, intOr(child(value, "strokeWidth"), out.stroke_width));
    out.font_size = std::max(8, intOr(child(value, "fontSize"), out.font_size));
    out.fill = parseColor4(child(value, "fill"), out.fill);
    out.stroke = parseColor4(child(value, "stroke"), out.stroke);
    out.text = parseColor4(child(value, "text"), out.text);
}

void parseUi(const JsonValue* value, AttendUiConfig& out) {
    if (!value || !value->isObject()) return;
    parseOverlayButton(child(value, "weatherButton"), out.weather_button);
    parseOverlayButton(child(value, "viewButton"), out.view_button);
    parseOverlayButton(child(value, "pokemonButton"), out.pokemon_button);
    parseOverlayButton(child(value, "textureVariantButton"), out.texture_variant_button);
    parseOverlayButton(child(value, "formVariantButton"), out.form_variant_button);
    if (const JsonValue* hand_cursor = child(value, "handCursor"); hand_cursor && hand_cursor->isObject()) {
        out.hand_cursor_scale = std::clamp(numOr(child(hand_cursor, "scale"), out.hand_cursor_scale), 0.5f, 4.0f);
        out.hand_cursor_hotspot_x_ratio =
            std::clamp(numOr(child(hand_cursor, "hotspotXRatio"), out.hand_cursor_hotspot_x_ratio), 0.0f, 1.0f);
        out.hand_cursor_hotspot_y_ratio =
            std::clamp(numOr(child(hand_cursor, "hotspotYRatio"), out.hand_cursor_hotspot_y_ratio), 0.0f, 1.0f);
        out.hand_cursor_pet_animation_speed =
            std::clamp(numOr(child(hand_cursor, "petAnimationSpeed"), out.hand_cursor_pet_animation_speed), 0.1f, 8.0f);
    }
}

std::vector<GradientStop> parseStops(const JsonValue* value, std::vector<GradientStop> fallback) {
    if (!value || !value->isArray()) {
        return fallback;
    }
    std::vector<GradientStop> stops;
    for (const JsonValue& item : value->asArray()) {
        if (!item.isObject()) continue;
        stops.push_back(GradientStop{
            std::clamp(numOr(child(&item, "at"), 0.0f), 0.0f, 1.0f),
            parseColor(child(&item, "color"), Color3{})});
    }
    if (stops.empty()) {
        return fallback;
    }
    std::sort(stops.begin(), stops.end(), [](const GradientStop& a, const GradientStop& b) {
        return a.at < b.at;
    });
    return stops;
}

std::string resolvePath(const std::string& root, const std::string& path) {
    if (path.empty()) return {};
    const fs::path p(path);
    return p.is_absolute() ? p.string() : (fs::path(root) / p).string();
}

void parseFloor(const JsonValue* floor, const std::string& project_root, AttendFloorConfig& out) {
    if (!floor) return;
    out.enabled = boolOr(child(floor, "enabled"), out.enabled);
    out.id = strOr(child(floor, "id"), out.id);
    out.placement_anchor = strOr(child(floor, "placementAnchor"), out.placement_anchor);
    out.model_path = resolvePath(project_root, strOr(child(floor, "model"), out.model_path));
    out.animation_name = strOr(child(floor, "animation"), out.animation_name);
    out.hidden_material_substrings =
        stringArrayOr(child(floor, "hiddenMaterialSubstrings"), out.hidden_material_substrings);
    out.weather_material_substrings =
        stringArrayOr(child(floor, "weatherMaterialSubstrings"), out.weather_material_substrings);
    out.weather_modes = parseWeatherModes(child(floor, "weatherModes"), out.weather_modes);
    out.active_weather =
        std::clamp(intOr(child(floor, "activeWeather"), out.active_weather), 0, std::max(0, static_cast<int>(out.weather_modes.size()) - 1));
    if (const JsonValue* pos = child(floor, "position")) {
        out.x = numOr(child(pos, "x"), out.x);
        out.y = numOr(child(pos, "y"), out.y);
        out.z = numOr(child(pos, "z"), out.z);
        out.model_x = numOr(child(pos, "x"), out.model_x);
        out.model_y = numOr(child(pos, "y"), out.model_y);
        out.model_z = numOr(child(pos, "z"), out.model_z);
    }
    if (const JsonValue* model = child(floor, "modelPlacement")) {
        out.model_x = numOr(child(model, "x"), out.model_x);
        out.model_y = numOr(child(model, "y"), out.model_y);
        out.model_z = numOr(child(model, "z"), out.model_z);
        out.model_yaw_degrees = numOr(child(model, "yawDegrees"), out.model_yaw_degrees);
        out.model_scale = numOr(child(model, "scale"), out.model_scale);
        if (const JsonValue* offset = child(model, "offset")) {
            out.anchor_offset_x = numOr(child(offset, "x"), out.anchor_offset_x);
            out.anchor_offset_z = numOr(child(offset, "z"), out.anchor_offset_z);
        }
    }
    out.x = numOr(child(floor, "x"), out.x);
    out.y = numOr(child(floor, "y"), out.y);
    out.z = numOr(child(floor, "z"), out.z);
    if (const JsonValue* extensions = child(floor, "extensions"); extensions && extensions->isArray()) {
        out.extensions.clear();
        for (const JsonValue& value : extensions->asArray()) {
            if (!value.isObject()) continue;
            AttendFloorExtensionConfig extension;
            extension.id = strOr(child(&value, "id"), extension.id);
            extension.enabled = boolOr(child(&value, "enabled"), extension.enabled);
            extension.model_path = resolvePath(project_root, strOr(child(&value, "model"), extension.model_path));
            extension.model_x = out.model_x;
            extension.model_y = out.model_y;
            extension.model_z = out.model_z;
            extension.model_yaw_degrees = out.model_yaw_degrees;
            extension.model_scale = out.model_scale;
            if (const JsonValue* model = child(&value, "modelPlacement")) {
                extension.model_x = numOr(child(model, "x"), extension.model_x);
                extension.model_y = numOr(child(model, "y"), extension.model_y);
                extension.model_z = numOr(child(model, "z"), extension.model_z);
                extension.model_yaw_degrees = numOr(child(model, "yawDegrees"), extension.model_yaw_degrees);
                extension.model_scale = numOr(child(model, "scale"), extension.model_scale);
            }
            if (!extension.model_path.empty()) {
                out.extensions.push_back(std::move(extension));
            }
        }
    }
}

void applyFloorAnchor(const PokemonAttendModelConfig& pokemon, AttendFloorConfig& floor) {
    if (!floor.enabled) return;
    if (floor.placement_anchor != "pokemon_feet") return;
    floor.x = pokemon.x + floor.anchor_offset_x;
    floor.z = pokemon.z + floor.anchor_offset_z;
    floor.model_x = floor.x;
    floor.model_z = floor.z;
    for (AttendFloorExtensionConfig& extension : floor.extensions) {
        extension.model_x = floor.model_x;
        extension.model_z = floor.model_z;
    }
}

void parseWall(const JsonValue* wall, AttendWallConfig& out) {
    if (!wall) return;
    out.id = strOr(child(wall, "id"), out.id);
    out.shape = strOr(child(wall, "shape"), out.shape);
    out.enabled = boolOr(child(wall, "enabled"), out.enabled);
    out.radius = numOr(child(wall, "radius"), out.radius);
    out.distance = numOr(child(wall, "distance"), out.distance);
    out.height = numOr(child(wall, "height"), out.height);
    out.bottom_y = numOr(child(wall, "bottomY"), out.bottom_y);
    out.arc_degrees = numOr(child(wall, "arcDegrees"), out.arc_degrees);
    out.segments = std::max(4, intOr(child(wall, "segments"), out.segments));
    out.vertical_segments = std::max(1, intOr(child(wall, "verticalSegments"), out.vertical_segments));
    out.edge_darkening = std::clamp(numOr(child(wall, "edgeDarkening"), out.edge_darkening), 0.0f, 0.95f);
    out.gradient_colors = parseStops(child(wall, "gradientColors"), out.gradient_colors);
}

void parseLighting(const JsonValue* lighting, AttendLightingConfig& out) {
    if (!lighting) return;
    out.brightness = numOr(child(lighting, "brightness"), out.brightness);
    out.pokemon_brightness =
        std::clamp(numOr(child(lighting, "pokemonBrightness"), out.pokemon_brightness), 0.1f, 3.0f);
    out.backdrop_brightness =
        std::clamp(numOr(child(lighting, "backdropBrightness"), out.backdrop_brightness), 0.1f, 3.0f);
    out.backdrop_saturation =
        std::clamp(numOr(child(lighting, "backdropSaturation"), out.backdrop_saturation), 0.0f, 2.0f);
    out.backdrop_contrast =
        std::clamp(numOr(child(lighting, "backdropContrast"), out.backdrop_contrast), 0.0f, 2.0f);
    out.ambient = std::clamp(numOr(child(lighting, "ambient"), out.ambient), 0.0f, 1.5f);
    out.directional = std::clamp(numOr(child(lighting, "directional"), out.directional), 0.0f, 1.5f);
    out.form_shadow = std::clamp(numOr(child(lighting, "formShadow"), out.form_shadow), 0.0f, 1.0f);
    parseAxis(child(lighting, "lightDirection"), out.light_direction);
    out.tint = parseColor(child(lighting, "tint"), out.tint);
}

} // namespace

AttendSceneConfig loadAttendSceneConfig(const std::string& project_root) {
    AttendSceneConfig out;
    out.wall.gradient_colors = {
        GradientStop{0.0f, Color3{0.28f, 0.49f, 0.71f}},
        GradientStop{1.0f, Color3{0.98f, 0.88f, 0.68f}},
    };

    const fs::path path = fs::path(project_root) / "config" / "gameplay" / "pokemon_attend" / "scene.json";
    const JsonValue root = parseJsonFile(path.string());
    const fs::path config_dir = fs::path(project_root) / "config" / "gameplay" / "pokemon_attend";
    const JsonValue pokemon_root = parseJsonFile((config_dir / "pokemon.json").string());
    const JsonValue floors_root = parseJsonFile((config_dir / "floors.json").string());
    const JsonValue walls_root = parseJsonFile((config_dir / "walls.json").string());
    const std::string active = strOr(child(&root, "activeScene"), out.id);
    const JsonValue* selected = nullptr;
    if (const JsonValue* scenes = child(&root, "scenes"); scenes && scenes->isArray()) {
        for (const JsonValue& scene : scenes->asArray()) {
            if (scene.isObject() && strOr(child(&scene, "id"), "") == active) {
                selected = &scene;
                break;
            }
        }
        if (!selected && !scenes->asArray().empty() && scenes->asArray().front().isObject()) {
            selected = &scenes->asArray().front();
        }
    }
    if (!selected) {
        throw std::runtime_error("pokemon_attend scene.json has no scenes");
    }

    out.id = strOr(child(selected, "id"), out.id);
    parsePokemon(child(selected, "pokemon"), project_root, out.pokemon);
    parseInteractionAdapter(child(selected, "interactionAdapter"), out.interaction_adapter);

    const std::string active_pokemon = strOr(child(selected, "activePokemon"), strOr(child(&root, "activePokemon"), ""));
    if (!active_pokemon.empty()) {
        const JsonValue* defaults = child(&pokemon_root, "defaults");
        const JsonValue* profile = catalogEntry(child(&pokemon_root, "pokemon"), active_pokemon);
        parsePokemonProviderProfile(defaults, profile, active_pokemon, project_root, out.pokemon, out.interaction_adapter);
    }
    if (const JsonValue* camera = child(selected, "camera")) {
        out.camera.auto_focus = boolOr(child(camera, "autoFocus"), out.camera.auto_focus);
        out.camera.screen_height_ratio = std::clamp(numOr(child(camera, "screenHeightRatio"), out.camera.screen_height_ratio), 0.15f, 0.95f);
        out.camera.distance_scale = std::clamp(numOr(child(camera, "distanceScale"), out.camera.distance_scale), 0.35f, 2.5f);
        out.camera.face_screen_height_ratio =
            std::clamp(numOr(child(camera, "faceScreenHeightRatio"), out.camera.face_screen_height_ratio), 0.15f, 0.98f);
        out.camera.face_distance_scale =
            std::clamp(numOr(child(camera, "faceDistanceScale"), out.camera.face_distance_scale), 0.15f, 2.5f);
        out.camera.face_target_y_ratio =
            std::clamp(numOr(child(camera, "faceTargetYRatio"), out.camera.face_target_y_ratio), 0.0f, 1.0f);
        out.camera.face_height_offset = numOr(child(camera, "faceHeightOffset"), out.camera.face_height_offset);
        out.camera.face_view_min_model_height =
            std::max(0.0f, numOr(child(camera, "faceViewMinModelHeight"), out.camera.face_view_min_model_height));
        out.camera.min_distance = std::max(0.1f, numOr(child(camera, "minDistance"), out.camera.min_distance));
        out.camera.max_distance = std::max(out.camera.min_distance, numOr(child(camera, "maxDistance"), out.camera.max_distance));
        out.camera.height_offset = numOr(child(camera, "heightOffset"), out.camera.height_offset);
        out.camera.target_y_ratio = std::clamp(numOr(child(camera, "targetYRatio"), out.camera.target_y_ratio), 0.0f, 1.0f);
        out.camera.depth_padding_scale =
            std::clamp(numOr(child(camera, "depthPaddingScale"), out.camera.depth_padding_scale), 0.0f, 1.5f);
        out.camera.face_depth_padding_scale =
            std::clamp(numOr(child(camera, "faceDepthPaddingScale"), out.camera.face_depth_padding_scale), 0.0f, 1.5f);
        out.camera.target_x = numOr(child(camera, "targetX"), out.camera.target_x);
        out.camera.target_z = numOr(child(camera, "targetZ"), out.camera.target_z);
        out.camera.distance = numOr(child(camera, "distance"), out.camera.distance);
        out.camera.height = numOr(child(camera, "height"), out.camera.height);
        out.camera.target_height = numOr(child(camera, "targetHeight"), out.camera.target_height);
        out.camera.fov_y_degrees = numOr(child(camera, "fovYDegrees"), out.camera.fov_y_degrees);
        out.camera.near_clip = numOr(child(camera, "nearClip"), out.camera.near_clip);
        out.camera.far_clip = numOr(child(camera, "farClip"), out.camera.far_clip);
    }
    if (const JsonValue* look = child(selected, "viewportLook")) {
        out.viewport_look.enabled = boolOr(child(look, "enabled"), out.viewport_look.enabled);
        out.viewport_look.max_x = std::max(0.0f, numOr(child(look, "maxX"), out.viewport_look.max_x));
        out.viewport_look.max_y = std::max(0.0f, numOr(child(look, "maxY"), out.viewport_look.max_y));
        out.viewport_look.edge_margin_ratio =
            std::clamp(numOr(child(look, "edgeMarginRatio"), out.viewport_look.edge_margin_ratio), 0.01f, 0.45f);
        out.viewport_look.smooth_seconds =
            std::clamp(numOr(child(look, "smoothSeconds"), out.viewport_look.smooth_seconds), 0.01f, 2.0f);
    }
    parseLighting(child(selected, "lighting"), out.lighting);
    if (const JsonValue* dof = child(selected, "depthOfField")) {
        out.depth_of_field.enabled = boolOr(child(dof, "enabled"), out.depth_of_field.enabled);
        out.depth_of_field.strength =
            std::clamp(numOr(child(dof, "strength"), out.depth_of_field.strength), 0.0f, 1.0f);
        out.depth_of_field.max_radius =
            std::clamp(numOr(child(dof, "maxRadius"), out.depth_of_field.max_radius), 0.0f, 12.0f);
        out.depth_of_field.focus_depth =
            std::max(0.0f, numOr(child(dof, "focusDepth"), out.depth_of_field.focus_depth));
        out.depth_of_field.falloff =
            std::max(0.01f, numOr(child(dof, "falloff"), out.depth_of_field.falloff));
    }
    if (const JsonValue* shadow = child(selected, "shadow")) {
        out.shadow.enabled = boolOr(child(shadow, "enabled"), out.shadow.enabled);
        out.shadow.strength = std::clamp(numOr(child(shadow, "strength"), out.shadow.strength), 0.0f, 1.0f);
        out.shadow.radius_x = std::max(0.01f, numOr(child(shadow, "radiusX"), out.shadow.radius_x));
        out.shadow.radius_z = std::max(0.01f, numOr(child(shadow, "radiusZ"), out.shadow.radius_z));
        out.shadow.y_offset = numOr(child(shadow, "yOffset"), out.shadow.y_offset);
    }
    const std::string active_floor = strOr(child(selected, "activeFloor"), strOr(child(&root, "activeFloor"), out.floor.id));
    if (!active_floor.empty()) {
        const JsonValue* floor_entry = catalogEntry(child(&floors_root, "floors"), active_floor);
        parseFloor(child(&floors_root, "environmentDefaults"), project_root, out.floor);
        parseFloor(floor_entry, project_root, out.floor);
    }
    if (const JsonValue* floor = child(selected, "floor")) {
        parseFloor(floor, project_root, out.floor);
    }
    applyFloorAnchor(out.pokemon, out.floor);
    const std::string active_wall = strOr(
        child(selected, "activeSky"),
        strOr(child(&root, "activeSky"), strOr(child(selected, "activeWall"), strOr(child(&root, "activeWall"), out.wall.id))));
    if (!active_wall.empty()) {
        const JsonValue* wall_defaults = child(&walls_root, "defaults");
        parseWall(wall_defaults, out.wall);
        parseLighting(child(wall_defaults, "lighting"), out.lighting);
        const JsonValue* selected_wall = catalogEntry(child(&walls_root, "skyPresets"), active_wall);
        if (!selected_wall) {
            selected_wall = catalogEntry(child(&walls_root, "walls"), active_wall);
        }
        parseWall(selected_wall, out.wall);
        parseLighting(child(selected_wall, "lighting"), out.lighting);
    }
    if (const JsonValue* wall = child(selected, "wall")) {
        parseWall(wall, out.wall);
        parseLighting(child(wall, "lighting"), out.lighting);
    }
    if (const JsonValue* background = child(selected, "background")) {
        out.clear_color = parseColor(child(background, "clearColor"), out.clear_color);
    }
    parseUi(child(selected, "ui"), out.ui);
    return out;
}

} // namespace pr::gameplay::attend
