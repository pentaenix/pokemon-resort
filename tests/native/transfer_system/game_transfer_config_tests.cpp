#include "ui/transfer_system/GameTransferConfig.hpp"

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

struct TempProject {
    fs::path root;
    fs::path config_dir;

    TempProject() {
        root = fs::temp_directory_path() / ("pokemon_resort_transfer_config_" + std::to_string(::getpid()));
        fs::remove_all(root);
        config_dir = root / "config";
        fs::create_directories(config_dir);
    }

    ~TempProject() {
        std::error_code error;
        fs::remove_all(root, error);
    }
};

void writeText(const fs::path& path, const std::string& text) {
    std::ofstream out(path);
    out << text;
}

void testGameTransferConfigParsesFullScreenStyleSurface() {
    TempProject temp;
    writeText(
        temp.config_dir / "game_transfer.json",
        R"json({
  "fade": { "in_seconds": 0.75, "out_seconds": 0.33 },
  "background_animation": { "enabled": true, "scale": 1.25, "speed_x": 4.5, "speed_y": -2.0 },
  "box_viewport": {
    "item_tool": {
      "item_size": 48,
      "grow_smoothing": 21.5,
        "sprite_mod_color": "#8899aa",
        "sprite_mod_alpha": 127
    }
  },
  "mini_preview": { "enabled": true, "width": 190, "height": 150, "sprite_scale": 1.6 },
  "pokemon_action_menu": {
    "width": 205,
    "row_height": 37,
    "padding_y": 8,
    "gap_from_slot": 11,
    "corner_radius": 9,
    "border_thickness": 3,
    "font_pt": 23,
    "grow_smoothing": 19.5,
    "background_color": "#fafafa",
    "background_alpha": 222,
    "border_color": "#dd3344",
    "selected_row_color": "#fedcba",
    "selected_row_alpha": 111,
    "text_color": "#112233",
    "dim_background_sprites": false,
    "dim_sprite_mod_color": "#778899",
    "dim_sprite_mod_alpha": 99,
    "modal_move_swaps_into_hand": true,
    "swap_tool_swaps_into_hand": false,
    "held_sprite_shadow_enabled": false,
    "held_sprite_shadow_color": "#123456",
    "held_sprite_shadow_alpha": 77,
    "held_sprite_shadow_offset_y": 14,
    "held_sprite_shadow_width": 44,
    "held_sprite_shadow_height": 11,
    "held_sprite_scale_multiplier": 1.35
  },
  "info_banner": {
    "enabled": true,
    "separator_height": 5,
    "info_height": 88,
    "icon_directory": "custom/icons",
    "game_icon_directory": "custom/game_icons",
    "unknown_icon": "custom/unknown.png",
    "text_defaults": {
      "font_pt": 24,
      "label_font_pt": 14,
      "color": "#445566",
      "label_color": "#223344"
    },
    "gender_symbol": {
      "male_color": "#88ccff",
      "female_color": "#ff5566",
      "font_pt": 31,
      "x_adjust": -3,
      "y_adjust": 4
    },
    "tool_basic_title": "Basic Mode",
    "pill_items_body": "Items body",
    "box_space_title": "Box Overview",
    "box_space_body": "Open any PC box quickly.",
    "layout": {
      "pokemon": {
        "rows": {
          "top": { "text_y": 7, "icon_y": 9, "icon_size": 40 }
        },
        "columns": {
          "icons": { "x": 18, "step": 44, "icon_size": 50, "scale": 0.8 },
          "main": { "x": 60 }
        }
      }
    },
    "fields": [
      { "field": "ball_icon", "kind": "icon", "row": "top", "column": "icons", "slot": 1, "scale": 1.25, "height_scale": 1.5, "flow_group": "top_icons", "flow_gap": 3, "contexts": ["pokemon"] },
      { "field": "nickname", "kind": "text", "label": "Name", "empty_text": "-", "row": "top", "column": "main", "contexts": ["pokemon", "empty"] }
    ]
  },
  "pill_toggle": {
    "track_width": 190,
    "track_height": 38,
    "pill_width": 92,
    "pill_height": 32,
    "gap_above_boxes": 20,
    "font_pt": 18,
    "track_color": "#112233",
    "pill_color": "#aabbcc",
    "toggle_smoothing": 9.5,
    "box_smoothing": 8.25
  },
  "tool_carousel": {
    "viewport_width": 205,
    "viewport_height": 92,
    "slide_span_pixels": 144,
    "slide_smoothing": 12.5,
    "texture_multiple": "custom/multiple.png",
    "texture_basic": "custom/basic.png",
    "texture_swap": "custom/swap.png",
    "texture_items": "custom/items.png",
    "frame_multiple": "#123456",
    "frame_basic": "#234567",
    "frame_swap": "#345678",
    "frame_items": "#456789"
  },
  "box_name_dropdown": {
    "enabled": true,
    "panel_width_pixels": 240,
    "item_font_pt": 19,
    "selected_row_tint": "#abcdef",
    "selected_row_tint_alpha": 140,
    "scroll_drag_multiplier": 0.9
  },
  "selection_cursor": {
    "enabled": true,
    "color": "#654321",
    "alpha": 210,
    "thickness": 3,
    "speech_bubble": {
      "enabled": true,
      "font_pt": 21,
      "text_color": "#010203",
      "fill_color": "#fefefe",
      "pokemon_label_format": "{species} Lv.{level}",
      "empty_slot_label": "None"
    }
  }
})json");

    const auto loaded = pr::transfer_system::loadGameTransfer(temp.root.string());

    expect(loaded.fade_in_seconds == 0.75, "fade.in_seconds should parse");
    expect(loaded.fade_out_seconds == 0.33, "fade.out_seconds should parse");
    expect(loaded.background_animation.enabled, "background animation enabled should parse");
    expect(loaded.background_animation.scale == 1.25, "background animation scale should parse");
    expect(loaded.background_animation.speed_x == 4.5, "background animation speed_x should parse");
    expect(loaded.background_animation.speed_y == -2.0, "background animation speed_y should parse");
    expect(loaded.box_viewport.item_tool_item_size == 48, "item tool item size should parse");
    expect(loaded.box_viewport.item_tool_grow_smoothing == 21.5, "item tool grow smoothing should parse");
    expect(loaded.box_viewport.item_tool_sprite_mod_color.g == 0x99, "item tool sprite mod color should parse");
    expect(loaded.box_viewport.item_tool_sprite_mod_color.a == 127, "item tool sprite mod alpha should parse");
    expect(loaded.mini_preview.width == 190, "mini preview width should parse");
    expect(loaded.mini_preview.height == 150, "mini preview height should parse");
    expect(loaded.mini_preview.sprite_scale == 1.6, "mini preview sprite scale should parse");
    expect(loaded.pokemon_action_menu.width == 205, "pokemon action menu width should parse");
    expect(loaded.pokemon_action_menu.row_height == 37, "pokemon action menu row height should parse");
    expect(loaded.pokemon_action_menu.gap_from_slot == 11, "pokemon action menu gap should parse");
    expect(loaded.pokemon_action_menu.font_pt == 23, "pokemon action menu font size should parse");
    expect(loaded.pokemon_action_menu.grow_smoothing == 19.5, "pokemon action menu grow smoothing should parse");
    expect(loaded.pokemon_action_menu.background_color.a == 222, "pokemon action menu background alpha should parse");
    expect(loaded.pokemon_action_menu.border_color.g == 0x33, "pokemon action menu border color should parse");
    expect(loaded.pokemon_action_menu.selected_row_color.g == 0xdc, "pokemon action menu selected row color should parse");
    expect(loaded.pokemon_action_menu.selected_row_color.a == 111, "pokemon action menu selected row alpha should parse");
    expect(loaded.pokemon_action_menu.text_color.b == 0x33, "pokemon action menu text color should parse");
    expect(!loaded.pokemon_action_menu.dim_background_sprites, "pokemon action menu dim flag should parse");
    expect(loaded.pokemon_action_menu.dim_sprite_mod_color.g == 0x88, "pokemon action menu dim color should parse");
    expect(loaded.pokemon_action_menu.dim_sprite_mod_color.a == 99, "pokemon action menu dim alpha should parse");
    expect(loaded.pokemon_action_menu.modal_move_swaps_into_hand, "pokemon action menu modal move swap policy should parse");
    expect(!loaded.pokemon_action_menu.swap_tool_swaps_into_hand, "pokemon action menu swap tool policy should parse");
    expect(!loaded.pokemon_action_menu.held_sprite_shadow_enabled, "pokemon action menu held shadow enabled should parse");
    expect(loaded.pokemon_action_menu.held_sprite_shadow_color.g == 0x34, "pokemon action menu held shadow color should parse");
    expect(loaded.pokemon_action_menu.held_sprite_shadow_color.a == 77, "pokemon action menu held shadow alpha should parse");
    expect(loaded.pokemon_action_menu.held_sprite_shadow_offset_y == 14, "pokemon action menu held shadow offset should parse");
    expect(loaded.pokemon_action_menu.held_sprite_shadow_width == 44, "pokemon action menu held shadow width should parse");
    expect(loaded.pokemon_action_menu.held_sprite_shadow_height == 11, "pokemon action menu held shadow height should parse");
    expect(
        loaded.pokemon_action_menu.held_sprite_scale_multiplier == 1.35,
        "pokemon action menu held sprite scale multiplier should parse");
    expect(loaded.info_banner.info_height == 88, "info banner height should parse");
    expect(loaded.info_banner.game_icon_directory == "custom/game_icons", "info banner game icon directory should parse");
    expect(loaded.info_banner.tool_basic_title == "Basic Mode", "info banner tooltip title should parse");
    expect(loaded.info_banner.pill_items_body == "Items body", "info banner pill copy should parse");
    expect(loaded.info_banner.box_space_title == "Box Overview", "info banner box space title should parse");
    expect(loaded.info_banner.box_space_body == "Open any PC box quickly.", "info banner box space body should parse");
    expect(loaded.info_banner.fields.size() == 2, "info banner fields should parse");
    expect(loaded.info_banner.fields[0].field == "ball_icon", "info banner first field name should parse");
    expect(loaded.info_banner.fields[0].x == 62, "info banner layout columns and slot step should resolve icon x");
    expect(loaded.info_banner.fields[0].y == 9, "info banner layout row should resolve icon y");
    expect(loaded.info_banner.fields[0].width == 50, "info banner section and field icon scale should resolve width");
    expect(loaded.info_banner.fields[0].height == 60, "info banner icon height scale should resolve height");
    expect(loaded.info_banner.fields[0].flow_group == "top_icons", "info banner icon flow group should parse");
    expect(loaded.info_banner.fields[0].flow_gap == 3, "info banner icon flow gap should parse");
    expect(loaded.info_banner.fields[1].x == 60, "info banner layout columns should resolve text x");
    expect(loaded.info_banner.fields[1].y == 7, "info banner layout row should resolve text y");
    expect(loaded.info_banner.fields[1].font_pt == 24, "info banner text default font size should parse");
    expect(loaded.info_banner.fields[1].label_font_pt == 14, "info banner label default font size should parse");
    expect(loaded.info_banner.fields[1].color.g == 0x55, "info banner text default color should parse");
    expect(loaded.info_banner.fields[1].label_color.g == 0x33, "info banner label default color should parse");
    expect(loaded.info_banner.gender_symbol_male_color.g == 0xcc, "gender male color should parse");
    expect(loaded.info_banner.gender_symbol_female_color.b == 0x66, "gender female color should parse");
    expect(loaded.info_banner.gender_symbol_font_pt == 31, "gender symbol font size should parse");
    expect(loaded.info_banner.gender_symbol_x_adjust == -3, "gender symbol x adjust should parse");
    expect(loaded.info_banner.gender_symbol_y_adjust == 4, "gender symbol y adjust should parse");
    expect(loaded.info_banner.fields[1].label == "Name", "info banner field label should parse");
    expect(loaded.info_banner.fields[1].contexts.size() == 2, "info banner field contexts should parse");
    expect(loaded.pill_toggle.track_width == 190, "pill track width should parse");
    expect(loaded.pill_toggle.track_color.r == 0x11, "pill track color should parse");
    expect(loaded.pill_toggle.pill_color.b == 0xcc, "pill color should parse");
    expect(loaded.pill_toggle.toggle_smoothing == 9.5, "pill toggle smoothing should parse");
    expect(loaded.tool_carousel.slide_span_pixels == 144, "tool carousel slide span should parse");
    expect(loaded.tool_carousel.texture_items == "custom/items.png", "tool carousel textures should parse");
    expect(loaded.tool_carousel.frame_swap.g == 0x56, "tool carousel frame colors should parse");
    expect(loaded.box_name_dropdown.enabled, "dropdown enabled should parse");
    expect(loaded.box_name_dropdown.panel_width_pixels == 240, "dropdown width should parse");
    expect(loaded.box_name_dropdown.selected_row_tint.a == 140, "dropdown tint alpha should parse");
    expect(loaded.selection_cursor.enabled, "selection cursor enabled should parse");
    expect(loaded.selection_cursor.alpha == 210, "selection cursor alpha should parse");
    expect(loaded.selection_cursor.speech_bubble.enabled, "speech bubble enabled should parse");
    expect(loaded.selection_cursor.speech_bubble.font_pt == 21, "speech bubble font size should parse");
    expect(loaded.selection_cursor.speech_bubble.empty_slot_label == "None", "speech bubble empty label should parse");
  }

