#include "ui/loading/ResortTransferLoadingConfig.hpp"

#include "core/Json.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace pr {
namespace {

const JsonValue* child(const JsonValue& parent, const std::string& key) {
    return parent.get(key);
}

int asInt(const JsonValue& value) {
    return static_cast<int>(value.asNumber());
}

double asDouble(const JsonValue& value) {
    return value.asNumber();
}

std::string asString(const JsonValue& value) {
    return value.asString();
}

bool asBool(const JsonValue& value) {
    return value.asBool();
}

std::string normalizeMessageText(std::string text) {
    for (std::size_t pos = 0; (pos = text.find("\\n", pos)) != std::string::npos;) {
        text.replace(pos, 2, "\n");
        ++pos;
    }
    for (std::size_t pos = 0; (pos = text.find("/n", pos)) != std::string::npos;) {
        text.replace(pos, 2, "\n");
        ++pos;
    }
    return text;
}

std::vector<double> asDoubleArray(const JsonValue& value) {
    if (!value.isArray()) {
        throw std::runtime_error("expected an array");
    }
    std::vector<double> out;
    const auto& arr = value.asArray();
    out.reserve(arr.size());
    for (const auto& item : arr) {
        out.push_back(item.asNumber());
    }
    return out;
}

int hexByte(const std::string& value, std::size_t offset) {
    return std::stoi(value.substr(offset, 2), nullptr, 16);
}

void applyColor(Color& out, const JsonValue& value) {
    if (!value.isString()) {
        throw std::runtime_error("resort transfer loading colors must be #RRGGBB strings");
    }
    const std::string hex = value.asString();
    if ((hex.size() != 7 && hex.size() != 9) || hex[0] != '#') {
        throw std::runtime_error("resort transfer loading colors must use #RRGGBB or #RRGGBBAA");
    }
    out.r = hexByte(hex, 1);
    out.g = hexByte(hex, 3);
    out.b = hexByte(hex, 5);
    out.a = hex.size() == 9 ? hexByte(hex, 7) : 255;
}

LoadingEase parseEase(const std::string& value, LoadingEase fallback) {
    if (value == "linear") return LoadingEase::Linear;
    if (value == "ease_in_cubic") return LoadingEase::EaseInCubic;
    if (value == "ease_out_cubic") return LoadingEase::EaseOutCubic;
    if (value == "ease_in_out_cubic") return LoadingEase::EaseInOutCubic;
    return fallback;
}

TemporalLoadingDemoType parseTemporalLoadingType(const std::string& value, TemporalLoadingDemoType fallback) {
    if (value == "pokeball") return TemporalLoadingDemoType::Pokeball;
    if (value == "quick_boat_pass") return TemporalLoadingDemoType::QuickBoatPass;
    if (value == "boat_loading" || value == "resort_transfer") return TemporalLoadingDemoType::BoatLoading;
    return fallback;
}

void applyStage(LoadingStageConfig& out, const JsonValue& obj) {
    if (auto v = child(obj, "start")) out.start_seconds = asDouble(*v);
    if (auto v = child(obj, "duration")) out.duration_seconds = std::max(0.01, asDouble(*v));
    if (auto v = child(obj, "ease")) out.ease = parseEase(asString(*v), out.ease);
}

void applyWave(ResortTransferWaveConfig& out, const JsonValue& obj) {
    if (auto v = child(obj, "bottom_offset")) out.bottom_offset = asInt(*v);
    if (auto v = child(obj, "color")) applyColor(out.color, *v);
    if (auto v = child(obj, "amplitude")) out.amplitude = asDouble(*v);
    if (auto v = child(obj, "wavelength")) out.wavelength = std::max(1.0, asDouble(*v));
    if (auto v = child(obj, "horizontal_speed")) out.horizontal_speed = asDouble(*v);
    if (auto v = child(obj, "phase")) out.phase = asDouble(*v);
}

fs::path resolvePath(const std::string& root, const std::string& configured) {
    fs::path path(configured);
    return path.is_absolute() ? path : (fs::path(root) / path);
}

} // namespace

double applyLoadingEase(LoadingEase ease, double value) {
    value = std::clamp(value, 0.0, 1.0);
    switch (ease) {
        case LoadingEase::Linear:
            return value;
        case LoadingEase::EaseInCubic:
            return value * value * value;
        case LoadingEase::EaseOutCubic: {
            const double inv = 1.0 - value;
            return 1.0 - inv * inv * inv;
        }
        case LoadingEase::EaseInOutCubic:
            return value < 0.5
                ? 4.0 * value * value * value
                : 1.0 - std::pow(-2.0 * value + 2.0, 3.0) / 2.0;
    }
    return value;
}

double loadingStageProgress(const LoadingStageConfig& stage, double elapsed_seconds) {
    const double raw = (elapsed_seconds - stage.start_seconds) / std::max(0.01, stage.duration_seconds);
    return applyLoadingEase(stage.ease, raw);
}

