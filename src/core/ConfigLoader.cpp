#include "core/ConfigLoader.hpp"
#include "core/Json.hpp"

#include <stdexcept>

namespace pr {
namespace {

const JsonValue* child(const JsonValue& parent, const std::string& key) { return parent.get(key); }
int asInt(const JsonValue& value) { return static_cast<int>(value.asNumber()); }
double asDouble(const JsonValue& value) { return value.asNumber(); }
std::string asString(const JsonValue& value) { return value.asString(); }
bool asBool(const JsonValue& value) { return value.asBool(); }

void applyColor(Color& out, const JsonValue& obj) {
    if (auto v = child(obj, "r")) out.r = asInt(*v);
    if (auto v = child(obj, "g")) out.g = asInt(*v);
    if (auto v = child(obj, "b")) out.b = asInt(*v);
    if (auto v = child(obj, "a")) out.a = asInt(*v);
}

void applyPoint(Point& out, const JsonValue& obj) {
    if (auto v = child(obj, "x")) out.x = asInt(*v);
    if (auto v = child(obj, "y")) out.y = asInt(*v);
}

template <std::size_t N>
void applyStringArray(std::array<std::string, N>& out, const JsonValue& value, const char* field_name) {
    if (!value.isArray()) {
        throw std::runtime_error(std::string(field_name) + " must be an array");
    }

    const auto& arr = value.asArray();
    if (arr.size() != out.size()) {
        throw std::runtime_error(std::string(field_name) + " must contain exactly " + std::to_string(out.size()) + " labels");
    }

    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = asString(arr[i]);
    }
}

void applyStringVector(std::vector<std::string>& out, const JsonValue& value, const char* field_name) {
    if (!value.isArray()) {
        throw std::runtime_error(std::string(field_name) + " must be an array");
    }

    out.clear();
    for (const JsonValue& item : value.asArray()) {
        out.push_back(asString(item));
    }
}

} // namespace

