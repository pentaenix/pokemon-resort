#include "ui/TransferSystemScreen.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>

namespace pr {

void TransferSystemScreen::updateResortBoxDropdown(double dt) {
    resort_box_browser_.updateDropdown(dt, box_name_dropdown_style_);
    if (!resort_box_browser_.dropdownOpenTarget() && resort_box_browser_.dropdownExpandT() < 0.02) {
        resort_dropdown_item_textures_.clear();
    }
}

void TransferSystemScreen::rebuildResortDropdownItemTextures(SDL_Renderer* renderer) {
    resort_dropdown_item_textures_.clear();
    if (!dropdown_item_font_.get()) {
        resort_dropdown_labels_dirty_ = false;
        return;
    }
    const Color text_color = box_name_dropdown_style_.item_text_color;
    int max_h = 8;
    const int box_n = static_cast<int>(resort_pc_boxes_.size());
    if (transfer_system::GameBoxBrowserController::dropdownListRowCount(box_n) > box_n) {
        TextureHandle rename_tex =
            renderTextTexture(renderer, dropdown_item_font_.get(), "RENAME BOX...", text_color);
        max_h = std::max(max_h, rename_tex.height);
        resort_dropdown_item_textures_.push_back(std::move(rename_tex));
    }
    for (const auto& box : resort_pc_boxes_) {
        TextureHandle tex = renderTextTexture(renderer, dropdown_item_font_.get(), box.name, text_color);
        max_h = std::max(max_h, tex.height);
        resort_dropdown_item_textures_.push_back(std::move(tex));
    }
    resort_box_browser_.setDropdownRowHeightPx(max_h + std::max(0, box_name_dropdown_style_.row_padding_y) * 2);
    resort_dropdown_labels_dirty_ = false;
}

bool TransferSystemScreen::computeResortBoxDropdownOuterRect(
    SDL_Rect& out_outer,
    float expand_scale,
    int& out_list_inner_h,
    int& out_list_clip_top_y) const {
    if (!resort_box_viewport_) {
        return false;
    }
    SDL_Rect pill{};
    if (!resort_box_viewport_->getNamePlateBounds(pill)) {
        return false;
    }
    const int stroke = std::max(0, box_name_dropdown_style_.panel_border_thickness);
    const int panel_w = std::max(1, box_name_dropdown_style_.panel_width_pixels);
    const int cx = pill.x + pill.w / 2;
    const int panel_left = cx - panel_w / 2;
    const int chrome_top = pill.y + pill.h / 2;
    const int list_top = pill.y + pill.h;
    out_list_clip_top_y = list_top;

    const int screen_h = window_config_.virtual_height;
    const int bottom_margin = std::max(0, box_name_dropdown_style_.bottom_margin_pixels);
    const int ref_h = std::max(1, box_name_dropdown_style_.reference_name_plate_height_pixels);
    const float mult = std::max(0.1f, box_name_dropdown_style_.max_height_multiplier);
    const int max_by_spec = static_cast<int>(std::lround(static_cast<float>(ref_h) * mult));
    const int available_for_list = screen_h - list_top - bottom_margin;
    const int raw_list_cap = std::max(1, std::min(max_by_spec, std::max(1, available_for_list)));
    const int count = std::max(1, static_cast<int>(resort_pc_boxes_.size()));
    const int rh = std::max(1, resort_box_browser_.dropdownRowHeightPx());
    const int rows = transfer_system::GameBoxBrowserController::dropdownListRowCount(count);
    const int content_h = rows * rh;
    const int inner_list_max = std::min(raw_list_cap, std::max(rh, content_h));
    const float es = std::clamp(expand_scale, 0.f, 1.f);
    out_list_inner_h = std::max(1, static_cast<int>(std::lround(static_cast<double>(inner_list_max) * static_cast<double>(es))));

    const int outer_top = chrome_top;
    const int inner_fill_top = outer_top + stroke;
    const int stem_h = std::max(0, list_top - inner_fill_top);
    const int total_inner_fill_h = stem_h + out_list_inner_h;
    const int outer_w = panel_w + stroke * 2;
    const int outer_h = total_inner_fill_h + stroke * 2;
    out_outer = SDL_Rect{panel_left - stroke, outer_top, outer_w, outer_h};
    return true;
}

bool TransferSystemScreen::hitTestResortBoxNamePlate(int logical_x, int logical_y) const {
    if (!resort_box_viewport_) {
        return false;
    }
    SDL_Rect r{};
    return resort_box_viewport_->getNamePlateBounds(r) && logical_x >= r.x && logical_x < r.x + r.w &&
           logical_y >= r.y && logical_y < r.y + r.h;
}

std::optional<int> TransferSystemScreen::resortDropdownRowIndexAtScreen(int logical_x, int logical_y) const {
    if (!resort_box_browser_.dropdownOpenTarget() || resort_pc_boxes_.empty()) {
        return std::nullopt;
    }
    SDL_Rect outer{};
    int list_h = 0;
    int list_clip_y = 0;
    if (!computeResortBoxDropdownOuterRect(outer, static_cast<float>(resort_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
        return std::nullopt;
    }
    const int stroke = std::max(0, box_name_dropdown_style_.panel_border_thickness);
    const int inner_x = outer.x + stroke;
    const int inner_w = outer.w - stroke * 2;
    if (logical_x < inner_x || logical_x >= inner_x + inner_w || logical_y < list_clip_y ||
        logical_y >= list_clip_y + list_h) {
        return std::nullopt;
    }
    const int rh = std::max(1, resort_box_browser_.dropdownRowHeightPx());
    const double rel_y = static_cast<double>(logical_y - list_clip_y) + resort_box_browser_.dropdownScrollPx();
    const int idx = static_cast<int>(std::floor(rel_y / static_cast<double>(rh)));
    const int rows = transfer_system::GameBoxBrowserController::dropdownListRowCount(
        static_cast<int>(resort_pc_boxes_.size()));
    if (idx < 0 || idx >= rows) {
        return std::nullopt;
    }
    return idx;
}

void TransferSystemScreen::clampResortDropdownScroll(int inner_draw_h) {
    resort_box_browser_.clampDropdownScroll(static_cast<int>(resort_pc_boxes_.size()), inner_draw_h);
}

void TransferSystemScreen::syncResortDropdownScrollToHighlight(int inner_draw_h) {
    resort_box_browser_.syncDropdownScrollToHighlight(static_cast<int>(resort_pc_boxes_.size()), inner_draw_h);
}

void TransferSystemScreen::closeResortBoxDropdown() {
    resort_box_browser_.closeGameBoxDropdown();
}

void TransferSystemScreen::toggleResortBoxDropdown() {
    SDL_Rect outer{};
    int list_h = 0;
    int list_clip_y = 0;
    const int inner_h = computeResortBoxDropdownOuterRect(outer, 1.f, list_h, list_clip_y) ? list_h : 0;
    if (resort_box_browser_.toggleGameBoxDropdown(
            box_name_dropdown_style_.enabled,
            resort_box_browser_.gameBoxSpaceMode(),
            static_cast<int>(resort_pc_boxes_.size()),
            inner_h)) {
        resort_dropdown_labels_dirty_ = true;
        ui_state_.requestButtonSfx();
    }
    if (resort_box_browser_.dropdownOpenTarget()) {
        closeGameBoxDropdown();
    }
}

void TransferSystemScreen::stepResortDropdownHighlight(int delta) {
    SDL_Rect outer{};
    int list_h = 0;
    int list_clip_y = 0;
    const int inner_h =
        computeResortBoxDropdownOuterRect(outer, static_cast<float>(resort_box_browser_.dropdownExpandT()), list_h, list_clip_y)
            ? list_h
            : 0;
    resort_box_browser_.stepDropdownHighlight(delta, static_cast<int>(resort_pc_boxes_.size()), inner_h);
}

void TransferSystemScreen::applyResortBoxDropdownSelection() {
    if (!resort_box_viewport_ || resort_pc_boxes_.empty()) {
        return;
    }
    const int n = static_cast<int>(resort_pc_boxes_.size());
    if (n < 2) {
        return;
    }
    const int hi = resort_box_browser_.dropdownHighlightIndex();
    if (hi <= 0) {
        closeResortBoxDropdown();
        openBoxRenameModal(BoxRenameModalPanel::Resort);
        return;
    }
    const int target_box = hi - 1;
    const bool changed =
        resort_box_browser_.jumpGameBoxToIndex(target_box, n, panelsReadyForInteraction());
    if (!changed) {
        return;
    }
    resort_box_viewport_->snapContentToModel(resortBoxViewportModelAt(resort_box_browser_.gameBoxIndex()));
    ui_state_.requestButtonSfx();
}

bool TransferSystemScreen::resortDropdownNavigationActive() const {
    return box_name_dropdown_style_.enabled && resort_box_browser_.dropdownOpenTarget() &&
           resort_box_browser_.dropdownExpandT() > 0.08 && resort_pc_boxes_.size() >= 2;
}

} // namespace pr