double loadingSequenceDuration(const LoadingStageConfig& a, const LoadingStageConfig& b, const LoadingStageConfig& c) {
    return std::max({a.start_seconds + a.duration_seconds, b.start_seconds + b.duration_seconds, c.start_seconds + c.duration_seconds});
}

ResortTransferLoadingConfig loadResortTransferLoadingConfig(const std::string& project_root) {
    ResortTransferLoadingConfig config;
    const fs::path path = resolvePath(project_root, "config/loading_screen.json");
    if (!fs::exists(path)) {
        return config;
    }

    const JsonValue root = parseJsonFile(path.string());
    const JsonValue* section = root.isObject() ? root.get("resort_transfer") : nullptr;
    if (!section || !section->isObject()) {
        return config;
    }

    if (auto assets = child(*section, "assets")) {
        if (auto v = child(*assets, "boat")) config.assets.boat = asString(*v);
        if (auto v = child(*assets, "cloud1")) config.assets.cloud1 = asString(*v);
        if (auto v = child(*assets, "cloud2")) config.assets.cloud2 = asString(*v);
    }
    if (auto colors = child(*section, "colors")) {
        if (auto v = child(*colors, "sky")) applyColor(config.colors.sky, *v);
        if (auto v = child(*colors, "sun")) applyColor(config.colors.sun, *v);
        if (auto v = child(*colors, "foam")) {
            applyColor(config.colors.foam, *v);
            config.foam.color = config.colors.foam;
        }
        if (auto v = child(*colors, "clouds")) applyColor(config.colors.clouds, *v);
        if (auto v = child(*colors, "border")) applyColor(config.colors.border, *v);
    }
    if (auto timing = child(*section, "timing")) {
        if (auto v = child(*timing, "intro_water_sun")) applyStage(config.timing.intro_water_sun, *v);
        if (auto v = child(*timing, "intro_clouds")) applyStage(config.timing.intro_clouds, *v);
        if (auto v = child(*timing, "intro_boat")) applyStage(config.timing.intro_boat, *v);
        if (auto v = child(*timing, "outro_boat")) applyStage(config.timing.outro_boat, *v);
        if (auto v = child(*timing, "outro_water_sun")) applyStage(config.timing.outro_water_sun, *v);
        if (auto v = child(*timing, "outro_clouds")) applyStage(config.timing.outro_clouds, *v);
    }
    if (auto quick = child(*section, "quick_pass")) {
        if (auto v = child(*quick, "duration")) config.quick_pass.duration_seconds = std::max(0.01, asDouble(*v));
        if (auto v = child(*quick, "duration_seconds")) config.quick_pass.duration_seconds = std::max(0.01, asDouble(*v));
        if (auto v = child(*quick, "ease")) config.quick_pass.ease = parseEase(asString(*v), config.quick_pass.ease);
        if (auto v = child(*quick, "water_in_fraction")) config.quick_pass.water_in_fraction = std::clamp(asDouble(*v), 0.01, 1.0);
        if (auto v = child(*quick, "water_out_start")) config.quick_pass.water_out_start = std::clamp(asDouble(*v), 0.0, 1.0);
        if (auto v = child(*quick, "water_out_fraction")) config.quick_pass.water_out_fraction = std::clamp(asDouble(*v), 0.01, 1.0);
        if (auto v = child(*quick, "cloud_exit_start")) config.quick_pass.cloud_exit_start = std::clamp(asDouble(*v), 0.0, 1.0);
        if (auto v = child(*quick, "cloud_exit_fraction")) config.quick_pass.cloud_exit_fraction = std::clamp(asDouble(*v), 0.01, 1.0);
    }
    if (auto v = child(*section, "minimum_loop_seconds")) {
        config.minimum_loop_seconds = std::max(0.0, asDouble(*v));
    }
    if (auto border = child(*section, "border")) {
        if (auto v = child(*border, "inset")) config.border.inset = asInt(*v);
        if (auto v = child(*border, "radius")) config.border.radius = asInt(*v);
        if (auto v = child(*border, "stroke")) config.border.stroke = asInt(*v);
    }
    if (auto sun = child(*section, "sun")) {
        if (auto v = child(*sun, "diameter")) config.sun.diameter = asInt(*v);
        if (auto v = child(*sun, "center_x_ratio")) config.sun.center_x_ratio = asDouble(*v);
        if (auto v = child(*sun, "bottom_offset")) config.sun.bottom_offset = asInt(*v);
        if (auto v = child(*sun, "offscreen_padding")) config.sun.offscreen_padding = asInt(*v);
    }
    if (auto boat = child(*section, "boat")) {
        if (auto v = child(*boat, "center_x_ratio")) config.boat.center_x_ratio = asDouble(*v);
        if (auto v = child(*boat, "bottom_offset")) config.boat.bottom_offset = asInt(*v);
        if (auto v = child(*boat, "scale")) config.boat.scale = std::max(0.01, asDouble(*v));
        if (auto v = child(*boat, "bob_pixels")) config.boat.bob_pixels = asDouble(*v);
        if (auto v = child(*boat, "bob_seconds")) config.boat.bob_seconds = std::max(0.01, asDouble(*v));
        if (auto v = child(*boat, "moving_bob_pixels")) config.boat.moving_bob_pixels = asDouble(*v);
    }
    if (auto clouds = child(*section, "clouds")) {
        if (auto v = child(*clouds, "cloud1_bottom_offset")) config.clouds.cloud1_bottom_offset = asInt(*v);
        if (auto v = child(*clouds, "cloud2_bottom_offset")) config.clouds.cloud2_bottom_offset = asInt(*v);
        if (auto v = child(*clouds, "spacing")) config.clouds.spacing = asInt(*v);
        if (auto v = child(*clouds, "base_x_ratio")) config.clouds.base_x_ratio = asDouble(*v);
        if (auto v = child(*clouds, "drift_speed")) config.clouds.drift_speed = asDouble(*v);
        if (auto v = child(*clouds, "exit_speed")) config.clouds.exit_speed = asDouble(*v);
        if (auto v = child(*clouds, "exit_boat_distance_scale")) config.clouds.exit_boat_distance_scale = asDouble(*v);
    }
    if (auto message = child(*section, "message")) {
        if (auto v = child(*message, "font")) config.message.font = asString(*v);
        if (auto v = child(*message, "font_size")) config.message.font_size = std::max(1, asInt(*v));
        if (auto v = child(*message, "color")) applyColor(config.message.color, *v);
        if (auto v = child(*message, "center_x_ratio")) config.message.center_x_ratio = asDouble(*v);
        if (auto v = child(*message, "center_bottom_offset")) config.message.center_bottom_offset = asInt(*v);
        if (auto v = child(*message, "line_spacing")) config.message.line_spacing = std::max(0, asInt(*v));
        if (auto v = child(*message, "max_width_ratio")) config.message.max_width_ratio = std::clamp(asDouble(*v), 0.05, 1.0);
        if (auto v = child(*message, "show_in_idle")) config.message.show_in_idle = asBool(*v);
        if (auto v = child(*message, "intro")) applyStage(config.message.intro, *v);
        if (auto v = child(*message, "outro")) applyStage(config.message.outro, *v);
        if (auto v = child(*message, "enter_y_offset")) config.message.enter_y_offset = asDouble(*v);
        if (auto v = child(*message, "default_key")) config.message.default_key = asString(*v);
        if (auto texts = child(*message, "texts"); texts && texts->isObject()) {
            config.message.texts.clear();
            for (const auto& [key, value] : texts->asObject()) {
                if (value.isString()) {
                    config.message.texts[key] = normalizeMessageText(value.asString());
                }
            }
        }
    }
    if (auto foam = child(*section, "foam")) {
        if (auto v = child(*foam, "color")) applyColor(config.foam.color, *v);
        if (auto v = child(*foam, "y_offset_from_boat_bottom")) config.foam.y_offset_from_boat_bottom = asDouble(*v);
        if (auto v = child(*foam, "overlap_with_hull")) config.foam.overlap_with_hull = asDouble(*v);
        if (auto v = child(*foam, "total_width_percent_of_boat_width")) config.foam.total_width_percent_of_boat_width = asDouble(*v);
        if (auto v = child(*foam, "front_extension")) config.foam.front_extension = asDouble(*v);
        if (auto v = child(*foam, "back_extension")) config.foam.back_extension = asDouble(*v);
        if (auto v = child(*foam, "thickness")) config.foam.thickness = asDouble(*v);
        if (auto v = child(*foam, "bow_splash_width")) config.foam.bow_splash_width = asDouble(*v);
        if (auto v = child(*foam, "bow_splash_height")) config.foam.bow_splash_height = asDouble(*v);
        if (auto v = child(*foam, "speed_crest_count")) config.foam.speed_crest_count = std::max(0, asInt(*v));
        if (auto v = child(*foam, "speed_crest_spacing")) config.foam.speed_crest_spacing = asDouble(*v);
        if (auto v = child(*foam, "speed_crest_front_extension")) {
            config.foam.speed_crest_front_extension = asDouble(*v);
            config.foam.speed_crest_front_x_offset = config.foam.speed_crest_front_extension;
        }
        if (auto v = child(*foam, "speed_crest_front_x_offset")) config.foam.speed_crest_front_x_offset = asDouble(*v);
        if (auto v = child(*foam, "speed_crest_height")) config.foam.speed_crest_height = asDouble(*v);
        if (auto v = child(*foam, "speed_crest_heights")) config.foam.speed_crest_heights = asDoubleArray(*v);
        if (auto v = child(*foam, "speed_crest_y_offset")) config.foam.speed_crest_y_offset = asDouble(*v);
        if (auto v = child(*foam, "speed_crest_y_offsets")) config.foam.speed_crest_y_offsets = asDoubleArray(*v);
        if (auto v = child(*foam, "speed_crest_thickness")) config.foam.speed_crest_thickness = asDouble(*v);
        if (auto v = child(*foam, "speed_crest_angle_offset")) config.foam.speed_crest_angle_offset = asDouble(*v);
        if (auto v = child(*foam, "speed_crest_angle_offsets")) config.foam.speed_crest_angle_offsets = asDoubleArray(*v);
        if (auto v = child(*foam, "speed_crest_angle_degrees")) config.foam.speed_crest_angle_degrees = asDouble(*v);
        if (auto v = child(*foam, "speed_crest_angle_degrees_list")) config.foam.speed_crest_angle_degrees_list = asDoubleArray(*v);
        if (auto v = child(*foam, "speed_crest_front_pulse_amount")) config.foam.speed_crest_front_pulse_amount = asDouble(*v);
        if (auto v = child(*foam, "speed_crest_front_pulse_seconds")) config.foam.speed_crest_front_pulse_seconds = std::max(0.01, asDouble(*v));
        if (auto v = child(*foam, "speed_crest_scroll_speed")) config.foam.speed_crest_scroll_speed = asDouble(*v);
        if (auto v = child(*foam, "main_scallop_count")) config.foam.main_scallop_count = std::max(1, asInt(*v));
        if (auto v = child(*foam, "front_scallop_amplitude")) config.foam.front_scallop_amplitude = asDouble(*v);
        if (auto v = child(*foam, "back_scallop_amplitude")) config.foam.back_scallop_amplitude = asDouble(*v);
        if (auto v = child(*foam, "front_scallop_wavelength")) config.foam.front_scallop_wavelength = std::max(1.0, asDouble(*v));
        if (auto v = child(*foam, "back_scallop_wavelength")) config.foam.back_scallop_wavelength = std::max(1.0, asDouble(*v));
        if (auto v = child(*foam, "scroll_speed")) config.foam.scroll_speed = asDouble(*v);
        if (auto v = child(*foam, "velocity_influence")) config.foam.velocity_influence = asDouble(*v);
        if (auto v = child(*foam, "foam_world_phase_coupling")) {
            config.foam.foam_world_phase_coupling = asDouble(*v);
        } else if (auto v = child(*foam, "foam_outro_world_phase_coupling")) {
            config.foam.foam_world_phase_coupling = asDouble(*v);
        }
        if (auto v = child(*foam, "trailing_stretch_amount")) config.foam.trailing_stretch_amount = asDouble(*v);
        if (auto v = child(*foam, "stern_taper")) config.foam.stern_taper = std::clamp(asDouble(*v), 0.05, 1.0);
        if (auto v = child(*foam, "segments")) config.foam.segments = std::max(8, asInt(*v));
    }
    if (auto waves = child(*section, "waves")) {
        if (auto v = child(*waves, "segments")) config.waves.segments = std::max(8, asInt(*v));
        if (auto v = child(*waves, "sink_padding")) config.waves.sink_padding = asDouble(*v);
        if (auto layers = child(*waves, "layers"); layers && layers->isArray()) {
            const auto& arr = layers->asArray();
            for (std::size_t i = 0; i < arr.size() && i < config.waves.layers.size(); ++i) {
                applyWave(config.waves.layers[i], arr[i]);
            }
        }
    }
    if (auto temporal = child(*section, "temporal"); temporal && temporal->isObject()) {
        config.temporal.clear();
        for (const auto& [key, value] : temporal->asObject()) {
            if (!value.isObject()) {
                continue;
            }
            TemporalLoadingDemoConfig demo;
            if (auto v = child(value, "loading_type")) demo.loading_type = parseTemporalLoadingType(asString(*v), demo.loading_type);
            if (auto v = child(value, "screen")) demo.loading_type = parseTemporalLoadingType(asString(*v), demo.loading_type);
            if (auto v = child(value, "message_key")) demo.message_key = asString(*v);
            if (auto v = child(value, "duration")) demo.simulated_load_duration_seconds = std::max(0.0, asDouble(*v));
            if (auto v = child(value, "simulated_load_duration")) demo.simulated_load_duration_seconds = std::max(0.0, asDouble(*v));
            if (auto v = child(value, "simulated_load_duration_seconds")) demo.simulated_load_duration_seconds = std::max(0.0, asDouble(*v));
            config.temporal[key] = demo;
        }
    }
    return config;
}

} // namespace pr
