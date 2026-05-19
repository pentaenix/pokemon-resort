#pragma once

#include "core/Types.hpp"

#include <string>

namespace pr::transfer_system {

struct PokemonSummaryPanelStyle {
    bool enabled = true;
    double enter_smoothing = 18.0;
    double exit_smoothing = 18.0;
    double retracted_box_smoothing = 26.0;
    int width = 650;
    int height = 577;
    int top_y = 100;
    int corner_radius = 16;
    int border_thickness = 3;
    Color fill_color{248, 244, 232, 255};
    Color border_color{201, 190, 147, 255};
    bool open_when_game_box_absent = true;
    int temporary_name_font_pt = 34;
    Color temporary_name_color{98, 92, 46, 255};
};

struct LoadedPokemonSummary {
    PokemonSummaryPanelStyle panel{};
};

LoadedPokemonSummary loadPokemonSummary(const std::string& project_root);

} // namespace pr::transfer_system
