#pragma once

#include <SDL.h>

namespace pr {

struct OverlayThreeSliceConfig {
    int selected_index = 0;
    int valid_count = 1;
    int cell_width = 1;
    int cell_height = 1;
    int sheet_columns = 1;
    int side_padding = 0;
    int bottom_padding = 0;
    int stretch_width = 1;
    int stretch_center_x = 0;
};

struct OverlayThreeSliceLayout {
    SDL_Rect left_src{};
    SDL_Rect middle_src{};
    SDL_Rect right_src{};
    SDL_Rect left_dst{};
    SDL_Rect middle_dst{};
    SDL_Rect right_dst{};
    bool visible = false;
};

int clampedOverlaySliceIndex(const OverlayThreeSliceConfig& config);
SDL_Rect overlaySliceSourceRect(const OverlayThreeSliceConfig& config, int sheet_w, int sheet_h);
OverlayThreeSliceLayout buildOverlayThreeSliceLayout(
    const OverlayThreeSliceConfig& config,
    int viewport_w,
    int viewport_h,
    int sheet_w,
    int sheet_h);
SDL_Rect scaleOverlayRect(const SDL_Rect& rect, const SDL_Rect& viewport_dst, int base_w, int base_h);

} // namespace pr
