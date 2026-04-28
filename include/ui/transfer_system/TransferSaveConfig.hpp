#pragma once

#include "core/Types.hpp"

#include <string>

namespace pr::transfer_system {

struct ExitSaveModalStyle {
    bool enabled = true;
    double enter_smoothing = 18.0;
    double exit_smoothing = 18.0;
    int offscreen_pad = 40;
    int shown_x = 40;
    int gap_above_info_banner = 18;
    int width = 460;
    int row_height = 52;
    int row_gap = 10;
    int padding_x = 18;
    int padding_top = 18;
    int padding_bottom = 18;
    int corner_radius = 12;
    int border_thickness = 3;
    bool dim_background = true;
    Color dim_color{0, 0, 0, 255};
    int dim_alpha = 140;
    Color card_fill{255, 255, 255, 255};
    int card_fill_alpha = 246;
    Color card_border{208, 208, 208, 255};
    int card_border_alpha = 220;
    Color row_fill{255, 255, 255, 255};
    int row_fill_alpha = 0;
    Color row_border{255, 255, 255, 255};
    int row_border_alpha = 0;
    Color selected_row_fill{191, 191, 191, 255};
    int selected_row_fill_alpha = 160;
    Color selected_row_border{138, 138, 138, 255};
    int selected_row_border_alpha = 220;
    Color text_color{17, 17, 17, 255};
    int text_alpha = 255;
    int font_pt = 28;
};

struct LoadedTransferSave {
    ExitSaveModalStyle exit_save_modal{};
};

LoadedTransferSave loadTransferSave(const std::string& project_root);

} // namespace pr::transfer_system

