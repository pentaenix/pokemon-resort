#include "ui/transfer_system/TransferSaveConfig.hpp"

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
    if (r < 0 || g < 0 || b < 0) {
        return fallback;
    }
    return Color{r, g, b, 255};
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

std::string stringFromObjectOrDefault(const JsonValue& obj, const std::string& key, std::string fallback) {
    const JsonValue* value = obj.get(key);
    return (value && value->isString()) ? value->asString() : std::move(fallback);
}

const JsonValue* objectChild(const JsonValue* value, const std::string& key) {
    if (!value || !value->isObject()) {
        return nullptr;
    }
    const JsonValue* child = value->get(key);
    return (child && child->isObject()) ? child : nullptr;
}

void applyColor(Color& out, const JsonValue& obj, const std::string& key, const JsonValue& tokens) {
    out = parseHexColorString(resolveColorToken(stringFromObjectOrDefault(obj, key, ""), tokens), out);
}

} // namespace

LoadedTransferSave loadTransferSave(const std::string& project_root) {
    LoadedTransferSave out;
    const fs::path design_path = fs::path(project_root) / "config" / "design.json";
    JsonValue design_root = parseJsonFile(design_path.string());
    const JsonValue tokens = (design_root.isObject() && design_root.get("tokens") && design_root.get("tokens")->isObject())
        ? *design_root.get("tokens")
        : JsonValue{};
    const fs::path path = fs::path(project_root) / "config" / "transfer_save.json";
    JsonValue root = parseJsonFile(path.string());
    if (!root.isObject()) {
        return out;
    }
    if (const JsonValue* modal = objectChild(&root, "exit_save_modal")) {
        ExitSaveModalStyle& s = out.exit_save_modal;
        const JsonValue& o = *modal;
        s.enabled = boolFromObjectOrDefault(o, "enabled", s.enabled);
        s.enter_smoothing = std::max(1.0, doubleFromObjectOrDefault(o, "enter_smoothing", s.enter_smoothing));
        s.exit_smoothing = std::max(1.0, doubleFromObjectOrDefault(o, "exit_smoothing", s.exit_smoothing));
        s.offscreen_pad = std::max(0, intFromObjectOrDefault(o, "offscreen_pad", s.offscreen_pad));
        s.shown_x = std::max(0, intFromObjectOrDefault(o, "shown_x", s.shown_x));
        s.gap_above_info_banner = std::max(0, intFromObjectOrDefault(o, "gap_above_info_banner", s.gap_above_info_banner));
        s.width = std::max(120, intFromObjectOrDefault(o, "width", s.width));
        s.row_height = std::max(24, intFromObjectOrDefault(o, "row_height", s.row_height));
        s.row_gap = std::max(0, intFromObjectOrDefault(o, "row_gap", s.row_gap));
        s.padding_x = std::max(0, intFromObjectOrDefault(o, "padding_x", s.padding_x));
        s.padding_top = std::max(0, intFromObjectOrDefault(o, "padding_top", s.padding_top));
        s.padding_bottom = std::max(0, intFromObjectOrDefault(o, "padding_bottom", s.padding_bottom));
        s.corner_radius = std::max(0, intFromObjectOrDefault(o, "corner_radius", s.corner_radius));
        s.border_thickness = std::max(0, intFromObjectOrDefault(o, "border_thickness", s.border_thickness));
        s.dim_background = boolFromObjectOrDefault(o, "dim_background", s.dim_background);
        applyColor(s.dim_color, o, "dim_color", tokens);
        s.dim_alpha = std::clamp(intFromObjectOrDefault(o, "dim_alpha", s.dim_alpha), 0, 255);
        applyColor(s.card_fill, o, "card_fill", tokens);
        s.card_fill_alpha = std::clamp(intFromObjectOrDefault(o, "card_fill_alpha", s.card_fill_alpha), 0, 255);
        applyColor(s.card_border, o, "card_border", tokens);
        s.card_border_alpha = std::clamp(intFromObjectOrDefault(o, "card_border_alpha", s.card_border_alpha), 0, 255);
        applyColor(s.row_fill, o, "row_fill", tokens);
        s.row_fill_alpha = std::clamp(intFromObjectOrDefault(o, "row_fill_alpha", s.row_fill_alpha), 0, 255);
        applyColor(s.row_border, o, "row_border", tokens);
        s.row_border_alpha = std::clamp(intFromObjectOrDefault(o, "row_border_alpha", s.row_border_alpha), 0, 255);
        applyColor(s.selected_row_fill, o, "selected_row_fill", tokens);
        s.selected_row_fill_alpha = std::clamp(intFromObjectOrDefault(o, "selected_row_fill_alpha", s.selected_row_fill_alpha), 0, 255);
        applyColor(s.selected_row_border, o, "selected_row_border", tokens);
        s.selected_row_border_alpha = std::clamp(intFromObjectOrDefault(o, "selected_row_border_alpha", s.selected_row_border_alpha), 0, 255);
        applyColor(s.text_color, o, "text_color", tokens);
        s.text_alpha = std::clamp(intFromObjectOrDefault(o, "text_alpha", s.text_alpha), 0, 255);
        s.font_pt = std::max(8, intFromObjectOrDefault(o, "font_pt", s.font_pt));
    }
    return out;
}

} // namespace pr::transfer_system

