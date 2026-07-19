#include "gameplay/attend/AttendSceneConfig.hpp"

#include "core/config/Json.hpp"
#include "gameplay/attend/PokemonModelCatalog.hpp"

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

std::unordered_map<std::string, std::string> stringMapOr(
    const JsonValue* value,
    std::unordered_map<std::string, std::string> fallback) {
    if (!value || !value->isObject()) return fallback;
    for (const auto& [key, child_value] : value->asObject()) {
        if (child_value.isString()) {
            fallback[key] = child_value.asString();
        }
    }
    return fallback;
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

std::string findModelFileForSpecies(
    const std::string& project_root,
    const std::string& model_dir,
    const std::string& species_id) {
    if (model_dir.empty() || species_id.empty()) return {};
    const fs::path dir = fs::path(resolvePath(project_root, model_dir));
    for (const PokemonModelCatalogEntry& entry : discoverPokemonModels(dir)) {
        if (entry.id == species_id) return entry.path;
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

std::string resolveConfigPath(const std::string& project_root, const fs::path& manifest_path, const std::string& path) {
    if (path.empty()) return {};
    const fs::path p(path);
    if (p.is_absolute()) return p.string();
    const fs::path config_relative = fs::path(project_root) / "config" / p;
    std::error_code ec;
    if (fs::exists(config_relative, ec)) return config_relative.string();
    return (manifest_path.parent_path() / p).string();
}

JsonValue referencedConfig(
    const std::string& project_root,
    const fs::path& manifest_path,
    const JsonValue* root,
    const char* section_key,
    const char* legacy_key) {
    const JsonValue* files = child(root, "files");
    const std::string path = strOr(child(files, section_key), strOr(child(root, legacy_key), ""));
    if (path.empty()) return JsonValue{};
    return parseJsonFile(resolveConfigPath(project_root, manifest_path, path));
}

std::vector<std::string> stringArrayOr(const JsonValue* value, std::vector<std::string> fallback) {
    if (!value || !value->isArray()) return fallback;
    std::vector<std::string> out;
    for (const JsonValue& item : value->asArray()) {
        if (item.isString()) out.push_back(item.asString());
    }
    return out.empty() ? fallback : out;
}

const JsonValue* firstChild(const JsonValue* obj, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        if (const JsonValue* value = child(obj, key)) return value;
    }
    return nullptr;
}

float numOrAny(const JsonValue* obj, std::initializer_list<const char*> keys, float fallback) {
    for (const char* key : keys) {
        if (const JsonValue* value = child(obj, key); value && value->isNumber()) {
            return static_cast<float>(value->asNumber());
        }
    }
    return fallback;
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
    pokemon.sprite_form_keys = stringMapOr(child(defaults, "spriteFormKeys"), pokemon.sprite_form_keys);
    pokemon.yaw_degrees = numOr(child(profile, "yawDegrees"), pokemon.yaw_degrees);
    pokemon.pitch_degrees = numOr(child(profile, "pitchDegrees"), pokemon.pitch_degrees);
    pokemon.scale = numOr(child(profile, "scale"), pokemon.scale);
    pokemon.sprite_form_keys = stringMapOr(child(profile, "spriteFormKeys"), pokemon.sprite_form_keys);

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

void parseCornerButton(const JsonValue* value, const std::string& project_root, AttendCornerButtonConfig& out) {
    if (!value || !value->isObject()) return;
    out.enabled = boolOr(child(value, "enabled"), out.enabled);
    out.icon_path = resolvePath(project_root, strOr(child(value, "icon"), out.icon_path));
    out.scale = std::clamp(numOr(child(value, "scale"), out.scale), 0.5f, 3.0f);
    out.icon_scale = std::clamp(numOr(child(value, "iconScale"), out.icon_scale), 0.2f, 1.2f);
    out.icon_offset_x = std::clamp(numOr(child(value, "iconOffsetX"), out.icon_offset_x), -0.35f, 0.35f);
    out.icon_offset_y = std::clamp(numOr(child(value, "iconOffsetY"), out.icon_offset_y), -0.35f, 0.35f);
    out.side_extension_ratio =
        std::clamp(numOr(child(value, "sideExtensionRatio"), out.side_extension_ratio), 0.0f, 1.0f);
    out.outer_border = parseColor4(child(value, "outerBorder"), out.outer_border);
    out.inner_border = parseColor4(child(value, "innerBorder"), out.inner_border);
    out.fill_top = parseColor4(child(value, "fillTop"), out.fill_top);
    out.fill_bottom = parseColor4(child(value, "fillBottom"), out.fill_bottom);
}

void parseCornerButtonBehavior(const JsonValue* value, AttendCornerButtonBehaviorConfig& out) {
    if (!value || !value->isObject()) return;
    out.hide_after_pet_seconds =
        std::clamp(numOr(child(value, "hideAfterPetSeconds"), out.hide_after_pet_seconds), 0.0f, 5.0f);
    out.return_delay_seconds =
        std::clamp(numOr(child(value, "returnDelaySeconds"), out.return_delay_seconds), 0.0f, 5.0f);
    out.slide_in_seconds =
        std::clamp(numOr(child(value, "slideInSeconds"), out.slide_in_seconds), 0.01f, 2.0f);
    out.slide_out_seconds =
        std::clamp(numOr(child(value, "slideOutSeconds"), out.slide_out_seconds), 0.01f, 2.0f);
}

void parseProfilePlate(const JsonValue* value, AttendProfilePlateConfig& out) {
    if (!value || !value->isObject()) return;
    out.enabled = boolOr(child(value, "enabled"), out.enabled);
    out.width = std::max(120, intOr(child(value, "width"), out.width));
    out.height = std::max(48, intOr(child(value, "height"), out.height));
    out.margin_x = std::max(0, intOr(child(value, "marginX"), out.margin_x));
    out.margin_y = std::max(0, intOr(child(value, "marginY"), out.margin_y));
    out.padding_x = std::max(0, intOr(child(value, "paddingX"), out.padding_x));
    out.padding_y = std::max(0, intOr(child(value, "paddingY"), out.padding_y));
    out.corner_radius = std::max(0, intOr(child(value, "cornerRadius"), out.corner_radius));
    out.outer_stroke_width = std::max(0, intOr(child(value, "outerStrokeWidth"), out.outer_stroke_width));
    out.inner_stroke_width = std::max(0, intOr(child(value, "innerStrokeWidth"), out.inner_stroke_width));
    out.show_sprite = boolOr(child(value, "showSprite"), out.show_sprite);
    out.sprite_size = std::max(1, intOr(child(value, "spriteSize"), out.sprite_size));
    out.sprite_scale = std::clamp(numOr(child(value, "spriteScale"), out.sprite_scale), 0.25f, 6.0f);
    out.sprite_gap = std::max(0, intOr(child(value, "spriteGap"), out.sprite_gap));
    out.sprite_offset_x = intOr(child(value, "spriteOffsetX"), out.sprite_offset_x);
    out.sprite_offset_y = intOr(child(value, "spriteOffsetY"), out.sprite_offset_y);
    out.name_text_offset_x = intOr(child(value, "nameTextOffsetX"), out.name_text_offset_x);
    out.name_text_offset_y = intOr(child(value, "nameTextOffsetY"), out.name_text_offset_y);
    out.detail_text_offset_x = intOr(child(value, "detailTextOffsetX"), out.detail_text_offset_x);
    out.detail_text_offset_y = intOr(child(value, "detailTextOffsetY"), out.detail_text_offset_y);
    out.name_font_size = std::max(8, intOr(child(value, "nameFontSize"), out.name_font_size));
    out.detail_font_size = std::max(8, intOr(child(value, "detailFontSize"), out.detail_font_size));
    out.name_row_height = std::max(1, intOr(child(value, "nameRowHeight"), out.name_row_height));
    out.detail_row_height = std::max(1, intOr(child(value, "detailRowHeight"), out.detail_row_height));
    out.name_row_width = std::max(1, intOr(child(value, "nameRowWidth"), out.name_row_width));
    out.characteristic_row_width = std::max(1, intOr(child(value, "characteristicRowWidth"), out.characteristic_row_width));
    out.nature_row_width = std::max(1, intOr(child(value, "natureRowWidth"), out.nature_row_width));
    out.row_gap = std::max(0, intOr(child(value, "rowGap"), out.row_gap));
    out.left_fade_width = std::max(0, intOr(child(value, "leftFadeWidth"), out.left_fade_width));
    out.slide_out_x = std::max(0, intOr(child(value, "slideOutX"), out.slide_out_x));
    out.characteristic = strOr(child(value, "characteristic"), out.characteristic);
    out.nature = strOr(child(value, "nature"), out.nature);
    out.font_path = strOr(child(value, "font"), strOr(child(value, "fontPath"), out.font_path));
    out.outer_border = parseColor4(child(value, "outerBorder"), out.outer_border);
    out.inner_border = parseColor4(child(value, "innerBorder"), out.inner_border);
    out.fill_top = parseColor4(child(value, "fillTop"), out.fill_top);
    out.fill_bottom = parseColor4(child(value, "fillBottom"), out.fill_bottom);
    out.divider = parseColor4(child(value, "divider"), out.divider);
    out.name_text = parseColor4(child(value, "nameText"), out.name_text);
    out.detail_text = parseColor4(child(value, "detailText"), out.detail_text);
}

void parseIdleBehavior(const JsonValue* value, AttendIdleBehaviorConfig& out) {
    if (!value || !value->isObject()) return;
    out.enabled = boolOr(child(value, "enabled"), out.enabled);
    out.emote_after_seconds = std::max(1.0f, numOr(child(value, "emoteAfterSeconds"), out.emote_after_seconds));
    out.emote_interval_min_seconds =
        std::max(0.0f, numOr(child(value, "emoteIntervalMinSeconds"), out.emote_interval_min_seconds));
    out.emote_interval_max_seconds =
        std::max(out.emote_interval_min_seconds, numOr(child(value, "emoteIntervalMaxSeconds"), out.emote_interval_max_seconds));
    out.sleep_after_seconds = std::max(1.0f, numOr(child(value, "sleepAfterSeconds"), out.sleep_after_seconds));
    out.emote_animation_semantic = strOr(child(value, "emoteAnimation"), out.emote_animation_semantic);
    out.sleep_animation_semantic = strOr(child(value, "sleepAnimation"), out.sleep_animation_semantic);
    out.emote_eye_expression = strOr(child(value, "emoteEye"), out.emote_eye_expression);
    out.emote_mouth_expression = strOr(child(value, "emoteMouth"), out.emote_mouth_expression);
    out.sleep_eye_expression = strOr(child(value, "sleepEye"), out.sleep_eye_expression);
    out.sleep_mouth_expression = strOr(child(value, "sleepMouth"), out.sleep_mouth_expression);
    out.emote_duration_seconds = std::clamp(numOr(child(value, "emoteDurationSeconds"), out.emote_duration_seconds), 0.0f, 30.0f);
    out.sleep_duration_seconds = std::clamp(numOr(child(value, "sleepDurationSeconds"), out.sleep_duration_seconds), 1.0f, 7200.0f);
    out.fade_in_seconds = std::clamp(numOr(child(value, "fadeInSeconds"), out.fade_in_seconds), 0.0f, 5.0f);
    out.fade_out_seconds = std::clamp(numOr(child(value, "fadeOutSeconds"), out.fade_out_seconds), 0.0f, 5.0f);
}

void parseCamera(const JsonValue* camera, AttendCameraConfig& out) {
    if (!camera || !camera->isObject()) return;
    out.auto_focus = boolOr(child(camera, "autoFocus"), out.auto_focus);
    out.screen_height_ratio = std::clamp(numOr(child(camera, "screenHeightRatio"), out.screen_height_ratio), 0.15f, 0.95f);
    out.distance_scale = std::clamp(numOr(child(camera, "distanceScale"), out.distance_scale), 0.35f, 2.5f);
    out.face_screen_height_ratio =
        std::clamp(numOr(child(camera, "faceScreenHeightRatio"), out.face_screen_height_ratio), 0.15f, 0.98f);
    out.face_distance_scale =
        std::clamp(numOr(child(camera, "faceDistanceScale"), out.face_distance_scale), 0.15f, 2.5f);
    out.face_target_y_ratio =
        std::clamp(numOr(child(camera, "faceTargetYRatio"), out.face_target_y_ratio), 0.0f, 1.0f);
    out.face_height_offset = numOr(child(camera, "faceHeightOffset"), out.face_height_offset);
    out.face_view_min_model_height =
        std::max(0.0f, numOr(child(camera, "faceViewMinModelHeight"), out.face_view_min_model_height));
    out.min_distance = std::max(0.1f, numOr(child(camera, "minDistance"), out.min_distance));
    out.max_distance = std::max(out.min_distance, numOr(child(camera, "maxDistance"), out.max_distance));
    out.height_offset = numOr(child(camera, "heightOffset"), out.height_offset);
    out.target_y_ratio = std::clamp(numOr(child(camera, "targetYRatio"), out.target_y_ratio), 0.0f, 1.0f);
    out.depth_padding_scale =
        std::clamp(numOr(child(camera, "depthPaddingScale"), out.depth_padding_scale), 0.0f, 1.5f);
    out.face_depth_padding_scale =
        std::clamp(numOr(child(camera, "faceDepthPaddingScale"), out.face_depth_padding_scale), 0.0f, 1.5f);
    out.target_x = numOr(child(camera, "targetX"), out.target_x);
    out.target_z = numOr(child(camera, "targetZ"), out.target_z);
    out.distance = numOr(child(camera, "distance"), out.distance);
    out.height = numOr(child(camera, "height"), out.height);
    out.target_height = numOr(child(camera, "targetHeight"), out.target_height);
    out.fov_y_degrees = numOr(child(camera, "fovYDegrees"), out.fov_y_degrees);
    out.near_clip = numOr(child(camera, "nearClip"), out.near_clip);
    out.far_clip = numOr(child(camera, "farClip"), out.far_clip);
    if (const JsonValue* framing = firstChild(camera, {"framing", "fullBodyFraming"}); framing && framing->isObject()) {
        out.screen_height_ratio =
            std::clamp(numOrAny(framing, {"screenHeightRatio", "pokemonScreenHeight"}, out.screen_height_ratio), 0.15f, 0.95f);
        out.distance_scale =
            std::clamp(numOrAny(framing, {"distanceScale", "distanceMultiplier"}, out.distance_scale), 0.35f, 2.5f);
        out.min_distance = std::max(0.1f, numOrAny(framing, {"minDistance", "minimumDistance"}, out.min_distance));
        out.max_distance =
            std::max(out.min_distance, numOrAny(framing, {"maxDistance", "maximumDistance"}, out.max_distance));
        out.target_y_ratio =
            std::clamp(numOrAny(framing, {"targetYRatio", "targetHeightRatio"}, out.target_y_ratio), 0.0f, 1.0f);
        out.height_offset = numOrAny(framing, {"heightOffset", "verticalOffset"}, out.height_offset);
        out.depth_padding_scale =
            std::clamp(numOrAny(framing, {"depthPaddingScale", "depthPadding"}, out.depth_padding_scale), 0.0f, 1.5f);
    }
    if (const JsonValue* face = child(camera, "faceFraming"); face && face->isObject()) {
        out.face_screen_height_ratio =
            std::clamp(numOrAny(face, {"screenHeightRatio", "faceScreenHeight"}, out.face_screen_height_ratio), 0.15f, 0.98f);
        out.face_distance_scale =
            std::clamp(numOrAny(face, {"distanceScale", "distanceMultiplier"}, out.face_distance_scale), 0.15f, 2.5f);
        out.face_target_y_ratio =
            std::clamp(numOrAny(face, {"targetYRatio", "headTargetHeightRatio"}, out.face_target_y_ratio), 0.0f, 1.0f);
        out.face_height_offset = numOrAny(face, {"heightOffset", "verticalOffset"}, out.face_height_offset);
        out.face_view_min_model_height =
            std::max(0.0f, numOrAny(face, {"minModelHeight", "minimumPokemonHeight"}, out.face_view_min_model_height));
        out.face_depth_padding_scale =
            std::clamp(numOrAny(face, {"depthPaddingScale", "depthPadding"}, out.face_depth_padding_scale), 0.0f, 1.5f);
    }
    if (const JsonValue* forms = firstChild(camera, {"formFraming", "formAdjustments", "formFaceFraming", "formFaceAdjustments"});
        forms && forms->isObject()) {
        for (const auto& [form_id, value] : forms->asObject()) {
            if (!value.isObject()) continue;
            AttendCameraConfig::FormFramingAdjustment adjustment;
            adjustment.target_y_ratio_offset = std::clamp(
                numOrAny(&value, {"targetYRatioOffset", "headTargetHeightRatioOffset"}, adjustment.target_y_ratio_offset),
                -0.25f,
                0.25f);
            adjustment.height_offset =
                std::clamp(numOrAny(&value, {"heightOffset", "verticalOffset"}, adjustment.height_offset), -0.5f, 0.5f);
            out.form_framing_adjustments[form_id] = adjustment;
        }
    }
    const JsonValue* camera_advanced = child(camera, "advanced");
    const JsonValue* fallback = firstChild(camera, {"manualFallback", "manualCameraFallback"});
    if (!fallback) fallback = firstChild(camera_advanced, {"manualFallback", "manualCameraFallback"});
    if (fallback && fallback->isObject()) {
        out.target_x = numOr(child(fallback, "targetX"), out.target_x);
        out.target_z = numOr(child(fallback, "targetZ"), out.target_z);
        out.distance = numOr(child(fallback, "distance"), out.distance);
        out.height = numOr(child(fallback, "height"), out.height);
        out.target_height = numOr(child(fallback, "targetHeight"), out.target_height);
    }
    const JsonValue* lens = firstChild(camera, {"lens"});
    if (!lens) lens = child(camera_advanced, "lens");
    if (lens && lens->isObject()) {
        out.fov_y_degrees = numOr(child(lens, "fovYDegrees"), out.fov_y_degrees);
        out.near_clip = numOr(child(lens, "nearClip"), out.near_clip);
        out.far_clip = numOr(child(lens, "farClip"), out.far_clip);
    }
    const JsonValue* freecam = firstChild(camera, {"freecam", "freeCamera"});
    if (!freecam) freecam = firstChild(camera_advanced, {"freecam", "freeCamera"});
    if (freecam && freecam->isObject()) {
        out.freecam_move_speed = std::max(0.0f, numOr(child(freecam, "moveSpeed"), out.freecam_move_speed));
        out.freecam_mouse_sensitivity =
            std::max(0.0f, numOr(child(freecam, "mouseSensitivity"), out.freecam_mouse_sensitivity));
        if (const JsonValue* offset = child(freecam, "initialOffset"); offset && offset->isArray()) {
            const auto& arr = offset->asArray();
            if (arr.size() > 0 && arr[0].isNumber()) out.freecam_initial_offset_x = numOr(&arr[0], out.freecam_initial_offset_x);
            if (arr.size() > 1 && arr[1].isNumber()) out.freecam_initial_offset_y = numOr(&arr[1], out.freecam_initial_offset_y);
            if (arr.size() > 2 && arr[2].isNumber()) out.freecam_initial_offset_z = numOr(&arr[2], out.freecam_initial_offset_z);
        }
        out.freecam_initial_yaw_degrees =
            numOr(child(freecam, "initialYawDegrees"), numOr(child(freecam, "initialYawDeg"), out.freecam_initial_yaw_degrees));
        out.freecam_initial_pitch_degrees =
            numOr(child(freecam, "initialPitchDegrees"), numOr(child(freecam, "initialPitchDeg"), out.freecam_initial_pitch_degrees));
        out.freecam_pitch_min_degrees =
            numOr(child(freecam, "pitchClampMinDegrees"), numOr(child(freecam, "pitchClampMinDeg"), out.freecam_pitch_min_degrees));
        out.freecam_pitch_max_degrees =
            numOr(child(freecam, "pitchClampMaxDegrees"), numOr(child(freecam, "pitchClampMaxDeg"), out.freecam_pitch_max_degrees));
    }
}

void parseViewportLook(const JsonValue* look, AttendViewportLookConfig& out) {
    if (!look || !look->isObject()) return;
    out.enabled = boolOr(child(look, "enabled"), out.enabled);
    out.max_x = std::max(0.0f, numOr(child(look, "maxX"), out.max_x));
    out.max_y = std::max(0.0f, numOr(child(look, "maxY"), out.max_y));
    out.edge_margin_ratio = std::clamp(numOr(child(look, "edgeMarginRatio"), out.edge_margin_ratio), 0.01f, 0.45f);
    out.smooth_seconds = std::clamp(numOr(child(look, "smoothSeconds"), out.smooth_seconds), 0.01f, 2.0f);
}

void parseDepthOfField(const JsonValue* dof, AttendDepthOfFieldConfig& out) {
    if (!dof || !dof->isObject()) return;
    out.enabled = boolOr(child(dof, "enabled"), out.enabled);
    out.strength = std::clamp(numOr(child(dof, "strength"), out.strength), 0.0f, 1.0f);
    out.max_radius = std::clamp(numOr(child(dof, "maxRadius"), out.max_radius), 0.0f, 12.0f);
    out.focus_depth = std::max(0.0f, numOr(child(dof, "focusDepth"), out.focus_depth));
    out.falloff = std::max(0.01f, numOr(child(dof, "falloff"), out.falloff));
}

void parseShadow(const JsonValue* shadow, AttendShadowConfig& out) {
    if (!shadow || !shadow->isObject()) return;
    out.enabled = boolOr(child(shadow, "enabled"), out.enabled);
    out.strength = std::clamp(numOr(child(shadow, "strength"), out.strength), 0.0f, 1.0f);
    out.radius_x = std::max(0.01f, numOr(child(shadow, "radiusX"), out.radius_x));
    out.radius_z = std::max(0.01f, numOr(child(shadow, "radiusZ"), out.radius_z));
    out.y_offset = numOr(child(shadow, "yOffset"), out.y_offset);
}

void parseUi(const JsonValue* value, const std::string& project_root, AttendUiConfig& out) {
    if (!value || !value->isObject()) return;
    out.pixelated_overlay = boolOr(child(value, "pixelatedOverlay"), out.pixelated_overlay);
    auto parse_render_mode = [](std::string value, const std::string& fallback) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value == "sd" ? std::string{"sd"} : (value == "hd" ? std::string{"hd"} : fallback);
    };
    if (const JsonValue* rendering = child(value, "rendering"); rendering && rendering->isObject()) {
        out.normal_render_mode =
            parse_render_mode(strOr(child(rendering, "normalUi"), out.normal_render_mode), out.normal_render_mode);
        out.debug_render_mode =
            parse_render_mode(strOr(child(rendering, "debugUi"), out.debug_render_mode), out.debug_render_mode);
    }
    parseOverlayButton(child(value, "weatherButton"), out.weather_button);
    parseOverlayButton(child(value, "viewButton"), out.view_button);
    parseOverlayButton(child(value, "pokemonButton"), out.pokemon_button);
    parseOverlayButton(child(value, "previousPokemonButton"), out.previous_pokemon_button);
    parseOverlayButton(child(value, "textureVariantButton"), out.texture_variant_button);
    parseOverlayButton(child(value, "formVariantButton"), out.form_variant_button);
    parseOverlayButton(child(value, "skyButton"), out.sky_button);
    parseOverlayButton(child(value, "emoteButton"), out.emote_button);
    parseOverlayButton(child(value, "sleepButton"), out.sleep_button);
    parseOverlayButton(child(value, "cryButton"), out.cry_button);
    parseCornerButton(child(value, "actionButton"), project_root, out.action_button);
    parseCornerButton(child(value, "itemsButton"), project_root, out.items_button);
    parseCornerButton(child(value, "returnButton"), project_root, out.return_button);
    parseCornerButtonBehavior(child(value, "cornerButtons"), out.corner_buttons);
    parseProfilePlate(child(value, "profilePlate"), out.profile_plate);
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

void parseAudio(const JsonValue* audio, AttendAudioConfig& out) {
    if (!audio || !audio->isObject()) return;
    out.pet_happy_cry_delay_seconds =
        std::clamp(numOr(child(audio, "petHappyCryDelaySeconds"), out.pet_happy_cry_delay_seconds), 0.0f, 10.0f);
}

std::string skyLabelFromId(std::string id) {
    bool next_upper = true;
    for (char& c : id) {
        if (c == '_' || c == '-') {
            c = ' ';
            next_upper = true;
        } else if (next_upper) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            next_upper = false;
        } else {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return id;
}

} // namespace

AttendSceneConfig loadAttendSceneConfig(const std::string& project_root) {
    AttendSceneConfig out;
    out.wall.gradient_colors = {
        GradientStop{0.0f, Color3{0.28f, 0.49f, 0.71f}},
        GradientStop{1.0f, Color3{0.98f, 0.88f, 0.68f}},
    };

    const fs::path path = fs::path(project_root) / "config" / "gameplay" / "pokemon_attend.json";
    const JsonValue root = parseJsonFile(path.string());
    JsonValue pokemon_file_root = referencedConfig(project_root, path, &root, "pokemonProvider", "pokemonProviderFile");
    JsonValue environment_file_root = referencedConfig(project_root, path, &root, "environment", "environmentFile");
    JsonValue interaction_file_root = referencedConfig(project_root, path, &root, "interaction", "interactionFile");
    JsonValue ui_file_root = referencedConfig(project_root, path, &root, "ui", "uiFile");
    JsonValue audio_file_root = referencedConfig(project_root, path, &root, "audio", "audioFile");
    JsonValue debug_file_root = referencedConfig(project_root, path, &root, "debug", "debugFile");
    if (!debug_file_root.isObject()) {
        debug_file_root = referencedConfig(project_root, path, &root, "preview", "previewFile");
    }
    const JsonValue* pokemon_root = child(&root, "pokemonProvider");
    if (!pokemon_root && pokemon_file_root.isObject()) {
        pokemon_root = child(&pokemon_file_root, "pokemonProvider");
        if (!pokemon_root) pokemon_root = &pokemon_file_root;
    }
    const JsonValue* environment_root = child(&root, "environment");
    if (!environment_root && environment_file_root.isObject()) {
        environment_root = child(&environment_file_root, "environment");
        if (!environment_root) environment_root = &environment_file_root;
    }
    const JsonValue* interaction_root = child(&root, "interaction");
    if (!interaction_root && interaction_file_root.isObject()) {
        interaction_root = child(&interaction_file_root, "interaction");
        if (!interaction_root) interaction_root = &interaction_file_root;
    }
    const JsonValue* ui_root = child(&root, "ui");
    if (!ui_root && ui_file_root.isObject()) {
        ui_root = child(&ui_file_root, "ui");
        if (!ui_root) ui_root = &ui_file_root;
    }
    const JsonValue* audio_root = child(&root, "audio");
    if (!audio_root && audio_file_root.isObject()) {
        audio_root = child(&audio_file_root, "audio");
        if (!audio_root) audio_root = &audio_file_root;
    }
    const std::string active = strOr(child(&root, "activeDebug"),
                                     strOr(child(&root, "activePreview"), strOr(child(&root, "activeScene"), out.id)));
    const JsonValue* selected = nullptr;
    if (const JsonValue* debug = child(&root, "debug"); debug && debug->isObject()) {
        selected = debug;
    }
    if (!selected && debug_file_root.isObject()) {
        selected = child(&debug_file_root, "debug");
        if (!selected) selected = child(&debug_file_root, "preview");
        if (!selected) selected = &debug_file_root;
    }
    if (!selected) {
        const JsonValue* debug_scenes = child(&root, "debugScenes");
        if (!debug_scenes) debug_scenes = child(&root, "previews");
        if (!debug_scenes) debug_scenes = child(&root, "scenes");
        if (debug_scenes && debug_scenes->isArray()) {
            for (const JsonValue& scene : debug_scenes->asArray()) {
                if (scene.isObject() && strOr(child(&scene, "id"), "") == active) {
                    selected = &scene;
                    break;
                }
            }
            if (!selected && !debug_scenes->asArray().empty() && debug_scenes->asArray().front().isObject()) {
                selected = &debug_scenes->asArray().front();
            }
        }
    }
    if (!selected) {
        throw std::runtime_error("pokemon_attend.json has no debug scene");
    }

    parseCamera(child(interaction_root, "camera"), out.camera);
    parseViewportLook(child(interaction_root, "viewportLook"), out.viewport_look);
    parseUi(child(interaction_root, "ui"), project_root, out.ui);
    parseUi(ui_root, project_root, out.ui);
    parseAudio(audio_root, out.audio);
    parseShadow(child(environment_root, "shadow"), out.shadow);
    parseDepthOfField(child(environment_root, "depthOfField"), out.depth_of_field);
    if (const JsonValue* background = child(environment_root, "background")) {
        out.clear_color = parseColor(child(background, "clearColor"), out.clear_color);
    }

    out.id = strOr(child(selected, "id"), out.id);
    parsePokemon(child(selected, "pokemon"), project_root, out.pokemon);
    parseInteractionAdapter(child(selected, "interactionAdapter"), out.interaction_adapter);

    const std::string active_pokemon = strOr(child(selected, "activePokemon"), strOr(child(&root, "activePokemon"), ""));
    if (!active_pokemon.empty()) {
        const JsonValue* defaults = child(pokemon_root, "defaults");
        const JsonValue* profile = catalogEntry(child(pokemon_root, "pokemon"), active_pokemon);
        parseIdleBehavior(child(defaults, "idleBehavior"), out.idle_behavior);
        parseIdleBehavior(child(profile, "idleBehavior"), out.idle_behavior);
        parsePokemonProviderProfile(defaults, profile, active_pokemon, project_root, out.pokemon, out.interaction_adapter);
        parseCamera(child(defaults, "camera"), out.camera);
        parseCamera(child(profile, "camera"), out.camera);
    }
    parseIdleBehavior(child(selected, "idleBehavior"), out.idle_behavior);
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
        if (const JsonValue* framing = firstChild(camera, {"framing", "fullBodyFraming"}); framing && framing->isObject()) {
            out.camera.screen_height_ratio =
                std::clamp(numOrAny(framing, {"screenHeightRatio", "pokemonScreenHeight"}, out.camera.screen_height_ratio), 0.15f, 0.95f);
            out.camera.distance_scale =
                std::clamp(numOrAny(framing, {"distanceScale", "distanceMultiplier"}, out.camera.distance_scale), 0.35f, 2.5f);
            out.camera.min_distance = std::max(0.1f, numOrAny(framing, {"minDistance", "minimumDistance"}, out.camera.min_distance));
            out.camera.max_distance =
                std::max(out.camera.min_distance, numOrAny(framing, {"maxDistance", "maximumDistance"}, out.camera.max_distance));
            out.camera.target_y_ratio =
                std::clamp(numOrAny(framing, {"targetYRatio", "targetHeightRatio"}, out.camera.target_y_ratio), 0.0f, 1.0f);
            out.camera.height_offset = numOrAny(framing, {"heightOffset", "verticalOffset"}, out.camera.height_offset);
            out.camera.depth_padding_scale =
                std::clamp(numOrAny(framing, {"depthPaddingScale", "depthPadding"}, out.camera.depth_padding_scale), 0.0f, 1.5f);
        }
        if (const JsonValue* face = child(camera, "faceFraming"); face && face->isObject()) {
            out.camera.face_screen_height_ratio =
                std::clamp(numOrAny(face, {"screenHeightRatio", "faceScreenHeight"}, out.camera.face_screen_height_ratio), 0.15f, 0.98f);
            out.camera.face_distance_scale =
                std::clamp(numOrAny(face, {"distanceScale", "distanceMultiplier"}, out.camera.face_distance_scale), 0.15f, 2.5f);
            out.camera.face_target_y_ratio =
                std::clamp(numOrAny(face, {"targetYRatio", "headTargetHeightRatio"}, out.camera.face_target_y_ratio), 0.0f, 1.0f);
            out.camera.face_height_offset = numOrAny(face, {"heightOffset", "verticalOffset"}, out.camera.face_height_offset);
            out.camera.face_view_min_model_height =
                std::max(0.0f, numOrAny(face, {"minModelHeight", "minimumPokemonHeight"}, out.camera.face_view_min_model_height));
            out.camera.face_depth_padding_scale =
                std::clamp(numOrAny(face, {"depthPaddingScale", "depthPadding"}, out.camera.face_depth_padding_scale), 0.0f, 1.5f);
        }
        const JsonValue* camera_advanced = child(camera, "advanced");
        if (const JsonValue* fallback = firstChild(camera, {"manualFallback", "manualCameraFallback"}); !fallback) {
            fallback = firstChild(camera_advanced, {"manualFallback", "manualCameraFallback"});
            if (fallback && fallback->isObject()) {
                out.camera.target_x = numOr(child(fallback, "targetX"), out.camera.target_x);
                out.camera.target_z = numOr(child(fallback, "targetZ"), out.camera.target_z);
                out.camera.distance = numOr(child(fallback, "distance"), out.camera.distance);
                out.camera.height = numOr(child(fallback, "height"), out.camera.height);
                out.camera.target_height = numOr(child(fallback, "targetHeight"), out.camera.target_height);
            }
        } else if (fallback->isObject()) {
            out.camera.target_x = numOr(child(fallback, "targetX"), out.camera.target_x);
            out.camera.target_z = numOr(child(fallback, "targetZ"), out.camera.target_z);
            out.camera.distance = numOr(child(fallback, "distance"), out.camera.distance);
            out.camera.height = numOr(child(fallback, "height"), out.camera.height);
            out.camera.target_height = numOr(child(fallback, "targetHeight"), out.camera.target_height);
        }
        if (const JsonValue* lens = firstChild(camera, {"lens"}); !lens) {
            lens = child(camera_advanced, "lens");
            if (lens && lens->isObject()) {
                out.camera.fov_y_degrees = numOr(child(lens, "fovYDegrees"), out.camera.fov_y_degrees);
                out.camera.near_clip = numOr(child(lens, "nearClip"), out.camera.near_clip);
                out.camera.far_clip = numOr(child(lens, "farClip"), out.camera.far_clip);
            }
        } else if (lens->isObject()) {
            out.camera.fov_y_degrees = numOr(child(lens, "fovYDegrees"), out.camera.fov_y_degrees);
            out.camera.near_clip = numOr(child(lens, "nearClip"), out.camera.near_clip);
            out.camera.far_clip = numOr(child(lens, "farClip"), out.camera.far_clip);
        }
        if (const JsonValue* freecam = firstChild(camera, {"freecam", "freeCamera"}); !freecam) {
            freecam = firstChild(camera_advanced, {"freecam", "freeCamera"});
            if (freecam && freecam->isObject()) {
                out.camera.freecam_move_speed =
                    std::max(0.0f, numOr(child(freecam, "moveSpeed"), out.camera.freecam_move_speed));
                out.camera.freecam_mouse_sensitivity =
                    std::max(0.0f, numOr(child(freecam, "mouseSensitivity"), out.camera.freecam_mouse_sensitivity));
                if (const JsonValue* offset = child(freecam, "initialOffset"); offset && offset->isArray()) {
                    const auto& arr = offset->asArray();
                    if (arr.size() > 0 && arr[0].isNumber()) out.camera.freecam_initial_offset_x = numOr(&arr[0], out.camera.freecam_initial_offset_x);
                    if (arr.size() > 1 && arr[1].isNumber()) out.camera.freecam_initial_offset_y = numOr(&arr[1], out.camera.freecam_initial_offset_y);
                    if (arr.size() > 2 && arr[2].isNumber()) out.camera.freecam_initial_offset_z = numOr(&arr[2], out.camera.freecam_initial_offset_z);
                }
                out.camera.freecam_initial_yaw_degrees =
                    numOr(child(freecam, "initialYawDegrees"), numOr(child(freecam, "initialYawDeg"), out.camera.freecam_initial_yaw_degrees));
                out.camera.freecam_initial_pitch_degrees =
                    numOr(child(freecam, "initialPitchDegrees"), numOr(child(freecam, "initialPitchDeg"), out.camera.freecam_initial_pitch_degrees));
                out.camera.freecam_pitch_min_degrees =
                    numOr(child(freecam, "pitchClampMinDegrees"), numOr(child(freecam, "pitchClampMinDeg"), out.camera.freecam_pitch_min_degrees));
                out.camera.freecam_pitch_max_degrees =
                    numOr(child(freecam, "pitchClampMaxDegrees"), numOr(child(freecam, "pitchClampMaxDeg"), out.camera.freecam_pitch_max_degrees));
            }
        } else if (freecam->isObject()) {
            out.camera.freecam_move_speed =
                std::max(0.0f, numOr(child(freecam, "moveSpeed"), out.camera.freecam_move_speed));
            out.camera.freecam_mouse_sensitivity =
                std::max(0.0f, numOr(child(freecam, "mouseSensitivity"), out.camera.freecam_mouse_sensitivity));
            if (const JsonValue* offset = child(freecam, "initialOffset"); offset && offset->isArray()) {
                const auto& arr = offset->asArray();
                if (arr.size() > 0 && arr[0].isNumber()) out.camera.freecam_initial_offset_x = numOr(&arr[0], out.camera.freecam_initial_offset_x);
                if (arr.size() > 1 && arr[1].isNumber()) out.camera.freecam_initial_offset_y = numOr(&arr[1], out.camera.freecam_initial_offset_y);
                if (arr.size() > 2 && arr[2].isNumber()) out.camera.freecam_initial_offset_z = numOr(&arr[2], out.camera.freecam_initial_offset_z);
            }
            out.camera.freecam_initial_yaw_degrees =
                numOr(child(freecam, "initialYawDegrees"), numOr(child(freecam, "initialYawDeg"), out.camera.freecam_initial_yaw_degrees));
            out.camera.freecam_initial_pitch_degrees =
                numOr(child(freecam, "initialPitchDegrees"), numOr(child(freecam, "initialPitchDeg"), out.camera.freecam_initial_pitch_degrees));
            out.camera.freecam_pitch_min_degrees =
                numOr(child(freecam, "pitchClampMinDegrees"), numOr(child(freecam, "pitchClampMinDeg"), out.camera.freecam_pitch_min_degrees));
            out.camera.freecam_pitch_max_degrees =
                numOr(child(freecam, "pitchClampMaxDegrees"), numOr(child(freecam, "pitchClampMaxDeg"), out.camera.freecam_pitch_max_degrees));
        }
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
    const AttendLightingConfig scene_lighting = out.lighting;
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
    const std::string active_floor = strOr(
        child(selected, "activeFloor"),
        strOr(child(environment_root, "activeFloor"), strOr(child(&root, "activeFloor"), out.floor.id)));
    if (!active_floor.empty()) {
        const JsonValue* floor_entry = catalogEntry(child(environment_root, "floors"), active_floor);
        const JsonValue* floor_defaults = child(environment_root, "floorDefaults");
        if (!floor_defaults) floor_defaults = child(environment_root, "environmentDefaults");
        parseFloor(floor_defaults, project_root, out.floor);
        parseFloor(floor_entry, project_root, out.floor);
    }
    if (const JsonValue* floor = child(selected, "floor")) {
        parseFloor(floor, project_root, out.floor);
    }
    applyFloorAnchor(out.pokemon, out.floor);
    const std::string active_wall = strOr(
        child(selected, "activeSky"),
        strOr(
            child(environment_root, "activeSky"),
            strOr(child(&root, "activeSky"), strOr(child(selected, "activeWall"), strOr(child(&root, "activeWall"), out.wall.id)))));
    if (!active_wall.empty()) {
        const JsonValue* wall_defaults = child(environment_root, "skyDefaults");
        if (!wall_defaults) wall_defaults = child(environment_root, "defaults");
        const JsonValue* sky_catalog = child(environment_root, "skyPresets");
        if (sky_catalog && sky_catalog->isObject()) {
            const std::vector<std::string> preset_order = stringArrayOr(child(environment_root, "skyPresetOrder"), {});
            std::vector<std::string> ids = preset_order;
            if (ids.empty()) {
                for (const auto& [id, value] : sky_catalog->asObject()) {
                    if (value.isObject()) ids.push_back(id);
                }
            }
            for (const std::string& id : ids) {
                const JsonValue* preset_value = catalogEntry(sky_catalog, id);
                if (!preset_value || !preset_value->isObject()) continue;
                const JsonValue& value = *preset_value;
                if (!value.isObject()) continue;
                AttendSkyPresetConfig preset;
                preset.id = id;
                preset.label = skyLabelFromId(id);
                preset.wall = out.wall;
                preset.lighting = scene_lighting;
                parseWall(wall_defaults, preset.wall);
                parseLighting(child(wall_defaults, "lighting"), preset.lighting);
                parseWall(&value, preset.wall);
                parseLighting(child(&value, "lighting"), preset.lighting);
                preset.id = strOr(child(&value, "id"), preset.id);
                preset.label = strOr(child(&value, "label"), preset.label);
                out.sky_presets.push_back(std::move(preset));
            }
        }
        parseWall(wall_defaults, out.wall);
        parseLighting(child(wall_defaults, "lighting"), out.lighting);
        const JsonValue* selected_wall = catalogEntry(child(environment_root, "skyPresets"), active_wall);
        if (!selected_wall) {
            selected_wall = catalogEntry(child(environment_root, "walls"), active_wall);
        }
        parseWall(selected_wall, out.wall);
        parseLighting(child(selected_wall, "lighting"), out.lighting);
        for (std::size_t i = 0; i < out.sky_presets.size(); ++i) {
            if (out.sky_presets[i].id == out.wall.id || out.sky_presets[i].id == active_wall) {
                out.active_sky = static_cast<int>(i);
                break;
            }
        }
    }
    if (const JsonValue* wall = child(selected, "wall")) {
        parseWall(wall, out.wall);
        parseLighting(child(wall, "lighting"), out.lighting);
    }
    if (const JsonValue* background = child(selected, "background")) {
        out.clear_color = parseColor(child(background, "clearColor"), out.clear_color);
    }
    parseUi(child(selected, "ui"), project_root, out.ui);
    return out;
}

} // namespace pr::gameplay::attend
