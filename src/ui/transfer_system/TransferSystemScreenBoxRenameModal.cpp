#include "ui/TransferSystemScreen.hpp"

#include "ui/transfer_system/detail/StringUtil.hpp"

#include <SDL.h>

#include <algorithm>

namespace pr {

void TransferSystemScreen::openBoxRenameModal(BoxRenameModalPanel panel) {
    closeGameBoxDropdown();
    closeResortBoxDropdown();
    box_rename_modal_panel_ = panel;
    const int idx =
        panel == BoxRenameModalPanel::Game ? game_box_browser_.gameBoxIndex() : resort_box_browser_.gameBoxIndex();
    box_rename_box_index_ = idx;
    box_rename_original_utf8_.clear();
    if (panel == BoxRenameModalPanel::Game && idx >= 0 && idx < static_cast<int>(game_pc_boxes_.size())) {
        box_rename_original_utf8_ = game_pc_boxes_[static_cast<std::size_t>(idx)].name;
    } else if (panel == BoxRenameModalPanel::Resort && idx >= 0 && idx < static_cast<int>(resort_pc_boxes_.size())) {
        box_rename_original_utf8_ = resort_pc_boxes_[static_cast<std::size_t>(idx)].name;
    }
    box_rename_text_utf8_ = box_rename_original_utf8_;
    box_rename_ime_utf8_.clear();
    box_rename_caret_blink_phase_ = 0.0;
    box_rename_editing_ = false;
    box_rename_focus_slot_ = BoxRenameFocusSlot::Field;
    box_rename_modal_open_ = true;
    syncBoxRenameModalLayout();
}

void TransferSystemScreen::syncBoxRenameModalLayout() {
    if (!box_rename_modal_open_) {
        return;
    }
    const int vw = window_config_.virtual_width;
    const int vh = window_config_.virtual_height;
    const int card_w = std::min(720, vw - 48);
    const int pad = 40;
    const int field_h = 56;
    const int gap = 20;
    const int btn_h = 52;
    const int btn_gap = 16;
    const int inner_w = card_w - pad * 2;
    const int btn_w = (inner_w - btn_gap) / 2;
    const int card_h = pad + field_h + gap + btn_h + pad;
    const int card_x = (vw - card_w) / 2;
    const int card_y = (vh - card_h) / 2;
    box_rename_card_rect_virt_ = SDL_Rect{card_x, card_y, card_w, card_h};
    const int field_y = card_y + pad;
    box_rename_text_field_rect_virt_ = SDL_Rect{card_x + pad, field_y, inner_w, field_h};
    const int btn_y = field_y + field_h + gap;
    box_rename_cancel_button_rect_virt_ = SDL_Rect{card_x + pad, btn_y, btn_w, btn_h};
    box_rename_ok_button_rect_virt_ = SDL_Rect{card_x + pad + btn_w + btn_gap, btn_y, btn_w, btn_h};
}

void TransferSystemScreen::closeBoxRenameModal(bool commit) {
    if (!box_rename_modal_open_) {
        SDL_StopTextInput();
        return;
    }
    box_rename_editing_ = false;
    box_rename_modal_open_ = false;
    if (commit) {
        std::string next = transfer_system::detail::trimAsciiWhitespaceCopy(box_rename_text_utf8_);
        if (next.empty()) {
            next = box_rename_original_utf8_;
        }
        const int idx = box_rename_box_index_;
        if (box_rename_modal_panel_ == BoxRenameModalPanel::Game && idx >= 0 &&
            idx < static_cast<int>(game_pc_boxes_.size())) {
            game_pc_boxes_[static_cast<std::size_t>(idx)].name = std::move(next);
            markGameBoxesDirty();
            refreshGameBoxViewportModel();
            dropdown_labels_dirty_ = true;
        } else if (
            box_rename_modal_panel_ == BoxRenameModalPanel::Resort && idx >= 0 &&
            idx < static_cast<int>(resort_pc_boxes_.size())) {
            resort_pc_boxes_[static_cast<std::size_t>(idx)].name = std::move(next);
            refreshResortBoxViewportModel();
            resort_dropdown_labels_dirty_ = true;
        }
    }
    box_rename_box_index_ = -1;
    box_rename_original_utf8_.clear();
    box_rename_text_utf8_.clear();
    box_rename_ime_utf8_.clear();
    SDL_StopTextInput();
}

bool TransferSystemScreen::handleBoxRenameModalPointerPressed(int logical_x, int logical_y) {
    syncBoxRenameModalLayout();
    auto within = [](int px, int py, const SDL_Rect& r) {
        return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
    };
    if (!within(logical_x, logical_y, box_rename_card_rect_virt_)) {
        closeBoxRenameModal(false);
        ui_state_.requestButtonSfx();
        return true;
    }
    if (within(logical_x, logical_y, box_rename_ok_button_rect_virt_)) {
        closeBoxRenameModal(true);
        ui_state_.requestButtonSfx();
        return true;
    }
    if (within(logical_x, logical_y, box_rename_cancel_button_rect_virt_)) {
        closeBoxRenameModal(false);
        ui_state_.requestButtonSfx();
        return true;
    }
    if (within(logical_x, logical_y, box_rename_text_field_rect_virt_)) {
        box_rename_focus_slot_ = BoxRenameFocusSlot::Field;
        if (!box_rename_editing_) {
            box_rename_editing_ = true;
            SDL_StartTextInput();
            SDL_SetTextInputRect(&box_rename_text_field_rect_virt_);
        }
        return true;
    }
    return true;
}

} // namespace pr

