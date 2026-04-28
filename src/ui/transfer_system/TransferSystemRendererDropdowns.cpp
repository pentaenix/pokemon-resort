#include "ui/TransferSystemScreen.hpp"
#include "ui/transfer_system/detail/SdlScanlineDraw.hpp"

#include <algorithm>
#include <cmath>

namespace pr {

using transfer_system::detail::fillRoundedRectScanlines;
using transfer_system::detail::fillRoundedRingScanlines;

void TransferSystemScreen::drawGameBoxNameDropdownChrome(SDL_Renderer* renderer) const {
    if (!box_name_dropdown_style_.enabled || game_pc_boxes_.empty() || game_box_browser_.dropdownExpandT() <= 1e-6) {
        return;
    }

    SDL_Rect outer{};
    int list_h = 0;
    int list_clip_y = 0;
    if (!computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
        return;
    }

    const int stroke = std::max(1, box_name_dropdown_style_.panel_border_thickness);
    const int rad =
        std::clamp(box_name_dropdown_style_.panel_corner_radius, 0, std::min(outer.w, outer.h) / 2);

    fillRoundedRingScanlines(
        renderer,
        outer.x,
        outer.y,
        outer.w,
        outer.h,
        rad,
        stroke,
        box_name_dropdown_style_.panel_border_color,
        box_name_dropdown_style_.panel_color);
}

void TransferSystemScreen::drawGameBoxNameDropdownList(SDL_Renderer* renderer) const {
    if (!box_name_dropdown_style_.enabled || game_pc_boxes_.empty() || game_box_browser_.dropdownExpandT() <= 1e-6) {
        return;
    }
    if (dropdown_labels_dirty_) {
        const_cast<TransferSystemScreen*>(this)->rebuildDropdownItemTextures(renderer);
    }
    const int n = transfer_system::GameBoxBrowserController::dropdownListRowCount(
        static_cast<int>(game_pc_boxes_.size()));
    if (static_cast<int>(dropdown_item_textures_.size()) != n) {
        return;
    }

    SDL_Rect outer{};
    int list_h = 0;
    int list_clip_y = 0;
    if (!computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
        return;
    }

    const int stroke = std::max(1, box_name_dropdown_style_.panel_border_thickness);
    const int inner_x = outer.x + stroke;
    const int inner_w = outer.w - stroke * 2;
    const int rh = std::max(1, game_box_browser_.dropdownRowHeightPx());

    SDL_Rect clip{inner_x, list_clip_y, inner_w, list_h};
    SDL_RenderSetClipRect(renderer, &clip);

    const Color tint = box_name_dropdown_style_.selected_row_tint;
    for (int i = 0; i < n; ++i) {
        const int row_top = list_clip_y + i * rh - static_cast<int>(std::lround(game_box_browser_.dropdownScrollPx()));
        if (row_top + rh < list_clip_y || row_top > list_clip_y + list_h) {
            continue;
        }
        if (i == game_box_browser_.dropdownHighlightIndex()) {
            const int rr = std::max(0, std::min(8, rh / 4));
            fillRoundedRectScanlines(renderer, inner_x, row_top, inner_w, rh, rr, tint);
        }
        if (i < static_cast<int>(dropdown_item_textures_.size()) && dropdown_item_textures_[i].texture) {
            const TextureHandle& tex = dropdown_item_textures_[i];
            const int tcx = inner_x + inner_w / 2 - tex.width / 2;
            const int tcy = row_top + (rh - tex.height) / 2;
            SDL_Rect dst{tcx, tcy, tex.width, tex.height};
            SDL_RenderCopy(renderer, tex.texture.get(), nullptr, &dst);
        }
    }

    SDL_RenderSetClipRect(renderer, nullptr);
}

void TransferSystemScreen::drawResortBoxNameDropdownChrome(SDL_Renderer* renderer) const {
    if (!box_name_dropdown_style_.enabled || resort_pc_boxes_.empty() || resort_box_browser_.dropdownExpandT() <= 1e-6) {
        return;
    }

    SDL_Rect outer{};
    int list_h = 0;
    int list_clip_y = 0;
    if (!computeResortBoxDropdownOuterRect(outer, static_cast<float>(resort_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
        return;
    }

    const int stroke = std::max(1, box_name_dropdown_style_.panel_border_thickness);
    const int rad =
        std::clamp(box_name_dropdown_style_.panel_corner_radius, 0, std::min(outer.w, outer.h) / 2);

    fillRoundedRingScanlines(
        renderer,
        outer.x,
        outer.y,
        outer.w,
        outer.h,
        rad,
        stroke,
        box_name_dropdown_style_.panel_border_color,
        box_name_dropdown_style_.panel_color);
}

void TransferSystemScreen::drawResortBoxNameDropdownList(SDL_Renderer* renderer) const {
    if (!box_name_dropdown_style_.enabled || resort_pc_boxes_.empty() || resort_box_browser_.dropdownExpandT() <= 1e-6) {
        return;
    }
    if (resort_dropdown_labels_dirty_) {
        const_cast<TransferSystemScreen*>(this)->rebuildResortDropdownItemTextures(renderer);
    }
    const int n = transfer_system::GameBoxBrowserController::dropdownListRowCount(
        static_cast<int>(resort_pc_boxes_.size()));
    if (static_cast<int>(resort_dropdown_item_textures_.size()) != n) {
        return;
    }

    SDL_Rect outer{};
    int list_h = 0;
    int list_clip_y = 0;
    if (!computeResortBoxDropdownOuterRect(outer, static_cast<float>(resort_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
        return;
    }

    const int stroke = std::max(1, box_name_dropdown_style_.panel_border_thickness);
    const int inner_x = outer.x + stroke;
    const int inner_w = outer.w - stroke * 2;
    const int rh = std::max(1, resort_box_browser_.dropdownRowHeightPx());

    SDL_Rect clip{inner_x, list_clip_y, inner_w, list_h};
    SDL_RenderSetClipRect(renderer, &clip);

    const Color tint = box_name_dropdown_style_.selected_row_tint;
    for (int i = 0; i < n; ++i) {
        const int row_top = list_clip_y + i * rh - static_cast<int>(std::lround(resort_box_browser_.dropdownScrollPx()));
        if (row_top + rh < list_clip_y || row_top > list_clip_y + list_h) {
            continue;
        }
        if (i == resort_box_browser_.dropdownHighlightIndex()) {
            const int rr = std::max(0, std::min(8, rh / 4));
            fillRoundedRectScanlines(renderer, inner_x, row_top, inner_w, rh, rr, tint);
        }
        if (i < static_cast<int>(resort_dropdown_item_textures_.size()) && resort_dropdown_item_textures_[i].texture) {
            const TextureHandle& tex = resort_dropdown_item_textures_[i];
            const int tcx = inner_x + inner_w / 2 - tex.width / 2;
            const int tcy = row_top + (rh - tex.height) / 2;
            SDL_Rect dst{tcx, tcy, tex.width, tex.height};
            SDL_RenderCopy(renderer, tex.texture.get(), nullptr, &dst);
        }
    }

    SDL_RenderSetClipRect(renderer, nullptr);
}

} // namespace pr

