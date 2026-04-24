#pragma once

#include "core/Types.hpp"

namespace pr::transfer_system {

class GameBoxBrowserController {
public:
    void enter(int box_count, int initial_game_box_index);

    int gameBoxIndex() const { return game_box_index_; }
    bool gameBoxSpaceMode() const { return game_box_space_mode_; }
    int gameBoxSpaceRowOffset() const { return game_box_space_row_offset_; }
    bool dropdownOpenTarget() const { return game_box_dropdown_open_target_; }
    double dropdownExpandT() const { return game_box_dropdown_expand_t_; }
    int dropdownHighlightIndex() const { return dropdown_highlight_index_; }
    double dropdownScrollPx() const { return dropdown_scroll_px_; }
    int dropdownRowHeightPx() const { return dropdown_row_height_px_; }

    void setDropdownRowHeightPx(int row_height_px);

    int gameBoxSpaceMaxRowOffset(int box_count) const;
    bool setGameBoxSpaceMode(bool enabled, int box_count);
    bool stepGameBoxSpaceRowDown(int box_count);
    bool stepGameBoxSpaceRowUp();
    bool advanceGameBox(int dir, int box_count, bool panels_ready);
    bool jumpGameBoxToIndex(int target_index, int box_count, bool panels_ready);

    void updateDropdown(double dt, const GameTransferBoxNameDropdownStyle& style);
    void scrollDropdownBy(double delta_px, int box_count, int inner_draw_h);
    void clampDropdownScroll(int box_count, int inner_draw_h);
    void syncDropdownScrollToHighlight(int box_count, int inner_draw_h);
    bool stepDropdownHighlight(int delta, int box_count, int inner_draw_h);
    bool toggleGameBoxDropdown(bool enabled, bool game_box_space_mode, int box_count, int inner_draw_h);
    void closeGameBoxDropdown();
    bool applyDropdownSelection(int box_count, bool panels_ready);

private:
    static void approachExponential(double& value, double target, double dt, double lambda);

    int game_box_index_ = 0;
    bool game_box_space_mode_ = false;
    int game_box_space_row_offset_ = 0;
    bool game_box_dropdown_open_target_ = false;
    double game_box_dropdown_expand_t_ = 0.0;
    int dropdown_highlight_index_ = 0;
    double dropdown_scroll_px_ = 0.0;
    int dropdown_row_height_px_ = 24;
};

} // namespace pr::transfer_system
