#pragma once

#include "core/Types.hpp"

#include <string>

namespace pr::transfer_system {

struct BackgroundAnimLoaded {
    bool enabled = false;
    double scale = 1.0;
    double speed_x = 0.0;
    double speed_y = 0.0;
};

struct LoadedGameTransfer {
    double fade_in_seconds = 0;
    double fade_out_seconds = 0.12;
    BackgroundAnimLoaded background_animation{};
    GameTransferBoxViewportStyle box_viewport{};
    GameTransferMiniPreviewStyle mini_preview{};
    GameTransferPillToggleStyle pill_toggle{};
    GameTransferToolCarouselStyle tool_carousel{};
    GameTransferBoxNameDropdownStyle box_name_dropdown{};
    GameTransferSelectionCursorStyle selection_cursor{};
};

LoadedGameTransfer loadGameTransfer(const std::string& project_root);

} // namespace pr::transfer_system

