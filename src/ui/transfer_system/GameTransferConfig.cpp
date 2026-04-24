#include "ui/transfer_system/GameTransferConfig.hpp"

#include "core/Json.hpp"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace pr::transfer_system {

namespace {

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

} // namespace

LoadedGameTransfer loadGameTransfer(const std::string& project_root) {
    LoadedGameTransfer out;
    const fs::path path = fs::path(project_root) / "config" / "game_transfer.json";
    if (!fs::exists(path)) {
        return out;
    }

    JsonValue root = parseJsonFile(path.string());
    if (!root.isObject()) {
        return out;
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
                        parseHexColorString(c->asString(), out.box_viewport.arrow_mod_color);
                }
            }
            out.box_viewport.box_name_font_pt = intFromObjectOrDefault(o, "box_name_font_pt", out.box_viewport.box_name_font_pt);
            if (const JsonValue* c = o.get("box_name_color")) {
                if (c->isString()) {
                    out.box_viewport.box_name_color =
                        parseHexColorString(c->asString(), out.box_viewport.box_name_color);
                }
            }
            out.box_viewport.box_space_font_pt =
                intFromObjectOrDefault(o, "box_space_font_pt", out.box_viewport.box_space_font_pt);
            if (const JsonValue* c = o.get("box_space_color")) {
                if (c->isString()) {
                    out.box_viewport.box_space_color =
                        parseHexColorString(c->asString(), out.box_viewport.box_space_color);
                }
            }
            out.box_viewport.footer_scroll_arrow_offset_y =
                intFromObjectOrDefault(o, "footer_scroll_arrow_offset_y", out.box_viewport.footer_scroll_arrow_offset_y);
            out.box_viewport.content_slide_smoothing =
                doubleFromObjectOrDefault(o, "content_slide_smoothing", out.box_viewport.content_slide_smoothing);
            out.box_viewport.sprite_scale = doubleFromObjectOrDefault(o, "sprite_scale", out.box_viewport.sprite_scale);
            out.box_viewport.sprite_offset_y = intFromObjectOrDefault(o, "sprite_offset_y", out.box_viewport.sprite_offset_y);

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
                    out.pill_toggle.track_color = parseHexColorString(c->asString(), out.pill_toggle.track_color);
                }
            }
            if (const JsonValue* c = o.get("pill_color")) {
                if (c->isString()) {
                    out.pill_toggle.pill_color = parseHexColorString(c->asString(), out.pill_toggle.pill_color);
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
                        parseHexColorString(c->asString(), out.tool_carousel.viewport_color);
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
            if (const JsonValue* c = o.get("frame_multiple")) {
                if (c->isString()) {
                    out.tool_carousel.frame_multiple =
                        parseHexColorString(c->asString(), out.tool_carousel.frame_multiple);
                }
            }
            if (const JsonValue* c = o.get("frame_basic")) {
                if (c->isString()) {
                    out.tool_carousel.frame_basic = parseHexColorString(c->asString(), out.tool_carousel.frame_basic);
                }
            }
            if (const JsonValue* c = o.get("frame_swap")) {
                if (c->isString()) {
                    out.tool_carousel.frame_swap = parseHexColorString(c->asString(), out.tool_carousel.frame_swap);
                }
            }
            if (const JsonValue* c = o.get("frame_items")) {
                if (c->isString()) {
                    out.tool_carousel.frame_items = parseHexColorString(c->asString(), out.tool_carousel.frame_items);
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
                        parseHexColorString(c->asString(), out.box_name_dropdown.panel_color);
                }
            }
            if (const JsonValue* c = o.get("panel_border_color")) {
                if (c->isString()) {
                    out.box_name_dropdown.panel_border_color =
                        parseHexColorString(c->asString(), out.box_name_dropdown.panel_border_color);
                }
            }
            if (const JsonValue* c = o.get("item_text_color")) {
                if (c->isString()) {
                    out.box_name_dropdown.item_text_color =
                        parseHexColorString(c->asString(), out.box_name_dropdown.item_text_color);
                }
            }
            if (const JsonValue* c = o.get("selected_row_tint")) {
                if (c->isString()) {
                    out.box_name_dropdown.selected_row_tint =
                        parseHexColorString(c->asString(), out.box_name_dropdown.selected_row_tint);
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
                    out.selection_cursor.color = parseHexColorString(c->asString(), out.selection_cursor.color);
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
                                parseHexColorString(c->asString(), out.selection_cursor.speech_bubble.text_color);
                        }
                    }
                    if (const JsonValue* c = b.get("fill_color")) {
                        if (c->isString()) {
                            out.selection_cursor.speech_bubble.fill_color =
                                parseHexColorString(c->asString(), out.selection_cursor.speech_bubble.fill_color);
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
