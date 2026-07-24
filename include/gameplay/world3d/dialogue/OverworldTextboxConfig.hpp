#pragma once

#include <string>

namespace pr::gameplay::world3d::dialogue {

struct OverworldTextboxConfig {
    std::string visible_mode = "enabled";
    std::string sprite_sheet_path = "assets/overworld/ui/text_boxes.png";
    int selected_skin_index = 0;
    int valid_skin_count = 13;
    int bottom_padding_px = 5;
    int side_padding_px = 5;
    int source_cell_width_px = 256;
    int source_cell_height_px = 48;
    int sheet_columns = 2;
    int stretch_strip_width_px = 5;
    int stretch_strip_center_x_px = 128;
    std::string text_font_path = "assets/fonts/power clear.ttf";
    int text_font_size_px = 14;
    int text_left_inset_px = 16;
    int text_right_inset_px = 12;
    int text_top_inset_px = 7;
    bool attend_button_enabled = true;
    std::string attend_button_icon_path = "assets/overworld/ui/attend_icon.png";
    int attend_button_top_px = 16;
    int attend_button_right_px = 16;
    int attend_button_width_px = 48;
    int attend_button_height_px = 48;
};

OverworldTextboxConfig loadOverworldTextboxConfig(const std::string& project_root);
bool overworldTextboxEnabled(const OverworldTextboxConfig& config);
int clampedTextboxSkinIndex(const OverworldTextboxConfig& config);

} // namespace pr::gameplay::world3d::dialogue
