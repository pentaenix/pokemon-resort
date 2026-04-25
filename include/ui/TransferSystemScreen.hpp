#pragma once

#include "core/Assets.hpp"
#include "core/Types.hpp"
#include "core/Font.hpp"
#include "core/PokeSpriteAssets.hpp"
#include "ui/BoxViewport.hpp"
#include "ui/FocusManager.hpp"
#include "ui/Screen.hpp"
#include "ui/TransferSaveSelection.hpp"
#include "ui/transfer_system/GameBoxBrowserController.hpp"
#include "ui/transfer_system/TransferInfoBannerPresenter.hpp"
#include "ui/transfer_system/TransferSystemUiStateController.hpp"

#include <SDL.h>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

namespace pr {

class TransferSystemScreen : public Screen {
public:
    TransferSystemScreen(
        SDL_Renderer* renderer,
        const WindowConfig& window_config,
        const std::string& font_path,
        const std::string& project_root,
        std::shared_ptr<PokeSpriteAssets> sprite_assets);

    void enter(const TransferSaveSelection& selection, SDL_Renderer* renderer, int initial_game_box_index);
    void update(double dt) override;
    void render(SDL_Renderer* renderer) override;

    int currentGameBoxIndex() const { return game_box_browser_.gameBoxIndex(); }
    const std::string& currentGameKey() const { return current_game_key_; }

    void onAdvancePressed() override;
    void onBackPressed() override;
    bool handlePointerPressed(int logical_x, int logical_y) override;
    void handlePointerMoved(int logical_x, int logical_y) override;
    bool handlePointerReleased(int logical_x, int logical_y) override;

    bool consumeButtonSfxRequest();
    /// Lighter tick when 2D focus changes (Transfer system only; uses `app.json` → `ui_move_sfx`).
    bool consumeUiMoveSfxRequest();
    bool consumeReturnToTicketListRequest();

    bool canNavigate2d() const override { return true; }
    void onNavigate2d(int dx, int dy) override;

#ifdef PR_ENABLE_TEST_HOOKS
    bool debugGameBoxSpaceMode() const { return game_box_browser_.gameBoxSpaceMode(); }
    int debugGameBoxSpaceRowOffset() const { return game_box_browser_.gameBoxSpaceRowOffset(); }
    FocusNodeId debugFocusedNode() const { return focus_.current(); }
    bool debugShouldDrawSpeechBubble() const { return currentFocusWantsSpeechBubble(); }
    bool debugGameBoxContentSliding() const {
        return game_save_box_viewport_ && game_save_box_viewport_->isContentSliding();
    }
    bool debugGameBoxTitleTextureReady() const {
        return game_save_box_viewport_ && game_save_box_viewport_->debugTitleTextureReady();
    }
    std::string debugGameBoxCachedTitleText() const {
        return game_save_box_viewport_ ? game_save_box_viewport_->debugCachedTitleText() : std::string{};
    }
    std::optional<SDL_Rect> debugMiniPreviewFirstSpriteRect() const { return debug_mini_preview_first_sprite_rect_; }
    SDL_Point debugMiniPreviewCellSize() const { return debug_mini_preview_cell_size_; }
    bool debugMiniPreviewVisible() const { return mini_preview_t_ > 1e-3 && mini_preview_box_index_ >= 0; }
    bool debugInfoBannerVisible() const { return info_banner_style_.enabled; }
    std::optional<SDL_Rect> debugPillTrackBounds() const;
    std::string debugInfoBannerPokemonName() const {
        if (const PcSlotSpecies* slot = activeInfoBannerPokemon()) {
            return !slot->nickname.empty() ? slot->nickname : slot->species_name;
        }
        return {};
    }
    std::string debugInfoBannerMode() const;
    std::optional<SDL_Rect> debugGameSlotBounds(int slot_index) const {
        if (!game_save_box_viewport_) {
            return std::nullopt;
        }
        SDL_Rect out{};
        return game_save_box_viewport_->getSlotBounds(slot_index, out) ? std::optional<SDL_Rect>(out) : std::nullopt;
    }
#endif

private:
    enum class MiniPreviewContext {
        Dropdown,
        MouseHover,
        BoxSpaceFocus,
    };

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
    void drawSpeechBubbleCursor(SDL_Renderer* renderer, const SDL_Rect& target, FocusNodeId focus_id) const;
    std::string speechBubbleLineForFocus(FocusNodeId focus_id) const;
    bool currentFocusWantsSpeechBubble() const;
    /// Which focus node (if any) contains this point — keeps keyboard focus aligned with mouse targets.
    std::optional<FocusNodeId> focusNodeAtPointer(int logical_x, int logical_y) const;
    /// Slots + footer game icons only (for hover callouts).
    std::optional<std::pair<FocusNodeId, SDL_Rect>> speechBubbleTargetAtPointer(int logical_x, int logical_y) const;
    bool gameSaveSlotHasSpecies(int slot_index) const;
    bool resortSlotHasSpecies(int slot_index) const;
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
    SDL_Renderer* renderer_ = nullptr;
    std::string project_root_;
    std::string font_path_;
    std::shared_ptr<PokeSpriteAssets> sprite_assets_;
    TextureHandle background_;
    BackgroundAnimation background_animation_;
    std::unique_ptr<BoxViewport> resort_box_viewport_;
    std::unique_ptr<BoxViewport> game_save_box_viewport_;
    GameTransferPillToggleStyle pill_style_;
    GameTransferToolCarouselStyle carousel_style_;
    GameTransferBoxNameDropdownStyle box_name_dropdown_style_;
    GameTransferSelectionCursorStyle selection_cursor_style_;
    GameTransferMiniPreviewStyle mini_preview_style_;
    GameTransferInfoBannerStyle info_banner_style_;
    std::array<TextureHandle, 4> tool_icons_{};
    FontHandle pill_font_;
    FontHandle dropdown_item_font_;
    FontHandle speech_bubble_font_;
    TextureHandle pill_label_pokemon_black_;
    TextureHandle pill_label_items_black_;
    TextureHandle pill_label_pokemon_white_;
    TextureHandle pill_label_items_white_;
    transfer_system::TransferSystemUiStateController ui_state_;

