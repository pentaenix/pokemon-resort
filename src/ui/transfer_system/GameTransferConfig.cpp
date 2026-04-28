#include "ui/transfer_system/GameTransferConfig.hpp"

#include "core/Json.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_map>

namespace fs = std::filesystem;

namespace pr::transfer_system {

namespace {

using TokenMap = std::unordered_map<std::string, std::string>;

Color parseHexColorString(const std::string& value, const Color& fallback) {
    if (value.size() != 7 || value[0] != '#') {
        return fallback;
    }
    auto parse_component = [&](int index) -> int {
        const auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
            if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
            return -1;
        };
        const int hi = hex(value[static_cast<std::size_t>(index)]);
        const int lo = hex(value[static_cast<std::size_t>(index + 1)]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        return (hi << 4) | lo;
    };
    const int r = parse_component(1);
    const int g = parse_component(3);
    const int b = parse_component(5);
    if (r < 0 || g < 0 || b < 0) {
        return fallback;
    }
    return Color{r, g, b, 255};
}

std::string resolveColorToken(const std::string& raw, const TokenMap& tokens) {
    if (raw.size() >= 2 && raw[0] == '$') {
        const std::string key = raw.substr(1);
        const auto it = tokens.find(key);
        if (it != tokens.end()) {
            return it->second;
        }
    }
    return raw;
}

Color parseColorString(const std::string& raw, const Color& fallback, const TokenMap& tokens) {
    return parseHexColorString(resolveColorToken(raw, tokens), fallback);
}

TokenMap loadDesignTokens(const std::string& project_root) {
    TokenMap out;
    const fs::path path = fs::path(project_root) / "config" / "design.json";
    if (!fs::exists(path)) {
        return out;
    }
    JsonValue root = parseJsonFile(path.string());
    if (!root.isObject()) {
        return out;
    }
    const JsonValue* tokens = root.get("tokens");
    if (!tokens || !tokens->isObject()) {
        return out;
    }
    for (const auto& [key, value] : tokens->asObject()) {
        if (!value.isString()) {
            continue;
        }
        out.emplace(key, value.asString());
    }
    return out;
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

void applyInfoBannerLayoutField(
    GameTransferInfoBannerFieldStyle& field,
    const JsonValue& banner,
    const JsonValue& field_value) {
    const JsonValue* layout = objectChild(&banner, "layout");
    if (!layout) {
        return;
    }

    const std::string layout_context =
        stringFromObjectOrDefault(field_value, "layout", field.contexts.empty() ? std::string{"pokemon"} : field.contexts.front());
    const JsonValue* context_layout = objectChild(layout, layout_context);
    if (!context_layout) {
        return;
    }

    const std::string row_key = stringFromObjectOrDefault(field_value, "row", {});
    const std::string column_key = stringFromObjectOrDefault(field_value, "column", {});
    const JsonValue* row = objectChild(objectChild(context_layout, "rows"), row_key);
    const JsonValue* column = objectChild(objectChild(context_layout, "columns"), column_key);

    if (column) {
        field.x = intFromObjectOrDefault(*column, "x", field.x);
        const int slot = intFromObjectOrDefault(field_value, "slot", 0);
        const int step = intFromObjectOrDefault(*column, "step", 0);
        field.x += slot * step;
        field.x += intFromObjectOrDefault(*column, "x_adjust", 0);
    }
    if (row) {
        const char* y_key = field.kind == "icon" ? "icon_y" : "text_y";
        field.y = intFromObjectOrDefault(*row, y_key, intFromObjectOrDefault(*row, "y", field.y));

        if (field.kind == "icon") {
            const int icon_base_size = intFromObjectOrDefault(
                field_value,
                "icon_base_size",
                column
                    ? intFromObjectOrDefault(*column, "icon_size", intFromObjectOrDefault(*row, "icon_size", field.width > 0 ? field.width : 40))
                    : intFromObjectOrDefault(*row, "icon_size", field.width > 0 ? field.width : 40));
            const double section_scale = column ? doubleFromObjectOrDefault(*column, "scale", 1.0) : 1.0;
            const double field_scale = doubleFromObjectOrDefault(field_value, "scale", 1.0);
            const double width_scale = section_scale * doubleFromObjectOrDefault(field_value, "width_scale", field_scale);
            const double height_scale = section_scale * doubleFromObjectOrDefault(field_value, "height_scale", field_scale);
            const int width = std::max(1, static_cast<int>(std::lround(static_cast<double>(icon_base_size) * width_scale)));
            const int height = std::max(1, static_cast<int>(std::lround(static_cast<double>(icon_base_size) * height_scale)));
            field.width = intFromObjectOrDefault(field_value, "width", width);
            field.height = intFromObjectOrDefault(field_value, "height", height);
        }
    }

    field.x += intFromObjectOrDefault(field_value, "x_adjust", 0);
    field.y += intFromObjectOrDefault(field_value, "y_adjust", 0);
}

} // namespace

LoadedGameTransfer loadGameTransfer(const std::string& project_root) {
    LoadedGameTransfer out;
    const TokenMap design_tokens = loadDesignTokens(project_root);
    const fs::path path = fs::path(project_root) / "config" / "game_transfer.json";
    if (!fs::exists(path)) {
        return out;
    }

    JsonValue root = parseJsonFile(path.string());
    if (!root.isObject()) {
        return out;
    }

    out.resort_pc_box_count =
        std::clamp(intFromObjectOrDefault(root, "resort_pc_box_count", out.resort_pc_box_count), 1, 512);

    // Optional exit button chrome (left of tool belt).
    out.exit_button_enabled = boolFromObjectOrDefault(root, "exit_button_enabled", out.exit_button_enabled);
    if (const JsonValue* exit_btn = root.get("exit_button")) {
        if (exit_btn->isObject()) {
            const JsonValue& o = *exit_btn;
            out.exit_button_enabled = boolFromObjectOrDefault(o, "enabled", out.exit_button_enabled);
            out.exit_button_gap_pixels = std::max(0, intFromObjectOrDefault(o, "gap_pixels", out.exit_button_gap_pixels));
            out.exit_button_icon_scale = std::clamp(
                doubleFromObjectOrDefault(o, "icon_scale", out.exit_button_icon_scale),
                0.05,
                4.0);
            if (const JsonValue* c = o.get("icon_mod_color")) {
                if (c->isString()) {
                    out.exit_button_icon_mod_color =
                        parseColorString(c->asString(), out.exit_button_icon_mod_color, design_tokens);
                }
            }
        }
    }

    if (const JsonValue* fade = root.get("fade_in_seconds")) {
        if (fade->isNumber()) {
            out.fade_in_seconds = std::max(0.0, fade->asNumber());
        }
    }
    if (const JsonValue* fade = root.get("fade_out_seconds")) {
        if (fade->isNumber()) {
            out.fade_out_seconds = std::max(0.0, fade->asNumber());
        }
    }
    if (const JsonValue* fade = root.get("fade")) {
        if (fade->isObject()) {
            out.fade_in_seconds = std::max(0.0, doubleFromObjectOrDefault(*fade, "in_seconds", out.fade_in_seconds));
            out.fade_out_seconds = std::max(0.0, doubleFromObjectOrDefault(*fade, "out_seconds", out.fade_out_seconds));
        }
    }

    if (const JsonValue* background_animation = root.get("background_animation")) {
        if (background_animation->isObject()) {
            out.background_animation.enabled =
                boolFromObjectOrDefault(*background_animation, "enabled", out.background_animation.enabled);
            out.background_animation.scale = std::max(
                0.01,
                doubleFromObjectOrDefault(*background_animation, "scale", out.background_animation.scale));
            out.background_animation.speed_x =
                doubleFromObjectOrDefault(*background_animation, "speed_x", out.background_animation.speed_x);
            out.background_animation.speed_y =
                doubleFromObjectOrDefault(*background_animation, "speed_y", out.background_animation.speed_y);
        }
    }

    if (const JsonValue* bv = root.get("box_viewport")) {
        if (bv->isObject()) {
            const JsonValue& o = *bv;
            out.box_viewport.arrow_texture = stringFromObjectOrDefault(o, "arrow_texture", out.box_viewport.arrow_texture);
            if (const JsonValue* c = o.get("arrow_mod_color")) {
                if (c->isString()) {
                    out.box_viewport.arrow_mod_color =
                        parseColorString(c->asString(), out.box_viewport.arrow_mod_color, design_tokens);
                }
            }
            out.box_viewport.box_name_font_pt = intFromObjectOrDefault(o, "box_name_font_pt", out.box_viewport.box_name_font_pt);
            if (const JsonValue* c = o.get("box_name_color")) {
                if (c->isString()) {
                    out.box_viewport.box_name_color =
                        parseColorString(c->asString(), out.box_viewport.box_name_color, design_tokens);
                }
            }
            out.box_viewport.box_space_font_pt =
                intFromObjectOrDefault(o, "box_space_font_pt", out.box_viewport.box_space_font_pt);
            if (const JsonValue* c = o.get("box_space_color")) {
                if (c->isString()) {
                    out.box_viewport.box_space_color =
                        parseColorString(c->asString(), out.box_viewport.box_space_color, design_tokens);
                }
            }
            out.box_viewport.footer_scroll_arrow_offset_y =
                intFromObjectOrDefault(o, "footer_scroll_arrow_offset_y", out.box_viewport.footer_scroll_arrow_offset_y);
            out.box_viewport.content_slide_smoothing =
                doubleFromObjectOrDefault(o, "content_slide_smoothing", out.box_viewport.content_slide_smoothing);
            out.box_viewport.sprite_scale = doubleFromObjectOrDefault(o, "sprite_scale", out.box_viewport.sprite_scale);
            out.box_viewport.sprite_offset_y = intFromObjectOrDefault(o, "sprite_offset_y", out.box_viewport.sprite_offset_y);

            if (const JsonValue* c = o.get("viewport_background_color")) {
                if (c->isString()) {
                    out.box_viewport.viewport_background_color =
                        parseColorString(c->asString(), out.box_viewport.viewport_background_color, design_tokens);
                }
            }
            if (const JsonValue* c = o.get("viewport_border_color")) {
                if (c->isString()) {
                    out.box_viewport.viewport_border_color =
                        parseColorString(c->asString(), out.box_viewport.viewport_border_color, design_tokens);
                }
            }
            out.box_viewport.viewport_border_thickness = std::max(
                0,
                intFromObjectOrDefault(o, "viewport_border_thickness", out.box_viewport.viewport_border_thickness));
            if (const JsonValue* c = o.get("name_plate_background_color")) {
                if (c->isString()) {
                    out.box_viewport.name_plate_background_color =
                        parseColorString(c->asString(), out.box_viewport.name_plate_background_color, design_tokens);
                }
            }
            if (const JsonValue* c = o.get("slot_background_color")) {
                if (c->isString()) {
                    out.box_viewport.slot_background_color =
                        parseColorString(c->asString(), out.box_viewport.slot_background_color, design_tokens);
                }
            }
            if (const JsonValue* c = o.get("footer_button_fill_color")) {
                if (c->isString()) {
                    out.box_viewport.footer_button_fill_color =
                        parseColorString(c->asString(), out.box_viewport.footer_button_fill_color, design_tokens);
                }
            }
            if (const JsonValue* c = o.get("footer_button_underline_color")) {
                if (c->isString()) {
                    out.box_viewport.footer_button_underline_color =
                        parseColorString(c->asString(), out.box_viewport.footer_button_underline_color, design_tokens);
                }
            }
            if (const JsonValue* c = o.get("footer_button_active_fill_color")) {
                if (c->isString()) {
                    out.box_viewport.footer_button_active_fill_color =
                        parseColorString(c->asString(), out.box_viewport.footer_button_active_fill_color, design_tokens);
                }
            }
            if (const JsonValue* c = o.get("footer_button_active_underline_color")) {
                if (c->isString()) {
                    out.box_viewport.footer_button_active_underline_color =
                        parseColorString(c->asString(), out.box_viewport.footer_button_active_underline_color, design_tokens);
                }
            }

            if (const JsonValue* item_tool = o.get("item_tool")) {
                if (item_tool->isObject()) {
                    out.box_viewport.item_tool_item_size =
                        intFromObjectOrDefault(*item_tool, "item_size", out.box_viewport.item_tool_item_size);
                    out.box_viewport.item_tool_grow_smoothing =
                        doubleFromObjectOrDefault(*item_tool, "grow_smoothing", out.box_viewport.item_tool_grow_smoothing);
                    if (const JsonValue* sprite_mod = item_tool->get("sprite_mod_color")) {
                        if (sprite_mod->isString()) {
                            out.box_viewport.item_tool_sprite_mod_color =
                                parseColorString(
                                    sprite_mod->asString(),
                                    out.box_viewport.item_tool_sprite_mod_color,
                                    design_tokens);
                        }
                    }
                    out.box_viewport.item_tool_sprite_mod_color.a =
                        std::clamp(
                            intFromObjectOrDefault(
                                *item_tool,
                                "sprite_mod_alpha",
                                out.box_viewport.item_tool_sprite_mod_color.a),
                            0,
                            255);
                }
            }

            if (const JsonValue* bs = o.get("box_space_sprites")) {
                if (bs->isObject()) {
                    out.box_viewport.box_space_sprite_scale =
                        doubleFromObjectOrDefault(*bs, "sprite_scale", out.box_viewport.box_space_sprite_scale);
                    out.box_viewport.box_space_sprite_offset_x =
                        intFromObjectOrDefault(*bs, "sprite_offset_x", out.box_viewport.box_space_sprite_offset_x);
                    out.box_viewport.box_space_sprite_offset_y =
                        intFromObjectOrDefault(*bs, "sprite_offset_y", out.box_viewport.box_space_sprite_offset_y);
                }
            }
        }
    }

    if (const JsonValue* mp = root.get("mini_preview")) {
        if (mp->isObject()) {
            const JsonValue& o = *mp;
            out.mini_preview.enabled = boolFromObjectOrDefault(o, "enabled", out.mini_preview.enabled);
            out.mini_preview.width = intFromObjectOrDefault(o, "width", out.mini_preview.width);
            out.mini_preview.height = intFromObjectOrDefault(o, "height", out.mini_preview.height);
            out.mini_preview.corner_radius = intFromObjectOrDefault(o, "corner_radius", out.mini_preview.corner_radius);
            out.mini_preview.border_thickness =
                intFromObjectOrDefault(o, "border_thickness", out.mini_preview.border_thickness);
            out.mini_preview.offset_y = intFromObjectOrDefault(o, "offset_y", out.mini_preview.offset_y);
            out.mini_preview.edge_pad = intFromObjectOrDefault(o, "edge_pad", out.mini_preview.edge_pad);
            out.mini_preview.enter_smoothing =
                doubleFromObjectOrDefault(o, "enter_smoothing", out.mini_preview.enter_smoothing);
            out.mini_preview.sprite_scale =
                doubleFromObjectOrDefault(o, "sprite_scale", out.mini_preview.sprite_scale);
        }
    }

    if (const JsonValue* bsp = root.get("box_space_long_press")) {
        if (bsp->isObject()) {
            const JsonValue& o = *bsp;
            out.box_space_long_press.box_swap_hold_seconds = std::max(
                0.0,
                doubleFromObjectOrDefault(o, "box_swap_hold_seconds", out.box_space_long_press.box_swap_hold_seconds));
            out.box_space_long_press.quick_drop_hold_seconds = std::max(
                0.0,
                doubleFromObjectOrDefault(o, "quick_drop_hold_seconds", out.box_space_long_press.quick_drop_hold_seconds));
            out.box_space_long_press.long_press_feedback_seconds =
                std::max(0.0,
                         doubleFromObjectOrDefault(
                             o,
                             "long_press_feedback_seconds",
                             out.box_space_long_press.long_press_feedback_seconds));
        }
    }

    if (const JsonValue* action_menu = root.get("pokemon_action_menu")) {
        if (action_menu->isObject()) {
            const JsonValue& o = *action_menu;
            out.pokemon_action_menu.width = intFromObjectOrDefault(o, "width", out.pokemon_action_menu.width);
            out.pokemon_action_menu.row_height =
                intFromObjectOrDefault(o, "row_height", out.pokemon_action_menu.row_height);
            out.pokemon_action_menu.padding_y =
                intFromObjectOrDefault(o, "padding_y", out.pokemon_action_menu.padding_y);
            out.pokemon_action_menu.gap_from_slot =
                intFromObjectOrDefault(o, "gap_from_slot", out.pokemon_action_menu.gap_from_slot);
            out.pokemon_action_menu.corner_radius =
                intFromObjectOrDefault(o, "corner_radius", out.pokemon_action_menu.corner_radius);
            out.pokemon_action_menu.border_thickness =
                intFromObjectOrDefault(o, "border_thickness", out.pokemon_action_menu.border_thickness);
            out.pokemon_action_menu.font_pt = intFromObjectOrDefault(o, "font_pt", out.pokemon_action_menu.font_pt);
            out.pokemon_action_menu.grow_smoothing =
                doubleFromObjectOrDefault(o, "grow_smoothing", out.pokemon_action_menu.grow_smoothing);
            if (const JsonValue* c = o.get("background_color")) {
                if (c->isString()) {
                    const int alpha = out.pokemon_action_menu.background_color.a;
                    out.pokemon_action_menu.background_color =
                        parseColorString(c->asString(), out.pokemon_action_menu.background_color, design_tokens);
                    out.pokemon_action_menu.background_color.a = alpha;
                }
            }
            out.pokemon_action_menu.background_color.a =
                std::clamp(intFromObjectOrDefault(o, "background_alpha", out.pokemon_action_menu.background_color.a), 0, 255);
            if (const JsonValue* c = o.get("border_color")) {
                if (c->isString()) {
                    out.pokemon_action_menu.border_color =
                        parseColorString(c->asString(), out.pokemon_action_menu.border_color, design_tokens);
                }
            }
            if (const JsonValue* c = o.get("selected_row_color")) {
                if (c->isString()) {
                    const int alpha = out.pokemon_action_menu.selected_row_color.a;
                    out.pokemon_action_menu.selected_row_color =
                        parseColorString(c->asString(), out.pokemon_action_menu.selected_row_color, design_tokens);
                    out.pokemon_action_menu.selected_row_color.a = alpha;
                }
            }
            out.pokemon_action_menu.selected_row_color.a =
                std::clamp(
                    intFromObjectOrDefault(
                        o,
                        "selected_row_alpha",
                        out.pokemon_action_menu.selected_row_color.a),
                    0,
                    255);
            if (const JsonValue* c = o.get("text_color")) {
                if (c->isString()) {
                    out.pokemon_action_menu.text_color =
                        parseColorString(c->asString(), out.pokemon_action_menu.text_color, design_tokens);
                }
            }
            out.pokemon_action_menu.dim_background_sprites =
                boolFromObjectOrDefault(o, "dim_background_sprites", out.pokemon_action_menu.dim_background_sprites);
            if (const JsonValue* c = o.get("dim_sprite_mod_color")) {
                if (c->isString()) {
                    const int alpha = out.pokemon_action_menu.dim_sprite_mod_color.a;
                    out.pokemon_action_menu.dim_sprite_mod_color =
                        parseColorString(c->asString(), out.pokemon_action_menu.dim_sprite_mod_color, design_tokens);
                    out.pokemon_action_menu.dim_sprite_mod_color.a = alpha;
                }
            }
            out.pokemon_action_menu.dim_sprite_mod_color.a =
                std::clamp(
                    intFromObjectOrDefault(
                        o,
                        "dim_sprite_mod_alpha",
                        out.pokemon_action_menu.dim_sprite_mod_color.a),
                    0,
                    255);
            out.pokemon_action_menu.modal_move_swaps_into_hand =
                boolFromObjectOrDefault(o, "modal_move_swaps_into_hand", out.pokemon_action_menu.modal_move_swaps_into_hand);
            out.pokemon_action_menu.swap_tool_swaps_into_hand =
                boolFromObjectOrDefault(o, "swap_tool_swaps_into_hand", out.pokemon_action_menu.swap_tool_swaps_into_hand);
            out.pokemon_action_menu.held_sprite_shadow_enabled =
                boolFromObjectOrDefault(o, "held_sprite_shadow_enabled", out.pokemon_action_menu.held_sprite_shadow_enabled);
            if (const JsonValue* c = o.get("held_sprite_shadow_color")) {
                if (c->isString()) {
                    const int alpha = out.pokemon_action_menu.held_sprite_shadow_color.a;
                    out.pokemon_action_menu.held_sprite_shadow_color =
                        parseColorString(c->asString(), out.pokemon_action_menu.held_sprite_shadow_color, design_tokens);
                    out.pokemon_action_menu.held_sprite_shadow_color.a = alpha;
                }
            }
            out.pokemon_action_menu.held_sprite_shadow_color.a =
                std::clamp(
                    intFromObjectOrDefault(
                        o,
                        "held_sprite_shadow_alpha",
                        out.pokemon_action_menu.held_sprite_shadow_color.a),
                    0,
                    255);
            out.pokemon_action_menu.held_sprite_shadow_offset_y =
                intFromObjectOrDefault(o, "held_sprite_shadow_offset_y", out.pokemon_action_menu.held_sprite_shadow_offset_y);
            out.pokemon_action_menu.held_sprite_shadow_width =
                intFromObjectOrDefault(o, "held_sprite_shadow_width", out.pokemon_action_menu.held_sprite_shadow_width);
            out.pokemon_action_menu.held_sprite_shadow_height =
                intFromObjectOrDefault(o, "held_sprite_shadow_height", out.pokemon_action_menu.held_sprite_shadow_height);
            out.pokemon_action_menu.held_sprite_scale_multiplier = std::clamp(
                doubleFromObjectOrDefault(
                    o,
                    "held_sprite_scale_multiplier",
                    out.pokemon_action_menu.held_sprite_scale_multiplier),
                0.01,
                32.0);
        }
    }

    if (const JsonValue* banner = root.get("info_banner")) {
        if (banner->isObject()) {
            const JsonValue& o = *banner;
            out.info_banner.enabled = boolFromObjectOrDefault(o, "enabled", out.info_banner.enabled);
            out.info_banner.separator_height =
                intFromObjectOrDefault(o, "separator_height", out.info_banner.separator_height);
            out.info_banner.info_height =
                intFromObjectOrDefault(o, "info_height", out.info_banner.info_height);
            out.info_banner.icon_directory =
                stringFromObjectOrDefault(o, "icon_directory", out.info_banner.icon_directory);
            out.info_banner.game_icon_directory =
                stringFromObjectOrDefault(o, "game_icon_directory", out.info_banner.game_icon_directory);
            out.info_banner.unknown_icon =
                stringFromObjectOrDefault(o, "unknown_icon", out.info_banner.unknown_icon);
            out.info_banner.tool_multiple_title =
                stringFromObjectOrDefault(o, "tool_multiple_title", out.info_banner.tool_multiple_title);
            out.info_banner.tool_multiple_body =
                stringFromObjectOrDefault(o, "tool_multiple_body", out.info_banner.tool_multiple_body);
            out.info_banner.tool_basic_title =
                stringFromObjectOrDefault(o, "tool_basic_title", out.info_banner.tool_basic_title);
            out.info_banner.tool_basic_body =
                stringFromObjectOrDefault(o, "tool_basic_body", out.info_banner.tool_basic_body);
            out.info_banner.tool_swap_title =
                stringFromObjectOrDefault(o, "tool_swap_title", out.info_banner.tool_swap_title);
            out.info_banner.tool_swap_body =
                stringFromObjectOrDefault(o, "tool_swap_body", out.info_banner.tool_swap_body);
            out.info_banner.tool_items_title =
                stringFromObjectOrDefault(o, "tool_items_title", out.info_banner.tool_items_title);
            out.info_banner.tool_items_body =
                stringFromObjectOrDefault(o, "tool_items_body", out.info_banner.tool_items_body);
            out.info_banner.pill_pokemon_title =
                stringFromObjectOrDefault(o, "pill_pokemon_title", out.info_banner.pill_pokemon_title);
            out.info_banner.pill_pokemon_body =
                stringFromObjectOrDefault(o, "pill_pokemon_body", out.info_banner.pill_pokemon_body);
            out.info_banner.pill_items_title =
                stringFromObjectOrDefault(o, "pill_items_title", out.info_banner.pill_items_title);
            out.info_banner.pill_items_body =
                stringFromObjectOrDefault(o, "pill_items_body", out.info_banner.pill_items_body);
            out.info_banner.box_space_title =
                stringFromObjectOrDefault(o, "box_space_title", out.info_banner.box_space_title);
            out.info_banner.box_space_body =
                stringFromObjectOrDefault(o, "box_space_body", out.info_banner.box_space_body);
            out.info_banner.exit_tooltip_title =
                stringFromObjectOrDefault(o, "exit_tooltip_title", out.info_banner.exit_tooltip_title);
            out.info_banner.exit_tooltip_body =
                stringFromObjectOrDefault(o, "exit_tooltip_body", out.info_banner.exit_tooltip_body);
            if (const JsonValue* c = o.get("separator_color")) {
                if (c->isString()) {
                    out.info_banner.separator_color =
                        parseColorString(c->asString(), out.info_banner.separator_color, design_tokens);
                }
            }
            if (const JsonValue* c = o.get("info_background_color")) {
                if (c->isString()) {
                    out.info_banner.info_background_color =
                        parseColorString(c->asString(), out.info_banner.info_background_color, design_tokens);
                }
            }
            if (const JsonValue* text_defaults = objectChild(&o, "text_defaults")) {
                out.info_banner.text_font_pt =
                    intFromObjectOrDefault(*text_defaults, "font_pt", out.info_banner.text_font_pt);
                out.info_banner.label_font_pt =
                    intFromObjectOrDefault(*text_defaults, "label_font_pt", out.info_banner.label_font_pt);
                if (const JsonValue* c = text_defaults->get("color")) {
                    if (c->isString()) {
                        out.info_banner.text_color =
                            parseColorString(c->asString(), out.info_banner.text_color, design_tokens);
                    }
                }
                if (const JsonValue* c = text_defaults->get("label_color")) {
                    if (c->isString()) {
                        out.info_banner.label_color =
                            parseColorString(c->asString(), out.info_banner.label_color, design_tokens);
                    }
                }
            }
            if (const JsonValue* gender_symbol = objectChild(&o, "gender_symbol")) {
                out.info_banner.gender_symbol_font_pt =
                    intFromObjectOrDefault(*gender_symbol, "font_pt", out.info_banner.gender_symbol_font_pt);
                out.info_banner.gender_symbol_x_adjust =
                    intFromObjectOrDefault(*gender_symbol, "x_adjust", out.info_banner.gender_symbol_x_adjust);
                out.info_banner.gender_symbol_y_adjust =
                    intFromObjectOrDefault(*gender_symbol, "y_adjust", out.info_banner.gender_symbol_y_adjust);
                if (const JsonValue* c = gender_symbol->get("male_color")) {
                    if (c->isString()) {
                        out.info_banner.gender_symbol_male_color =
                            parseHexColorString(c->asString(), out.info_banner.gender_symbol_male_color);
                    }
                }
                if (const JsonValue* c = gender_symbol->get("female_color")) {
                    if (c->isString()) {
                        out.info_banner.gender_symbol_female_color =
                            parseHexColorString(c->asString(), out.info_banner.gender_symbol_female_color);
                    }
                }
            }
            out.info_banner.fields.clear();
            if (const JsonValue* fields = o.get("fields"); fields && fields->isArray()) {
                for (const JsonValue& field_value : fields->asArray()) {
                    if (!field_value.isObject()) {
                        continue;
                    }
                    GameTransferInfoBannerFieldStyle field;
                    field.font_pt = out.info_banner.text_font_pt;
                    field.color = out.info_banner.text_color;
                    field.label_font_pt = out.info_banner.label_font_pt;
                    field.label_color = out.info_banner.label_color;
                    field.field = stringFromObjectOrDefault(field_value, "field", field.field);
                    field.kind = stringFromObjectOrDefault(field_value, "kind", field.kind);
                    field.label = stringFromObjectOrDefault(field_value, "label", field.label);
                    field.empty_text = stringFromObjectOrDefault(field_value, "empty_text", field.empty_text);
                    field.x = intFromObjectOrDefault(field_value, "x", field.x);
                    field.y = intFromObjectOrDefault(field_value, "y", field.y);
                    field.width = intFromObjectOrDefault(field_value, "width", field.width);
                    field.height = intFromObjectOrDefault(field_value, "height", field.height);
                    field.flow_group = stringFromObjectOrDefault(field_value, "flow_group", field.flow_group);
                    field.flow_gap = intFromObjectOrDefault(field_value, "flow_gap", field.flow_gap);
                    field.font_pt = intFromObjectOrDefault(field_value, "font_pt", field.font_pt);
                    field.label_font_pt = intFromObjectOrDefault(field_value, "label_font_pt", field.label_font_pt);
                    if (const JsonValue* c = field_value.get("color")) {
                        if (c->isString()) {
                            field.color = parseColorString(c->asString(), field.color, design_tokens);
                        }
                    }
                    if (const JsonValue* c = field_value.get("label_color")) {
                        if (c->isString()) {
                            field.label_color = parseColorString(c->asString(), field.label_color, design_tokens);
                        }
                    }
                    field.contexts.clear();
                    if (const JsonValue* contexts = field_value.get("contexts"); contexts && contexts->isArray()) {
                        for (const JsonValue& context_value : contexts->asArray()) {
                            if (context_value.isString()) {
                                field.contexts.push_back(context_value.asString());
                            }
                        }
                    }
                    if (field.contexts.empty()) {
                        field.contexts = {"pokemon", "empty"};
                    }
                    applyInfoBannerLayoutField(field, o, field_value);
                    if (!field.field.empty()) {
                        out.info_banner.fields.push_back(std::move(field));
                    }
                }
            }
        }
    }

    if (const JsonValue* pt = root.get("pill_toggle")) {
        if (pt->isObject()) {
            const JsonValue& o = *pt;
            out.pill_toggle.track_width = intFromObjectOrDefault(o, "track_width", out.pill_toggle.track_width);
            out.pill_toggle.track_height = intFromObjectOrDefault(o, "track_height", out.pill_toggle.track_height);
            out.pill_toggle.pill_width = intFromObjectOrDefault(o, "pill_width", out.pill_toggle.pill_width);
            out.pill_toggle.pill_height = intFromObjectOrDefault(o, "pill_height", out.pill_toggle.pill_height);
            out.pill_toggle.pill_inset = intFromObjectOrDefault(o, "pill_inset", out.pill_toggle.pill_inset);
            if (const JsonValue* gab = o.get("gap_above_boxes")) {
                if (gab->isNumber()) {
                    out.pill_toggle.gap_above_boxes = static_cast<int>(gab->asNumber());
                }
            } else {
                out.pill_toggle.gap_above_boxes =
                    intFromObjectOrDefault(o, "gap_above_left_box", out.pill_toggle.gap_above_boxes);
            }
            out.pill_toggle.font_pt = intFromObjectOrDefault(o, "font_pt", out.pill_toggle.font_pt);
            if (const JsonValue* c = o.get("track_color")) {
                if (c->isString()) {
                    out.pill_toggle.track_color =
                        parseColorString(c->asString(), out.pill_toggle.track_color, design_tokens);
                }
            }
            if (const JsonValue* c = o.get("pill_color")) {
                if (c->isString()) {
                    out.pill_toggle.pill_color =
                        parseColorString(c->asString(), out.pill_toggle.pill_color, design_tokens);
                }
            }
            if (const JsonValue* c = o.get("label_unselected_color")) {
                if (c->isString()) {
                    out.pill_toggle.label_unselected_color =
                        parseColorString(c->asString(), out.pill_toggle.label_unselected_color, design_tokens);
                }
            }
            if (const JsonValue* c = o.get("label_selected_color")) {
                if (c->isString()) {
                    out.pill_toggle.label_selected_color =
                        parseColorString(c->asString(), out.pill_toggle.label_selected_color, design_tokens);
                }
            }
            out.pill_toggle.toggle_smoothing =
                doubleFromObjectOrDefault(o, "toggle_smoothing", out.pill_toggle.toggle_smoothing);
            out.pill_toggle.box_smoothing =
                doubleFromObjectOrDefault(o, "box_smoothing", out.pill_toggle.box_smoothing);
            if (!o.get("toggle_smoothing")) {
                const double legacy_dur = doubleFromObjectOrDefault(o, "toggle_slide_duration_seconds", 0.18);
                if (legacy_dur > 1e-9) {
                    out.pill_toggle.toggle_smoothing = 5.0 / legacy_dur;
                }
            }
            if (!o.get("box_smoothing")) {
                const double legacy_dur = doubleFromObjectOrDefault(o, "box_panel_slide_duration_seconds", 0.42);
                if (legacy_dur > 1e-9) {
                    out.pill_toggle.box_smoothing = 4.0 / legacy_dur;
                }
            }
        }
    }

    if (const JsonValue* tc = root.get("tool_carousel")) {
        if (tc->isObject()) {
            const JsonValue& o = *tc;
            out.tool_carousel.viewport_width =
                intFromObjectOrDefault(o, "viewport_width", out.tool_carousel.viewport_width);
            out.tool_carousel.viewport_height =
                intFromObjectOrDefault(o, "viewport_height", out.tool_carousel.viewport_height);
            out.tool_carousel.offset_from_left_wall =
                intFromObjectOrDefault(o, "offset_from_left_wall", out.tool_carousel.offset_from_left_wall);
            out.tool_carousel.rest_y = intFromObjectOrDefault(o, "rest_y", out.tool_carousel.rest_y);
            out.tool_carousel.hidden_y = intFromObjectOrDefault(o, "hidden_y", out.tool_carousel.hidden_y);
            out.tool_carousel.viewport_corner_radius =
                intFromObjectOrDefault(o, "viewport_corner_radius", out.tool_carousel.viewport_corner_radius);
            out.tool_carousel.viewport_clip_inset =
                intFromObjectOrDefault(o, "viewport_clip_inset", out.tool_carousel.viewport_clip_inset);
            if (const JsonValue* c = o.get("viewport_color")) {
                if (c->isString()) {
                    out.tool_carousel.viewport_color =
                        parseColorString(c->asString(), out.tool_carousel.viewport_color, design_tokens);
                }
            }
            out.tool_carousel.icon_size = intFromObjectOrDefault(o, "icon_size", out.tool_carousel.icon_size);
            out.tool_carousel.selection_frame_size =
                intFromObjectOrDefault(o, "selection_frame_size", out.tool_carousel.selection_frame_size);
            out.tool_carousel.selection_stroke =
                intFromObjectOrDefault(o, "selection_stroke", out.tool_carousel.selection_stroke);
            if (const JsonValue* s = o.get("selector_size")) {
                if (s->isNumber()) {
                    out.tool_carousel.selection_frame_size = static_cast<int>(s->asNumber());
                }
            }
            if (const JsonValue* s = o.get("selector_thickness")) {
                if (s->isNumber()) {
                    out.tool_carousel.selection_stroke = static_cast<int>(s->asNumber());
                }
            }
            out.tool_carousel.selector_corner_radius =
                intFromObjectOrDefault(o, "selector_corner_radius", out.tool_carousel.selector_corner_radius);
            out.tool_carousel.slide_span_pixels =
                intFromObjectOrDefault(o, "slide_span_pixels", out.tool_carousel.slide_span_pixels);
            out.tool_carousel.belt_spacing_pixels =
                intFromObjectOrDefault(o, "belt_spacing_pixels", out.tool_carousel.belt_spacing_pixels);
            out.tool_carousel.slide_smoothing =
                doubleFromObjectOrDefault(o, "slide_smoothing", out.tool_carousel.slide_smoothing);
            out.tool_carousel.slot_center_left =
                intFromObjectOrDefault(o, "slot_center_left", out.tool_carousel.slot_center_left);
            out.tool_carousel.slot_center_middle =
                intFromObjectOrDefault(o, "slot_center_middle", out.tool_carousel.slot_center_middle);
            out.tool_carousel.slot_center_right =
                intFromObjectOrDefault(o, "slot_center_right", out.tool_carousel.slot_center_right);
            out.tool_carousel.texture_multiple =
                stringFromObjectOrDefault(o, "texture_multiple", out.tool_carousel.texture_multiple);
            out.tool_carousel.texture_basic =
                stringFromObjectOrDefault(o, "texture_basic", out.tool_carousel.texture_basic);
            out.tool_carousel.texture_swap =
                stringFromObjectOrDefault(o, "texture_swap", out.tool_carousel.texture_swap);
            out.tool_carousel.texture_items =
                stringFromObjectOrDefault(o, "texture_items", out.tool_carousel.texture_items);
            if (const JsonValue* c = o.get("icon_mod_color")) {
                if (c->isString()) {
                    out.tool_carousel.icon_mod_color =
                        parseColorString(c->asString(), out.tool_carousel.icon_mod_color, design_tokens);
                }
            }
            if (const JsonValue* c = o.get("frame_multiple")) {
                if (c->isString()) {
                    out.tool_carousel.frame_multiple =
                        parseColorString(c->asString(), out.tool_carousel.frame_multiple, design_tokens);
                }
            }
            if (const JsonValue* c = o.get("frame_basic")) {
                if (c->isString()) {
                    out.tool_carousel.frame_basic =
                        parseColorString(c->asString(), out.tool_carousel.frame_basic, design_tokens);
                }
            }
            if (const JsonValue* c = o.get("frame_swap")) {
                if (c->isString()) {
                    out.tool_carousel.frame_swap =
                        parseColorString(c->asString(), out.tool_carousel.frame_swap, design_tokens);
                }
            }
            if (const JsonValue* c = o.get("frame_items")) {
                if (c->isString()) {
                    out.tool_carousel.frame_items =
                        parseColorString(c->asString(), out.tool_carousel.frame_items, design_tokens);
                }
            }
        }
    }

    if (const JsonValue* dd = root.get("box_name_dropdown")) {
        if (dd->isObject()) {
            const JsonValue& o = *dd;
            out.box_name_dropdown.enabled = boolFromObjectOrDefault(o, "enabled", out.box_name_dropdown.enabled);
            out.box_name_dropdown.panel_width_pixels =
                intFromObjectOrDefault(o, "panel_width_pixels", out.box_name_dropdown.panel_width_pixels);
            out.box_name_dropdown.max_height_multiplier = static_cast<float>(doubleFromObjectOrDefault(
                o,
                "max_height_multiplier",
                static_cast<double>(out.box_name_dropdown.max_height_multiplier)));
            out.box_name_dropdown.reference_name_plate_height_pixels = intFromObjectOrDefault(
                o,
                "reference_name_plate_height_pixels",
                out.box_name_dropdown.reference_name_plate_height_pixels);
            out.box_name_dropdown.item_font_pt =
                intFromObjectOrDefault(o, "item_font_pt", out.box_name_dropdown.item_font_pt);
            out.box_name_dropdown.row_padding_y =
                intFromObjectOrDefault(o, "row_padding_y", out.box_name_dropdown.row_padding_y);
            out.box_name_dropdown.panel_corner_radius =
                intFromObjectOrDefault(o, "panel_corner_radius", out.box_name_dropdown.panel_corner_radius);
            out.box_name_dropdown.panel_border_thickness =
                intFromObjectOrDefault(o, "panel_border_thickness", out.box_name_dropdown.panel_border_thickness);
            if (const JsonValue* c = o.get("panel_color")) {
                if (c->isString()) {
                    out.box_name_dropdown.panel_color =
                        parseColorString(c->asString(), out.box_name_dropdown.panel_color, design_tokens);
                }
            }
            if (const JsonValue* c = o.get("panel_border_color")) {
                if (c->isString()) {
                    out.box_name_dropdown.panel_border_color =
                        parseColorString(c->asString(), out.box_name_dropdown.panel_border_color, design_tokens);
                }
            }
            if (const JsonValue* c = o.get("item_text_color")) {
                if (c->isString()) {
                    out.box_name_dropdown.item_text_color =
                        parseColorString(c->asString(), out.box_name_dropdown.item_text_color, design_tokens);
                }
            }
            if (const JsonValue* c = o.get("selected_row_tint")) {
                if (c->isString()) {
                    out.box_name_dropdown.selected_row_tint =
                        parseColorString(c->asString(), out.box_name_dropdown.selected_row_tint, design_tokens);
                }
            }
            out.box_name_dropdown.selected_row_tint.a = std::clamp(
                intFromObjectOrDefault(o, "selected_row_tint_alpha", out.box_name_dropdown.selected_row_tint.a),
                0,
                255);
            out.box_name_dropdown.open_smoothing =
                doubleFromObjectOrDefault(o, "open_smoothing", out.box_name_dropdown.open_smoothing);
            out.box_name_dropdown.close_smoothing =
                doubleFromObjectOrDefault(o, "close_smoothing", out.box_name_dropdown.close_smoothing);
            out.box_name_dropdown.bottom_margin_pixels =
                intFromObjectOrDefault(o, "bottom_margin_pixels", out.box_name_dropdown.bottom_margin_pixels);
            out.box_name_dropdown.scroll_drag_multiplier =
                doubleFromObjectOrDefault(o, "scroll_drag_multiplier", out.box_name_dropdown.scroll_drag_multiplier);
        }
    }

    if (const JsonValue* sc = root.get("selection_cursor")) {
        if (sc->isObject()) {
            const JsonValue& o = *sc;
            out.selection_cursor.enabled = boolFromObjectOrDefault(o, "enabled", out.selection_cursor.enabled);
            if (const JsonValue* c = o.get("color")) {
                if (c->isString()) {
                    out.selection_cursor.color = parseColorString(c->asString(), out.selection_cursor.color, design_tokens);
                }
            }
            out.selection_cursor.alpha = intFromObjectOrDefault(o, "alpha", out.selection_cursor.alpha);
            out.selection_cursor.thickness = intFromObjectOrDefault(o, "thickness", out.selection_cursor.thickness);
            out.selection_cursor.padding = intFromObjectOrDefault(o, "padding", out.selection_cursor.padding);
            out.selection_cursor.min_width = intFromObjectOrDefault(o, "min_width", out.selection_cursor.min_width);
            out.selection_cursor.min_height = intFromObjectOrDefault(o, "min_height", out.selection_cursor.min_height);
            out.selection_cursor.corner_radius =
                intFromObjectOrDefault(o, "corner_radius", out.selection_cursor.corner_radius);
            out.selection_cursor.beat_speed =
                doubleFromObjectOrDefault(o, "beat_speed", out.selection_cursor.beat_speed);
            out.selection_cursor.beat_magnitude =
                doubleFromObjectOrDefault(o, "beat_magnitude", out.selection_cursor.beat_magnitude);

            if (const JsonValue* sb = o.get("speech_bubble")) {
                if (sb->isObject()) {
                    const JsonValue& b = *sb;
                    out.selection_cursor.speech_bubble.enabled =
                        boolFromObjectOrDefault(b, "enabled", out.selection_cursor.speech_bubble.enabled);
                    out.selection_cursor.speech_bubble.font_pt =
                        intFromObjectOrDefault(b, "font_pt", out.selection_cursor.speech_bubble.font_pt);
                    if (const JsonValue* c = b.get("text_color")) {
                        if (c->isString()) {
                            out.selection_cursor.speech_bubble.text_color =
                                parseColorString(
                                    c->asString(),
                                    out.selection_cursor.speech_bubble.text_color,
                                    design_tokens);
                        }
                    }
                    if (const JsonValue* c = b.get("fill_color")) {
                        if (c->isString()) {
                            out.selection_cursor.speech_bubble.fill_color =
                                parseColorString(
                                    c->asString(),
                                    out.selection_cursor.speech_bubble.fill_color,
                                    design_tokens);
                        }
                    }
                    out.selection_cursor.speech_bubble.border_thickness = intFromObjectOrDefault(
                        b, "border_thickness", out.selection_cursor.speech_bubble.border_thickness);
                    out.selection_cursor.speech_bubble.corner_radius =
                        intFromObjectOrDefault(b, "corner_radius", out.selection_cursor.speech_bubble.corner_radius);
                    out.selection_cursor.speech_bubble.padding_x =
                        intFromObjectOrDefault(b, "padding_x", out.selection_cursor.speech_bubble.padding_x);
                    out.selection_cursor.speech_bubble.padding_y =
                        intFromObjectOrDefault(b, "padding_y", out.selection_cursor.speech_bubble.padding_y);
                    out.selection_cursor.speech_bubble.min_width =
                        intFromObjectOrDefault(b, "min_width", out.selection_cursor.speech_bubble.min_width);
                    out.selection_cursor.speech_bubble.max_width =
                        intFromObjectOrDefault(b, "max_width", out.selection_cursor.speech_bubble.max_width);
                    out.selection_cursor.speech_bubble.min_height =
                        intFromObjectOrDefault(b, "min_height", out.selection_cursor.speech_bubble.min_height);
                    out.selection_cursor.speech_bubble.empty_min_width =
                        intFromObjectOrDefault(b, "empty_min_width", out.selection_cursor.speech_bubble.empty_min_width);
                    out.selection_cursor.speech_bubble.empty_min_height =
                        intFromObjectOrDefault(b, "empty_min_height", out.selection_cursor.speech_bubble.empty_min_height);
                    out.selection_cursor.speech_bubble.triangle_base_width = intFromObjectOrDefault(
                        b, "triangle_base_width", out.selection_cursor.speech_bubble.triangle_base_width);
                    out.selection_cursor.speech_bubble.triangle_height =
                        intFromObjectOrDefault(b, "triangle_height", out.selection_cursor.speech_bubble.triangle_height);
                    out.selection_cursor.speech_bubble.gap_above_target =
                        intFromObjectOrDefault(b, "gap_above_target", out.selection_cursor.speech_bubble.gap_above_target);
                    out.selection_cursor.speech_bubble.screen_margin =
                        intFromObjectOrDefault(b, "screen_margin", out.selection_cursor.speech_bubble.screen_margin);
                    out.selection_cursor.speech_bubble.resort_game_title = stringFromObjectOrDefault(
                        b, "resort_game_title", out.selection_cursor.speech_bubble.resort_game_title);
                    out.selection_cursor.speech_bubble.pokemon_label_format = stringFromObjectOrDefault(
                        b, "pokemon_label_format", out.selection_cursor.speech_bubble.pokemon_label_format);
                    out.selection_cursor.speech_bubble.default_pokemon_level = intFromObjectOrDefault(
                        b, "default_pokemon_level", out.selection_cursor.speech_bubble.default_pokemon_level);
                    out.selection_cursor.speech_bubble.empty_slot_label = stringFromObjectOrDefault(
                        b, "empty_slot_label", out.selection_cursor.speech_bubble.empty_slot_label);
                }
            }
        }
    }

    return out;
}

} // namespace pr::transfer_system
