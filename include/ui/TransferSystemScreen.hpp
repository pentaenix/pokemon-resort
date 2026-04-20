#pragma once

#include "core/Assets.hpp"
#include "core/Types.hpp"
#include "core/Font.hpp"
#include "ui/BoxViewport.hpp"
#include "ui/FocusManager.hpp"
#include "ui/ScreenInput.hpp"
#include "ui/TransferSaveSelection.hpp"

#include <SDL.h>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace pr {

class TransferSystemScreen : public ScreenInput {
public:
    TransferSystemScreen(
        SDL_Renderer* renderer,
        const WindowConfig& window_config,
        const std::string& font_path,
        const std::string& project_root);

    void enter(const TransferSaveSelection& selection, SDL_Renderer* renderer, int initial_game_box_index);
    void update(double dt);
    void render(SDL_Renderer* renderer);

    int currentGameBoxIndex() const { return game_box_index_; }
    const std::string& currentGameKey() const { return current_game_key_; }

    void onAdvancePressed() override;
    void onBackPressed() override;
    bool handlePointerPressed(int logical_x, int logical_y) override;
    void handlePointerMoved(int logical_x, int logical_y) override;
    bool handlePointerReleased(int logical_x, int logical_y) override;

    bool consumeButtonSfxRequest();
    bool consumeReturnToTicketListRequest();

    bool canNavigate2d() const override { return true; }
    void onNavigate2d(int dx, int dy) override;

private:
    void requestReturnToTicketList();
    void drawBackground(SDL_Renderer* renderer) const;
    void updateAnimations(double dt);
    void updateEnterExit(double dt);
    void updateCarouselSlide(double dt);
    void drawPillToggle(SDL_Renderer* renderer) const;
    bool hitTestPillTrack(int logical_x, int logical_y) const;
    void togglePillTarget();
    void cachePillLabelTextures(SDL_Renderer* renderer);
    void syncBoxViewportPositions();
    /// Changes PC box for the external save panel (`dir` −1 / +1). No-op if UI not ready or no boxes.
    void advanceGameBox(int dir);
    void drawToolCarousel(SDL_Renderer* renderer) const;
    void drawBottomBanner(SDL_Renderer* renderer) const;
    void drawGameBoxNameDropdownChrome(SDL_Renderer* renderer) const;
    void drawGameBoxNameDropdownList(SDL_Renderer* renderer) const;
    void drawSelectionCursor(SDL_Renderer* renderer) const;
    /// Which focus node (if any) contains this point — keeps keyboard focus aligned with mouse targets.
    std::optional<FocusNodeId> focusNodeAtPointer(int logical_x, int logical_y) const;
    bool hitTestToolCarousel(int logical_x, int logical_y) const;
    /// Cycles selection: `dir` −1 = previous tool, +1 = next (infinite wrap).
    void cycleToolCarousel(int dir);
    bool carouselSlideAnimating() const;
    Color carouselFrameColorForIndex(int tool_index) const;
    int carouselScreenY() const;

    struct BackgroundAnimation {
        bool enabled = false;
        double scale = 1.0;
        double speed_x = 0.0;
        double speed_y = 0.0;
    };

    WindowConfig window_config_;
    std::string project_root_;
    std::string font_path_;
    TextureHandle background_;
    BackgroundAnimation background_animation_;
    std::unique_ptr<BoxViewport> resort_box_viewport_;
    std::unique_ptr<BoxViewport> game_save_box_viewport_;
    GameTransferPillToggleStyle pill_style_;
    GameTransferToolCarouselStyle carousel_style_;
    GameTransferBoxNameDropdownStyle box_name_dropdown_style_;
    GameTransferSelectionCursorStyle selection_cursor_style_;
    std::array<TextureHandle, 4> tool_icons_{};
    /// 0 = multiple, 1 = basic, 2 = swap, 3 = items.
    int selected_tool_index_ = 1;
    FontHandle pill_font_;
    FontHandle dropdown_item_font_;
    TextureHandle pill_label_pokemon_black_;
    TextureHandle pill_label_items_black_;
    TextureHandle pill_label_pokemon_white_;
    TextureHandle pill_label_items_white_;

    double elapsed_seconds_ = 0.0;
    double fade_in_seconds_ = 0.0;
    double fade_out_seconds_ = 0.12;
    double exit_fade_seconds_ = 0.0;
    bool play_button_sfx_requested_ = false;
    bool return_to_ticket_list_requested_ = false;

    /// 0 = Pokémon (left), 1 = Items (right).
    double slider_t_ = 0.0;
    double slider_target_ = 0.0;
    /// 0 = boxes off-screen, 1 = boxes at rest.
    double panels_reveal_ = 0.0;
    double panels_target_ = 1.0;
    /// Horizontal slide of tool strip (px); commits `selected_tool_index_` when motion finishes.
    double carousel_slide_offset_x_ = 0.0;
    double carousel_slide_target_x_ = 0.0;
    /// 0 = UI off-screen, 1 = at rest (first-enter animation, and used on exit).
    double ui_enter_ = 0.0;
    double ui_enter_target_ = 1.0;
    /// 0 = bottom banner hidden below screen, 1 = at rest.
    double bottom_banner_reveal_ = 0.0;
    double bottom_banner_target_ = 1.0;
    bool exit_in_progress_ = false;

    // --- Game save box data + navigation (right box only) ---
    std::vector<TransferSaveSelection::PcBox> game_pc_boxes_{};
    int game_box_index_ = 0;
    int pending_game_box_index_ = -1;
    bool game_box_was_sliding_ = false;
    std::unordered_map<std::string, TextureHandle> sprite_cache_{};
    std::string current_game_key_{};

    FocusManager focus_;
    /// After a mouse hit on a focusable control, hide the controller selection ring until keyboard/gamepad input.
    bool selection_cursor_hidden_after_mouse_ = false;

    bool game_box_dropdown_open_target_ = false;
    double game_box_dropdown_expand_t_ = 0.0;
    int dropdown_highlight_index_ = 0;
    double dropdown_scroll_px_ = 0.0;
    int dropdown_row_height_px_ = 24;
    /// Mouse: distinguish click (pick box) vs drag-to-scroll inside the dropdown list.
    bool dropdown_lmb_down_in_panel_ = false;
    int dropdown_lmb_last_y_ = 0;
    double dropdown_lmb_drag_accum_ = 0.0;
    mutable bool dropdown_labels_dirty_ = false;
    mutable std::vector<TextureHandle> dropdown_item_textures_{};

    void updateGameBoxDropdown(double dt);
    void rebuildDropdownItemTextures(SDL_Renderer* renderer);
    bool hitTestGameBoxNamePlate(int logical_x, int logical_y) const;
    /// Outer chrome grows from the pill vertical center; `out_list_clip_top_y` / `out_list_inner_h` describe the list area only (below the pill).
    bool computeGameBoxDropdownOuterRect(
        SDL_Rect& out_outer,
        float expand_scale,
        int& out_list_inner_h,
        int& out_list_clip_top_y) const;
    std::optional<int> dropdownRowIndexAtScreen(int logical_x, int logical_y) const;
    void clampDropdownScroll(int inner_h);
    void syncDropdownScrollToHighlight(int inner_h);
    void toggleGameBoxDropdown();
    void closeGameBoxDropdown();
    void stepDropdownHighlight(int delta);
    void applyGameBoxDropdownSelection();
    BoxViewportModel gameBoxViewportModelAt(int box_index) const;
    void jumpGameBoxToIndex(int target_index);
};

} // namespace pr
