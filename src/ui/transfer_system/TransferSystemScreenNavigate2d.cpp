#include "ui/TransferSystemScreen.hpp"

#include <algorithm>

namespace pr {

void TransferSystemScreen::onNavigate2d(int dx, int dy) {
    selection_cursor_hidden_after_mouse_ = false;
    if (box_rename_modal_open_ && !box_rename_editing_) {
        using S = BoxRenameFocusSlot;
        if (dx != 0 || dy != 0) {
            switch (box_rename_focus_slot_) {
                case S::Field:
                    if (dy > 0) {
                        box_rename_focus_slot_ = S::Cancel;
                        ui_state_.requestUiMoveSfx();
                    }
                    break;
                case S::Cancel:
                    if (dy < 0) {
                        box_rename_focus_slot_ = S::Field;
                        ui_state_.requestUiMoveSfx();
                    } else if (dx > 0) {
                        box_rename_focus_slot_ = S::Confirm;
                        ui_state_.requestUiMoveSfx();
                    }
                    break;
                case S::Confirm:
                    if (dy < 0) {
                        box_rename_focus_slot_ = S::Field;
                        ui_state_.requestUiMoveSfx();
                    } else if (dx < 0) {
                        box_rename_focus_slot_ = S::Cancel;
                        ui_state_.requestUiMoveSfx();
                    }
                    break;
            }
        }
        return;
    }
    if (keyboard_multi_marquee_active_ &&
        ((keyboard_multi_marquee_from_game_ && game_box_browser_.gameBoxSpaceMode()) ||
         (!keyboard_multi_marquee_from_game_ && resort_box_browser_.gameBoxSpaceMode()))) {
        keyboard_multi_marquee_active_ = false;
    }
    if (keyboard_multi_marquee_active_) {
        int row = keyboard_multi_marquee_corner_slot_ / 6;
        int col = keyboard_multi_marquee_corner_slot_ % 6;
        col += dx;
        row += dy;
        row = std::clamp(row, 0, 4);
        col = std::clamp(col, 0, 5);
        keyboard_multi_marquee_corner_slot_ = row * 6 + col;
        focus_.setCurrent((keyboard_multi_marquee_from_game_ ? 2000 : 1000) + keyboard_multi_marquee_corner_slot_);
        ui_state_.requestUiMoveSfx();
        return;
    }
    if (pokemon_action_menu_.visible()) {
        if (dy != 0) {
            pokemon_action_menu_.stepSelection(dy > 0 ? 1 : -1);
            ui_state_.requestUiMoveSfx();
        }
        (void)dx;
        return;
    }
    if (item_action_menu_.visible()) {
        if (dy != 0) {
            item_action_menu_.stepSelection(dy > 0 ? 1 : -1);
            ui_state_.requestUiMoveSfx();
        }
        (void)dx;
        return;
    }
    if (dropdownAcceptsNavigation()) {
        if (dy < 0) {
            stepDropdownHighlight(-1);
        } else if (dy > 0) {
            stepDropdownHighlight(1);
        }
        (void)dx;
        return;
    }

    // Box Space mode: allow vertical navigation to scroll through all rows before leaving the grid.
    if (game_box_browser_.gameBoxSpaceMode() && dy != 0) {
        const FocusNodeId cur = focus_.current();
        if (cur >= 2000 && cur <= 2029) {
            const int slot = cur - 2000;
            const int max_row =
                game_box_browser_.gameBoxSpaceMaxRowOffset(static_cast<int>(game_pc_boxes_.size()));
            if (dy > 0) {
                // At bottom row of the 6×5 grid.
                if (slot >= 24) {
                    if (game_box_browser_.gameBoxSpaceRowOffset() < max_row) {
                        stepGameBoxSpaceRowDown();
                        return;
                    }
                    // At end: only now wrap to the footer Box Space button.
                    focus_.setCurrent(2110);
                    ui_state_.requestUiMoveSfx();
                    return;
                }
            } else if (dy < 0) {
                // At top row of the grid.
                if (slot < 6 && game_box_browser_.gameBoxSpaceRowOffset() > 0) {
                    stepGameBoxSpaceRowUp();
                    return;
                }
            }
        }
    }
    if (resort_box_browser_.gameBoxSpaceMode() && dy != 0) {
        const FocusNodeId cur = focus_.current();
        if (cur >= 1000 && cur <= 1029) {
            const int slot = cur - 1000;
            const int max_row =
                resort_box_browser_.gameBoxSpaceMaxRowOffset(static_cast<int>(resort_pc_boxes_.size()));
            if (dy > 0) {
                if (slot >= 24) {
                    if (resort_box_browser_.gameBoxSpaceRowOffset() < max_row) {
                        stepResortBoxSpaceRowDown();
                        return;
                    }
                    focus_.setCurrent(1110);
                    ui_state_.requestUiMoveSfx();
                    return;
                }
            } else if (dy < 0) {
                if (slot < 6 && resort_box_browser_.gameBoxSpaceRowOffset() > 0) {
                    stepResortBoxSpaceRowUp();
                    return;
                }
            }
        }
    }

    const FocusNodeId focus_before = focus_.current();
    focus_.navigate(dx, dy, window_config_.virtual_width, window_config_.virtual_height);
    if (focus_.current() != focus_before) {
        ui_state_.requestUiMoveSfx();
    }
}

} // namespace pr