    // --- Mini preview (hover / dropdown) ---
    double mini_preview_t_ = 0.0;
    double mini_preview_target_ = 0.0;
    int mini_preview_box_index_ = -1;
    int mouse_hover_mini_preview_box_index_ = -1;
    BoxViewportModel mini_preview_model_{};
    void updateMiniPreview(double dt);
    void drawMiniPreview(SDL_Renderer* renderer) const;
#ifdef PR_ENABLE_TEST_HOOKS
    mutable std::optional<SDL_Rect> debug_mini_preview_first_sprite_rect_{};
    mutable SDL_Point debug_mini_preview_cell_size_{0, 0};
#endif

    // --- Game save box data + navigation (right box only) ---
    std::vector<TransferSaveSelection::PcBox> game_pc_boxes_{};
    transfer_system::GameBoxBrowserController game_box_browser_;
    std::string current_game_key_{};
    /// Display name for the external save footer icon callout (`selection.game_title`).
    std::string selection_game_title_;
    TransferSaveSelection transfer_selection_{};
    mutable std::unordered_map<int, FontHandle> info_banner_font_cache_{};
    mutable std::unordered_map<std::string, TextureHandle> info_banner_text_cache_{};
    mutable std::unordered_map<std::string, TextureHandle> info_banner_icon_cache_{};
    mutable TextureHandle speech_bubble_label_tex_{};
    mutable std::string speech_bubble_label_cache_{};

    // --- Box Space (game save panel only, for now) ---
    TextureHandle box_space_full_tex_{};
    TextureHandle box_space_empty_tex_{};
    TextureHandle box_space_noempty_tex_{};

    void setGameBoxSpaceMode(bool enabled);
    void stepGameBoxSpaceRowDown();
    void stepGameBoxSpaceRowUp();
    int gameBoxSpaceMaxRowOffset() const;
    BoxViewportModel gameBoxSpaceViewportModelAt(int row_offset) const;

    bool box_space_drag_active_ = false;
    int box_space_drag_last_y_ = 0;
    double box_space_drag_accum_ = 0.0;
    int box_space_pressed_cell_ = -1;

    FocusManager focus_;
    /// After a mouse hit on a focusable control, hide the controller selection ring until keyboard/gamepad input.
    bool selection_cursor_hidden_after_mouse_ = false;
    /// Mouse is over a Pokémon slot or game icon (speech bubble stays visible even if `selection_cursor_hidden_after_mouse_`).
    bool speech_hover_active_ = false;
    FocusNodeId mouse_hover_focus_node_ = -1;

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
    transfer_system::TransferInfoBannerContext activeInfoBannerContext() const;
    const PcSlotSpecies* activeInfoBannerPokemon() const;
    FontHandle infoBannerFont(int font_pt) const;
    TextureHandle infoBannerTextTexture(SDL_Renderer* renderer, int font_pt, const Color& color, const std::string& text) const;
    std::string infoBannerTextFitToReference(
        SDL_Renderer* renderer,
        int font_pt,
        const Color& color,
        const std::string& text,
        const std::string& reference_text) const;
    TextureHandle infoBannerIconTexture(SDL_Renderer* renderer, const std::string& icon_group, const std::string& icon_key) const;
    void jumpGameBoxToIndex(int target_index);
    bool panelsReadyForInteraction() const;
    bool dropdownAcceptsNavigation() const;
    std::optional<int> focusedGameSlotIndex() const;
    bool gameBoxHasPreviewContent(int box_index) const;
    bool shouldShowMiniPreviewForBox(int box_index, MiniPreviewContext context) const;
    bool openGameBoxFromBoxSpaceSelection(int box_index);
    bool activateFocusedGameSlot();
    bool handleGameBoxSpacePointerPressed(int logical_x, int logical_y);
    bool handleDropdownPointerPressed(int logical_x, int logical_y);
    bool handleGameBoxNavigationPointerPressed(int logical_x, int logical_y);
};

} // namespace pr
