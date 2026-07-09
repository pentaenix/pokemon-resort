#include "gameplay/world3d/dialogue/OverworldTextboxConfig.hpp"

#include "core/config/Json.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>

namespace pr::gameplay::world3d::dialogue {

namespace fs = std::filesystem;

namespace {

int intOr(const JsonValue* value, int fallback) {
    return value && value->isNumber() ? static_cast<int>(value->asNumber()) : fallback;
}

std::string stringOr(const JsonValue* value, const std::string& fallback) {
    return value && value->isString() ? value->asString() : fallback;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

} // namespace

OverworldTextboxConfig loadOverworldTextboxConfig(const std::string& project_root) {
    OverworldTextboxConfig out{};
    const fs::path path = fs::path(project_root) / "config" / "gameplay" / "world3d" / "textbox.json";
    try {
        const JsonValue root = parseJsonFile(path.string());
        if (!root.isObject()) {
            return out;
        }
        const JsonValue* textbox = root.get("textbox");
        if (!textbox || !textbox->isObject()) {
            return out;
        }

        out.visible_mode = stringOr(textbox->get("visibleMode"), out.visible_mode);
        out.selected_skin_index = intOr(textbox->get("selectedSkinIndex"), out.selected_skin_index);
        out.valid_skin_count = intOr(textbox->get("validSkinCount"), out.valid_skin_count);
        out.sprite_sheet_path = stringOr(textbox->get("spriteSheetPath"), out.sprite_sheet_path);

        if (const JsonValue* padding = textbox->get("padding"); padding && padding->isObject()) {
            out.bottom_padding_px = intOr(padding->get("bottomPx"), out.bottom_padding_px);
            out.side_padding_px = intOr(padding->get("sidePx"), out.side_padding_px);
        }
        if (const JsonValue* source = textbox->get("sourceSheet"); source && source->isObject()) {
            out.source_cell_width_px = intOr(source->get("cellWidthPx"), out.source_cell_width_px);
            out.source_cell_height_px = intOr(source->get("cellHeightPx"), out.source_cell_height_px);
            out.sheet_columns = intOr(source->get("columns"), out.sheet_columns);
        }
        if (const JsonValue* stretch = textbox->get("horizontalStretch"); stretch && stretch->isObject()) {
            out.stretch_strip_width_px = intOr(stretch->get("stripWidthPx"), out.stretch_strip_width_px);
            out.stretch_strip_center_x_px = intOr(stretch->get("sourceCenterXPx"), out.stretch_strip_center_x_px);
        }
        if (const JsonValue* future = textbox->get("futureText"); future && future->isObject()) {
            out.future_text_font_path = stringOr(future->get("fontPath"), out.future_text_font_path);
            out.future_text_font_size_px = intOr(future->get("fontSizePx"), out.future_text_font_size_px);
        }
    } catch (const std::exception& ex) {
        std::cerr << "[Overworld3D][Textbox] Could not load " << path << ": " << ex.what() << '\n';
        return out;
    }

    out.valid_skin_count = std::clamp(out.valid_skin_count, 1, 13);
    out.bottom_padding_px = std::max(0, out.bottom_padding_px);
    out.side_padding_px = std::max(0, out.side_padding_px);
    out.source_cell_width_px = std::max(1, out.source_cell_width_px);
    out.source_cell_height_px = std::max(1, out.source_cell_height_px);
    out.sheet_columns = std::max(1, out.sheet_columns);
    out.stretch_strip_width_px = std::clamp(out.stretch_strip_width_px, 1, out.source_cell_width_px);
    out.stretch_strip_center_x_px = std::clamp(out.stretch_strip_center_x_px, 0, out.source_cell_width_px);
    out.future_text_font_size_px = std::max(1, out.future_text_font_size_px);
    return out;
}

bool overworldTextboxEnabled(const OverworldTextboxConfig& config) {
    const std::string mode = lower(config.visible_mode);
    return mode == "enabled" || mode == "visible" || mode == "on";
}

int clampedTextboxSkinIndex(const OverworldTextboxConfig& config) {
    return std::clamp(config.selected_skin_index, 0, std::max(0, config.valid_skin_count - 1));
}

} // namespace pr::gameplay::world3d::dialogue
