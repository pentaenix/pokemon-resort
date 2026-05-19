#include "ui/transfer_system/summary/PokemonSummaryConfig.hpp"

#include "core/config/Json.hpp"

#include <algorithm>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace pr::transfer_system {
namespace {

Color parseHexColorString(const std::string& value, const Color& fallback) {
    if (value.size() != 7 || value[0] != '#') {
        return fallback;
    }
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    };
    auto component = [&](int index) -> int {
        const int hi = hex(value[static_cast<std::size_t>(index)]);
        const int lo = hex(value[static_cast<std::size_t>(index + 1)]);
        return (hi < 0 || lo < 0) ? -1 : ((hi << 4) | lo);
    };
    const int r = component(1);
    const int g = component(3);
    const int b = component(5);
    return (r < 0 || g < 0 || b < 0) ? fallback : Color{r, g, b, 255};
}

std::string resolveColorToken(const std::string& raw, const JsonValue& tokens) {
    if (raw.size() >= 2 && raw[0] == '$' && tokens.isObject()) {
        const std::string key = raw.substr(1);
        if (const JsonValue* v = tokens.get(key); v && v->isString()) {
            return v->asString();
        }
    }
    return raw;
}

const JsonValue* objectChild(const JsonValue* value, const std::string& key) {
    if (!value || !value->isObject()) {
        return nullptr;
    }
    const JsonValue* child = value->get(key);
    return (child && child->isObject()) ? child : nullptr;
}

double doubleFromObjectOrDefault(const JsonValue& obj, const std::string& key, double fallback) {
    const JsonValue* value = obj.get(key);
    return value ? value->asNumber() : fallback;
}

int intFromObjectOrDefault(const JsonValue& obj, const std::string& key, int fallback) {
    const JsonValue* value = obj.get(key);
    return value ? static_cast<int>(value->asNumber()) : fallback;
}

bool boolFromObjectOrDefault(const JsonValue& obj, const std::string& key, bool fallback) {
    const JsonValue* value = obj.get(key);
    return value ? value->asBool() : fallback;
}

void applyColor(Color& out, const JsonValue& obj, const std::string& key, const JsonValue& tokens) {
    const JsonValue* value = obj.get(key);
    if (!value || !value->isString()) {
        return;
    }
    out = parseHexColorString(resolveColorToken(value->asString(), tokens), out);
}

} // namespace

LoadedPokemonSummary loadPokemonSummary(const std::string& project_root) {
    LoadedPokemonSummary out;
    const fs::path design_path = fs::path(project_root) / "config" / "design.json";
    JsonValue tokens;
    if (fs::exists(design_path)) {
        JsonValue design_root = parseJsonFile(design_path.string());
        if (design_root.isObject()) {
            if (const JsonValue* t = objectChild(&design_root, "tokens")) {
                tokens = *t;
            }
        }
    }

    const fs::path path = fs::path(project_root) / "config" / "pokemon_summary.json";
    if (!fs::exists(path)) {
        return out;
    }
    JsonValue root = parseJsonFile(path.string());
    if (!root.isObject()) {
        return out;
    }
    if (const JsonValue* panel = objectChild(&root, "panel")) {
        PokemonSummaryPanelStyle& s = out.panel;
        const JsonValue& o = *panel;
        s.enabled = boolFromObjectOrDefault(o, "enabled", s.enabled);
        s.enter_smoothing = std::max(1.0, doubleFromObjectOrDefault(o, "enter_smoothing", s.enter_smoothing));
        s.exit_smoothing = std::max(1.0, doubleFromObjectOrDefault(o, "exit_smoothing", s.exit_smoothing));
        s.retracted_box_smoothing =
            std::max(1.0, doubleFromObjectOrDefault(o, "retracted_box_smoothing", s.retracted_box_smoothing));
        s.width = std::max(120, intFromObjectOrDefault(o, "width", s.width));
        s.height = std::max(120, intFromObjectOrDefault(o, "height", s.height));
        s.top_y = std::max(0, intFromObjectOrDefault(o, "top_y", s.top_y));
        s.corner_radius = std::max(0, intFromObjectOrDefault(o, "corner_radius", s.corner_radius));
        s.border_thickness = std::max(0, intFromObjectOrDefault(o, "border_thickness", s.border_thickness));
        applyColor(s.fill_color, o, "fill_color", tokens);
        applyColor(s.border_color, o, "border_color", tokens);
        s.open_when_game_box_absent =
            boolFromObjectOrDefault(o, "open_when_game_box_absent", s.open_when_game_box_absent);
        s.temporary_name_font_pt = std::max(8, intFromObjectOrDefault(o, "temporary_name_font_pt", s.temporary_name_font_pt));
        applyColor(s.temporary_name_color, o, "temporary_name_color", tokens);
    }
    return out;
}

} // namespace pr::transfer_system