TitleScreenConfig loadConfigFromJson(const std::string& path) {
    JsonValue root = parseJsonFile(path);
    if (!root.isObject()) {
        throw std::runtime_error("Config root must be an object");
    }

    TitleScreenConfig config;

    if (auto section = child(root, "window")) {
        if (auto v = child(*section, "width")) config.window.width = asInt(*v);
        if (auto v = child(*section, "height")) config.window.height = asInt(*v);
        if (auto v = child(*section, "virtual_width")) config.window.virtual_width = asInt(*v);
        if (auto v = child(*section, "virtual_height")) config.window.virtual_height = asInt(*v);
        if (auto v = child(*section, "title")) config.window.title = asString(*v);
        if (auto v = child(*section, "design_width")) config.window.design_width = asInt(*v);
        if (auto v = child(*section, "design_height")) config.window.design_height = asInt(*v);
    }

    if (auto section = child(root, "assets")) {
        if (auto v = child(*section, "background_a")) config.assets.background_a = asString(*v);
        if (auto v = child(*section, "background_b")) config.assets.background_b = asString(*v);
        if (auto v = child(*section, "button_main")) config.assets.button_main = asString(*v);
        if (auto v = child(*section, "logo_splash")) config.assets.logo_splash = asString(*v);
        if (auto v = child(*section, "logo_main")) config.assets.logo_main = asString(*v);
        if (auto v = child(*section, "logo_main_mask")) config.assets.logo_main_mask = asString(*v);
        if (auto v = child(*section, "font")) config.assets.font = asString(*v);
    }

    if (auto section = child(root, "timings")) {
        if (auto v = child(*section, "splash_fade_in")) config.timings.splash_fade_in = asDouble(*v);
        if (auto v = child(*section, "splash_hold")) config.timings.splash_hold = asDouble(*v);
        if (auto v = child(*section, "splash_fade_out")) config.timings.splash_fade_out = asDouble(*v);
        if (auto v = child(*section, "main_logo_on_black")) config.timings.main_logo_on_black = asDouble(*v);
        if (auto v = child(*section, "white_flash")) config.timings.white_flash = asDouble(*v);
        if (auto v = child(*section, "title_hold_before_prompt")) config.timings.title_hold_before_prompt = asDouble(*v);
        if (auto v = child(*section, "start_transition")) config.timings.start_transition = asDouble(*v);
    }

    if (auto section = child(root, "prompt")) {
        if (auto v = child(*section, "text")) config.prompt.text = asString(*v);
        if (auto v = child(*section, "center_x")) config.prompt.center_x = asInt(*v);
        if (auto v = child(*section, "baseline_y")) config.prompt.baseline_y = asInt(*v);
        if (auto v = child(*section, "font_pt_size")) config.prompt.font_pt_size = asInt(*v);
        if (auto v = child(*section, "blink_cycle_seconds")) config.prompt.blink_cycle_seconds = asDouble(*v);
        if (auto v = child(*section, "color")) applyColor(config.prompt.color, *v);
    }

    if (auto section = child(root, "layout")) {
        if (auto v = child(*section, "splash_logo_center")) applyPoint(config.layout.splash_logo_center, *v);
        if (auto v = child(*section, "main_logo_center")) applyPoint(config.layout.main_logo_center, *v);
        if (auto v = child(*section, "background_a_top_left")) applyPoint(config.layout.background_a_top_left, *v);
        if (auto v = child(*section, "background_b_top_left")) applyPoint(config.layout.background_b_top_left, *v);
    }

    if (auto section = child(root, "transition")) {
        if (auto v = child(*section, "main_logo_end_y")) config.transition.main_logo_end_y = asInt(*v);
        if (auto v = child(*section, "background_a_end_y")) config.transition.background_a_end_y = asInt(*v);
        if (auto v = child(*section, "logo_speed_scale")) {
            const double shared_speed_scale = asDouble(*v);
            config.transition.background_a_speed_scale = shared_speed_scale;
            config.transition.main_logo_speed_scale = shared_speed_scale;
        }
        if (auto v = child(*section, "background_a_speed_scale")) config.transition.background_a_speed_scale = asDouble(*v);
        if (auto v = child(*section, "main_logo_speed_scale")) config.transition.main_logo_speed_scale = asDouble(*v);
        if (auto v = child(*section, "menu_intro_duration")) config.menu.animation.intro_duration = asDouble(*v);
        if (auto v = child(*section, "menu_outro_duration")) config.menu.animation.outro_duration = asDouble(*v);
        if (auto v = child(*section, "menu_slide_distance")) config.menu.animation.slide_distance = asInt(*v);
        if (auto v = child(*section, "fade_prompt_out")) config.transition.fade_prompt_out = asBool(*v);
    }

    if (auto section = child(root, "menu")) {
        if (auto v = child(*section, "items")) applyStringArray(config.menu.items, *v, "menu.items");
        if (auto v = child(*section, "center_x")) config.menu.center_x = asInt(*v);
        if (auto v = child(*section, "top_y")) config.menu.top_y = asInt(*v);
        if (auto v = child(*section, "vertical_spacing")) config.menu.vertical_spacing = asInt(*v);
        if (auto v = child(*section, "font_pt_size")) config.menu.font_pt_size = asInt(*v);
        if (auto v = child(*section, "text_color")) applyColor(config.menu.text_color, *v);
        if (auto animation = child(*section, "animation")) {
            if (auto v = child(*animation, "intro_duration")) config.menu.animation.intro_duration = asDouble(*v);
            if (auto v = child(*animation, "outro_duration")) config.menu.animation.outro_duration = asDouble(*v);
            if (auto v = child(*animation, "slide_distance")) config.menu.animation.slide_distance = asInt(*v);
            if (auto v = child(*animation, "bounce")) config.menu.animation.bounce = asDouble(*v);
        }
        if (auto selection = child(*section, "selection")) {
            if (auto v = child(*selection, "beat_speed")) config.menu.selection.beat_speed = asDouble(*v);
            if (auto v = child(*selection, "beat_magnitude")) config.menu.selection.beat_magnitude = asDouble(*v);
        }
        if (auto section_transition = child(*section, "section_transition")) {
            if (auto v = child(*section_transition, "button_out_duration")) config.menu.section_transition.button_out_duration = asDouble(*v);
            if (auto v = child(*section_transition, "fade_duration")) config.menu.section_transition.fade_duration = asDouble(*v);
        }
    }

    if (auto section = child(root, "shine")) {
        if (auto v = child(*section, "enabled")) config.shine.enabled = asBool(*v);
        if (auto v = child(*section, "delay_seconds")) config.shine.delay_seconds = asDouble(*v);
        if (auto v = child(*section, "duration_seconds")) config.shine.duration_seconds = asDouble(*v);
        if (auto v = child(*section, "repeat_count")) config.shine.repeat_count = asInt(*v);
        if (auto v = child(*section, "gap_seconds")) config.shine.gap_seconds = asDouble(*v);
        if (auto v = child(*section, "band_width")) config.shine.band_width = asInt(*v);
        if (auto v = child(*section, "max_alpha")) config.shine.max_alpha = asInt(*v);
        if (auto v = child(*section, "travel_padding")) config.shine.travel_padding = asInt(*v);
        if (auto v = child(*section, "use_additive_blend")) config.shine.use_additive_blend = asBool(*v);
    }

    if (auto section = child(root, "input")) {
        if (auto v = child(*section, "accept_any_key")) config.input.accept_any_key = asBool(*v);
        if (auto v = child(*section, "accept_mouse")) config.input.accept_mouse = asBool(*v);
        if (auto v = child(*section, "accept_controller")) config.input.accept_controller = asBool(*v);
        if (auto v = child(*section, "navigate_up_keys")) applyStringVector(config.input.navigate_up_keys, *v, "input.navigate_up_keys");
        if (auto v = child(*section, "navigate_down_keys")) applyStringVector(config.input.navigate_down_keys, *v, "input.navigate_down_keys");
        if (auto v = child(*section, "forward_keys")) applyStringVector(config.input.forward_keys, *v, "input.forward_keys");
        if (auto v = child(*section, "back_keys")) applyStringVector(config.input.back_keys, *v, "input.back_keys");
    }

    if (auto section = child(root, "audio")) {
        if (auto v = child(*section, "menu_music")) config.audio.menu_music = asString(*v);
        if (auto v = child(*section, "button_sfx")) config.audio.button_sfx = asString(*v);
        if (auto v = child(*section, "music_volume")) config.audio.music_volume = asInt(*v);
        if (auto v = child(*section, "ui_volume")) config.audio.sfx_volume = asInt(*v);
        if (auto v = child(*section, "sfx_volume")) config.audio.sfx_volume = asInt(*v);
    }

    if (auto section = child(root, "persistence")) {
        if (auto v = child(*section, "save_options")) config.persistence.save_options = asBool(*v);
        if (auto v = child(*section, "organization")) config.persistence.organization = asString(*v);
        if (auto v = child(*section, "application")) config.persistence.application = asString(*v);
        if (auto v = child(*section, "save_file_name")) config.persistence.save_file_name = asString(*v);
        if (auto v = child(*section, "backup_file_name")) config.persistence.backup_file_name = asString(*v);
    }

    if (auto section = child(root, "skip")) {
        if (auto v = child(*section, "splash_fade_in")) config.skip.splash_fade_in = asBool(*v);
        if (auto v = child(*section, "splash_hold")) config.skip.splash_hold = asBool(*v);
        if (auto v = child(*section, "splash_fade_out")) config.skip.splash_fade_out = asBool(*v);
        if (auto v = child(*section, "main_logo_on_black")) config.skip.main_logo_on_black = asBool(*v);
        if (auto v = child(*section, "white_flash")) config.skip.white_flash = asBool(*v);
        if (auto v = child(*section, "title_hold")) config.skip.title_hold = asBool(*v);
        if (auto v = child(*section, "waiting_for_start")) config.skip.waiting_for_start = asBool(*v);
        if (auto v = child(*section, "start_transition")) config.skip.start_transition = asBool(*v);
        if (auto v = child(*section, "main_menu_intro")) config.skip.main_menu_intro = asBool(*v);
        if (auto v = child(*section, "main_menu_idle")) config.skip.main_menu_idle = asBool(*v);
    }

    return config;
}

} // namespace pr
