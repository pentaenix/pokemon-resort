#include "ui/overlay/OverlaySliceLayout.hpp"

#include <algorithm>
#include <cmath>

namespace pr {

int clampedOverlaySliceIndex(const OverlayThreeSliceConfig& config) {
    return std::clamp(config.selected_index, 0, std::max(0, config.valid_count - 1));
}

SDL_Rect overlaySliceSourceRect(const OverlayThreeSliceConfig& config, int sheet_w, int sheet_h) {
    const int cell_w = std::max(1, config.cell_width);
    const int cell_h = std::max(1, config.cell_height);
    const int columns = std::max(1, config.sheet_columns);
    const int rows = std::max(1, sheet_h / cell_h);
    const int valid_index = clampedOverlaySliceIndex(config);
    const int col = valid_index / rows;
    const int row = valid_index % rows;
    if (col >= columns || ((col + 1) * cell_w) > sheet_w || ((row + 1) * cell_h) > sheet_h) {
        return SDL_Rect{0, 0, 0, 0};
    }
    return SDL_Rect{col * cell_w, row * cell_h, cell_w, cell_h};
}

OverlayThreeSliceLayout buildOverlayThreeSliceLayout(
    const OverlayThreeSliceConfig& config,
    int viewport_w,
    int viewport_h,
    int sheet_w,
    int sheet_h) {
    OverlayThreeSliceLayout layout{};
    if (viewport_w <= 0 || viewport_h <= 0) {
        return layout;
    }

    const SDL_Rect skin = overlaySliceSourceRect(config, sheet_w, sheet_h);
    if (skin.w <= 0 || skin.h <= 0) {
        return layout;
    }

    const int side_padding = std::max(0, config.side_padding);
    const int bottom_padding = std::max(0, config.bottom_padding);
    const int box_w = std::max(1, viewport_w - (side_padding * 2));
    const int box_h = std::max(1, config.cell_height);
    const int box_x = side_padding;
    const int box_y = std::max(0, viewport_h - bottom_padding - box_h);

    const int strip_w = std::clamp(config.stretch_width, 1, skin.w);
    const int strip_x = skin.x + std::clamp(
        config.stretch_center_x - (strip_w / 2),
        0,
        std::max(0, skin.w - strip_w));
    const int left_w = std::max(0, strip_x - skin.x);
    const int right_x = strip_x + strip_w;
    const int right_w = std::max(0, (skin.x + skin.w) - right_x);
    const int middle_dst_w = std::max(1, box_w - left_w - right_w);

    layout.left_src = SDL_Rect{skin.x, skin.y, left_w, skin.h};
    layout.middle_src = SDL_Rect{strip_x, skin.y, strip_w, skin.h};
    layout.right_src = SDL_Rect{right_x, skin.y, right_w, skin.h};
    layout.left_dst = SDL_Rect{box_x, box_y, left_w, box_h};
    layout.middle_dst = SDL_Rect{box_x + left_w, box_y, middle_dst_w, box_h};
    layout.right_dst = SDL_Rect{box_x + left_w + middle_dst_w, box_y, right_w, box_h};
    layout.visible = left_w > 0 && right_w > 0 && box_w > left_w + right_w;
    return layout;
}

SDL_Rect scaleOverlayRect(const SDL_Rect& rect, const SDL_Rect& viewport_dst, int base_w, int base_h) {
    const double sx = static_cast<double>(viewport_dst.w) / static_cast<double>(std::max(1, base_w));
    const double sy = static_cast<double>(viewport_dst.h) / static_cast<double>(std::max(1, base_h));
    const int x0 = viewport_dst.x + static_cast<int>(std::lround(static_cast<double>(rect.x) * sx));
    const int y0 = viewport_dst.y + static_cast<int>(std::lround(static_cast<double>(rect.y) * sy));
    const int x1 = viewport_dst.x + static_cast<int>(std::lround(static_cast<double>(rect.x + rect.w) * sx));
    const int y1 = viewport_dst.y + static_cast<int>(std::lround(static_cast<double>(rect.y + rect.h) * sy));
    return SDL_Rect{x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)};
}

} // namespace pr
