#include "ui/transfer_system/GameBoxBrowserController.hpp"

#include <algorithm>
#include <cmath>

namespace pr::transfer_system {

void GameBoxBrowserController::enter(int box_count, int initial_game_box_index) {
    game_box_dropdown_open_target_ = false;
    game_box_dropdown_expand_t_ = 0.0;
    dropdown_scroll_px_ = 0.0;
    game_box_space_mode_ = false;
    game_box_space_row_offset_ = 0;
    game_box_index_ = 0;
    dropdown_highlight_index_ = 0;

    if (box_count > 0) {
        game_box_index_ = ((initial_game_box_index % box_count) + box_count) % box_count;
        dropdown_highlight_index_ =
            box_count >= 2 ? game_box_index_ + 1 : game_box_index_;
    }
}

void GameBoxBrowserController::setDropdownRowHeightPx(int row_height_px) {
    dropdown_row_height_px_ = std::max(1, row_height_px);
}

int GameBoxBrowserController::gameBoxSpaceMaxRowOffset(int box_count) const {
    if (box_count <= 30) {
        return 0;
    }
    const int extra = box_count - 30;
    return (extra + 5) / 6;
}

bool GameBoxBrowserController::setGameBoxSpaceMode(bool enabled, int box_count) {
    const bool next_enabled = enabled && box_count > 0;
    const bool changed = game_box_space_mode_ != next_enabled;
    game_box_space_mode_ = next_enabled;
    game_box_space_row_offset_ = std::clamp(game_box_space_row_offset_, 0, gameBoxSpaceMaxRowOffset(box_count));
    return changed;
}

bool GameBoxBrowserController::stepGameBoxSpaceRowDown(int box_count) {
    if (!game_box_space_mode_) {
        return false;
    }
    const int max_row = gameBoxSpaceMaxRowOffset(box_count);
    if (max_row <= 0) {
        return false;
    }
    // At the last row, wrap back to the first row (same as wrapping the carousel).
    if (game_box_space_row_offset_ >= max_row) {
        game_box_space_row_offset_ = 0;
        return true;
    }
    ++game_box_space_row_offset_;
    return true;
}

bool GameBoxBrowserController::stepGameBoxSpaceRowUp() {
    if (!game_box_space_mode_ || game_box_space_row_offset_ <= 0) {
        return false;
    }
    --game_box_space_row_offset_;
    return true;
}

bool GameBoxBrowserController::advanceGameBox(int dir, int box_count, bool panels_ready) {
    if (dir == 0 || box_count <= 0 || game_box_space_mode_ || !panels_ready) {
        return false;
    }
    game_box_index_ = ((game_box_index_ + dir) % box_count + box_count) % box_count;
    return true;
}

bool GameBoxBrowserController::jumpGameBoxToIndex(int target_index, int box_count, bool panels_ready) {
    if (box_count <= 0 || game_box_space_mode_ || !panels_ready) {
        return false;
    }
    target_index = (target_index % box_count + box_count) % box_count;
    if (target_index == game_box_index_) {
        closeGameBoxDropdown();
        return false;
    }
    game_box_index_ = target_index;
    closeGameBoxDropdown();
    return true;
}

void GameBoxBrowserController::updateDropdown(double dt, const GameTransferBoxNameDropdownStyle& style) {
    if (!style.enabled) {
        return;
    }
    const double target = game_box_dropdown_open_target_ ? 1.0 : 0.0;
    const double lambda = game_box_dropdown_open_target_ ? style.open_smoothing : style.close_smoothing;
    approachExponential(game_box_dropdown_expand_t_, target, dt, std::max(1.0, lambda));
}

void GameBoxBrowserController::scrollDropdownBy(double delta_px, int box_count, int inner_draw_h) {
    dropdown_scroll_px_ += delta_px;
    clampDropdownScroll(box_count, inner_draw_h);
}

void GameBoxBrowserController::clampDropdownScroll(int box_count, int inner_draw_h) {
    if (box_count <= 0 || dropdown_row_height_px_ <= 0) {
        dropdown_scroll_px_ = 0.0;
        return;
    }
    const int rows = dropdownListRowCount(box_count);
    const int content_h = rows * dropdown_row_height_px_;
    const int max_scroll = std::max(0, content_h - inner_draw_h);
    dropdown_scroll_px_ = std::clamp(dropdown_scroll_px_, 0.0, static_cast<double>(max_scroll));
}

void GameBoxBrowserController::syncDropdownScrollToHighlight(int box_count, int inner_draw_h) {
    const int rh = std::max(1, dropdown_row_height_px_);
    const int rows = dropdownListRowCount(box_count);
    const int clamped_highlight = std::clamp(dropdown_highlight_index_, 0, std::max(0, rows - 1));
    const int top = clamped_highlight * rh;
    const int bottom = top + rh;
    if (top < static_cast<int>(dropdown_scroll_px_)) {
        dropdown_scroll_px_ = static_cast<double>(top);
    }
    if (bottom > static_cast<int>(dropdown_scroll_px_) + inner_draw_h) {
        dropdown_scroll_px_ = static_cast<double>(bottom - inner_draw_h);
    }
    clampDropdownScroll(box_count, inner_draw_h);
}

bool GameBoxBrowserController::stepDropdownHighlight(int delta, int box_count, int inner_draw_h) {
    if (box_count <= 0 || delta == 0) {
        return false;
    }
    const int row_count = dropdownListRowCount(box_count);
    if (row_count <= 0) {
        return false;
    }
    dropdown_highlight_index_ = ((dropdown_highlight_index_ + delta) % row_count + row_count) % row_count;
    syncDropdownScrollToHighlight(box_count, inner_draw_h);
    return true;
}

bool GameBoxBrowserController::toggleGameBoxDropdown(
    bool enabled,
    bool game_box_space_mode,
    int box_count,
    int inner_draw_h) {
    if (!enabled || box_count < 2 || game_box_space_mode) {
        return false;
    }
    if (game_box_dropdown_open_target_) {
        closeGameBoxDropdown();
        return true;
    }
    game_box_dropdown_open_target_ = true;
    dropdown_highlight_index_ = game_box_index_ + 1;
    syncDropdownScrollToHighlight(box_count, inner_draw_h);
    return true;
}

void GameBoxBrowserController::closeGameBoxDropdown() {
    game_box_dropdown_open_target_ = false;
}

bool GameBoxBrowserController::applyDropdownSelection(int box_count, bool panels_ready) {
    if (dropdown_highlight_index_ <= 0) {
        return false;
    }
    return jumpGameBoxToIndex(dropdown_highlight_index_ - 1, box_count, panels_ready);
}

void GameBoxBrowserController::approachExponential(double& value, double target, double dt, double lambda) {
    if (lambda <= 1e-9) {
        value = target;
        return;
    }
    const double alpha = 1.0 - std::exp(-lambda * dt);
    value += (target - value) * alpha;
    if (std::fabs(target - value) < 0.0005) {
        value = target;
    }
}

} // namespace pr::transfer_system
