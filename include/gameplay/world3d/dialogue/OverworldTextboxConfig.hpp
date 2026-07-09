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
    std::string future_text_font_path = "assets/fonts/Power.ttf";
    int future_text_font_size_px = 16;
};

OverworldTextboxConfig loadOverworldTextboxConfig(const std::string& project_root);
bool overworldTextboxEnabled(const OverworldTextboxConfig& config);
int clampedTextboxSkinIndex(const OverworldTextboxConfig& config);

} // namespace pr::gameplay::world3d::dialogue