void testResortPcBoxCountParsesAndClamps() {
    TempProject temp;
    writeText(temp.config_dir / "game_transfer.json", R"json({ "resort_pc_box_count": 42 })json");
    const pr::transfer_system::LoadedGameTransfer loaded = pr::transfer_system::loadGameTransfer(temp.root.string());
    expect(loaded.resort_pc_box_count == 42, "resort_pc_box_count should parse from minimal game_transfer.json");

    writeText(temp.config_dir / "game_transfer.json", R"json({ "resort_pc_box_count": 0 })json");
    const pr::transfer_system::LoadedGameTransfer clamp_low = pr::transfer_system::loadGameTransfer(temp.root.string());
    expect(clamp_low.resort_pc_box_count == 1, "resort_pc_box_count below 1 should clamp to 1");

    writeText(temp.config_dir / "game_transfer.json", R"json({ "resort_pc_box_count": 900 })json");
    const pr::transfer_system::LoadedGameTransfer clamp_high = pr::transfer_system::loadGameTransfer(temp.root.string());
    expect(clamp_high.resort_pc_box_count == 512, "resort_pc_box_count above 512 should clamp to 512");
}

} // namespace

int main() {
    testGameTransferConfigParsesFullScreenStyleSurface();
    testResortPcBoxCountParsesAndClamps();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
