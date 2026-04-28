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
#include "ui/transfer_system/MultiPokemonMoveController.hpp"
#include "ui/transfer_system/PokemonActionMenuController.hpp"
#include "ui/transfer_system/PokemonMoveController.hpp"
#include "ui/transfer_system/ItemActionMenuController.hpp"
#include "ui/transfer_system/move/Gestures.hpp"
#include "ui/transfer_system/move/HeldMoveController.hpp"
#include "ui/transfer_system/TransferSaveConfig.hpp"
#include "ui/transfer_system/TransferInfoBannerPresenter.hpp"
#include "ui/transfer_system/TransferSystemUiStateController.hpp"

#include <SDL.h>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

namespace pr::resort {
class PokemonResortService;
struct PokemonSlotView;
}

namespace pr {

class TransferSystemScreen : public Screen {
public:
    TransferSystemScreen(
        SDL_Renderer* renderer,
        const WindowConfig& window_config,
        const std::string& font_path,
        const std::string& project_root,
        std::shared_ptr<PokeSpriteAssets> sprite_assets,
        std::string save_directory,
        const char* bridge_argv0,
        resort::PokemonResortService* resort_service = nullptr);

    void enter(const TransferSaveSelection& selection, SDL_Renderer* renderer, int initial_game_box_index);
    void update(double dt) override;
    void render(SDL_Renderer* renderer) override;

    int currentGameBoxIndex() const { return game_box_browser_.gameBoxIndex(); }
    const std::string& currentGameKey() const { return current_game_key_; }

    void onAdvancePressed() override;
    void onBackPressed() override;
    bool captureAdvanceForLongPress() const override;
    std::optional<double> advanceLongPressSeconds() const override;
    void onAdvanceLongPress() override;
    void onAdvanceLongPressCharge(double elapsed_seconds) override;
    void onAdvanceLongPressEnded(bool long_press_action_fired) override;
    bool captureNavigate2dForLongPress(int dx, int dy) const override;
    std::optional<double> navigate2dLongPressSeconds(int dx, int dy) const override;
    void onNavigate2dLongPress(int dx, int dy) override;
    void onNavigationLongPressCharge(double elapsed_seconds, int dx, int dy) override;
    void onNavigationLongPressEnded(bool long_press_action_fired) override;
    bool handlePointerPressed(int logical_x, int logical_y) override;
    void handlePointerMoved(int logical_x, int logical_y) override;
    bool handlePointerReleased(int logical_x, int logical_y) override;
    bool handleUnroutedSdlEvent(const SDL_Event& event) override;
    bool capturesUnroutedKeyboardFocus() const override;

    bool consumeButtonSfxRequest();
    /// Lighter tick when 2D focus changes (Transfer system only; uses `app.json` → `ui_move_sfx`).
    bool consumeUiMoveSfxRequest();
    bool consumePickupSfxRequest();
    bool consumePutdownSfxRequest();
    bool consumeErrorSfxRequest();
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
    std::string debugSpeechBubbleLineForFocus(FocusNodeId focus_id) const { return speechBubbleLineForFocus(focus_id); }
    std::string debugInfoBannerPokemonName() const {
        if (const PcSlotSpecies* slot = activeInfoBannerPokemon()) {
            return !slot->nickname.empty() ? slot->nickname : slot->species_name;
        }
        return {};
    }
    std::string debugInfoBannerMode() const;
    bool debugPokemonActionMenuVisible() const { return pokemon_action_menu_.visible() && !pokemon_action_menu_.closing(); }
    bool debugPokemonActionMenuFromGameBox() const { return pokemon_action_menu_.fromGameBox(); }
    int debugPokemonActionMenuSelectedRow() const { return pokemon_action_menu_.selectedRow(); }
    bool debugPokemonMoveActive() const { return pokemon_move_.active(); }
    bool debugMultiPokemonMoveActive() const { return multi_pokemon_move_.active(); }
    int debugHeldMultiPokemonCount() const { return multi_pokemon_move_.count(); }
    std::string debugHeldPokemonName() const {
        if (const auto* held = pokemon_move_.held()) {
            return !held->pokemon.nickname.empty() ? held->pokemon.nickname : held->pokemon.species_name;
        }
        return {};
    }
    std::string debugHeldItemName() const {
        if (const auto* held = held_move_.heldItem()) {
            return held->item_name;
        }
        return {};
    }
    std::string debugGameSlotPokemonName(int slot_index) const {
        if (const PcSlotSpecies* slot = pokemonAt(transfer_system::PokemonMoveController::SlotRef{
                transfer_system::PokemonMoveController::Panel::Game,
                game_box_browser_.gameBoxIndex(),
                slot_index})) {
            return slot->occupied() ? (!slot->nickname.empty() ? slot->nickname : slot->species_name) : std::string{};
        }
        return {};
    }
    std::string debugResortSlotPokemonName(int slot_index) const {
        if (const PcSlotSpecies* slot = pokemonAt(transfer_system::PokemonMoveController::SlotRef{
                transfer_system::PokemonMoveController::Panel::Resort,
                0,
                slot_index})) {
            return slot->occupied() ? (!slot->nickname.empty() ? slot->nickname : slot->species_name) : std::string{};
        }
        return {};
    }
    std::optional<SDL_Rect> debugPokemonActionMenuRect() const {
        return pokemon_action_menu_.visible() ? std::optional<SDL_Rect>(pokemonActionMenuFinalRect()) : std::nullopt;
    }
    std::optional<SDL_Rect> debugGameSlotBounds(int slot_index) const {
        if (!game_save_box_viewport_) {
            return std::nullopt;
        }
        SDL_Rect out{};
        return game_save_box_viewport_->getSlotBounds(slot_index, out) ? std::optional<SDL_Rect>(out) : std::nullopt;
    }
    std::optional<SDL_Rect> debugResortSlotBounds(int slot_index) const {
        if (!resort_box_viewport_) {
            return std::nullopt;
        }
        SDL_Rect out{};
        return resort_box_viewport_->getSlotBounds(slot_index, out) ? std::optional<SDL_Rect>(out) : std::nullopt;
    }
    std::optional<SDL_Rect> debugGameNamePlateBounds() const {
        if (!game_save_box_viewport_) {
            return std::nullopt;
        }
        SDL_Rect out{};
        return game_save_box_viewport_->getNamePlateBounds(out) ? std::optional<SDL_Rect>(out) : std::nullopt;
    }
    std::optional<int> debugDropdownRowAtScreen(int logical_x, int logical_y) const;
    std::optional<SDL_Rect> debugMultiSelectionRect() const { return multi_select_drag_active_ ? std::optional<SDL_Rect>(multi_select_drag_rect_) : std::nullopt; }
#endif

private:
    enum class MiniPreviewContext {
        Dropdown,
        MouseHover,
        BoxSpaceFocus,
    };

    void requestReturnToTicketList();

    // --- Exit/save modal (external game save edits) ---
    bool exit_save_modal_open_ = false;
    bool exit_save_modal_target_open_ = false;
    int exit_save_modal_selected_row_ = 0;
    double exit_save_modal_reveal_ = 0.0; // 0..1 (animated)
    SDL_Rect exit_save_modal_card_rect_virt_{0, 0, 0, 0};
    std::array<SDL_Rect, 3> exit_save_modal_row_rects_virt_{};
    void openExitSaveModal();
    void closeExitSaveModal();
    void updateExitSaveModal(double dt);
    void syncExitSaveModalLayout();
    void stepExitSaveModalSelection(int delta);
    std::optional<int> exitSaveModalRowAtPoint(int logical_x, int logical_y) const;
    void activateExitSaveModalRow(int row);
    bool handleExitSaveModalPointerPressed(int logical_x, int logical_y);

    // --- External game save edits (prototype overlay persistence) ---
    bool game_boxes_dirty_ = false;
    void markGameBoxesDirty();
    bool saveGameBoxEditsOverlayAndClearDirty();
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
    void drawPokemonActionMenu(SDL_Renderer* renderer) const;
    void drawItemActionMenu(SDL_Renderer* renderer) const;
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
    bool itemToolActive() const;
    bool normalPokemonToolActive() const;
    bool multiPokemonToolActive() const;
    bool gameSlotHasHeldItem(int slot_index) const;
    bool resortSlotHasHeldItem(int slot_index) const;
    std::string gameSlotHeldItemName(int slot_index) const;
    Color carouselFrameColorForIndex(int tool_index) const;
    int carouselScreenY() const;
    int exitButtonScreenY() const;

    struct BackgroundAnimation {
        bool enabled = false;
        double scale = 1.0;
        double speed_x = 0.0;
        double speed_y = 0.0;
    };

    WindowConfig window_config_;
    SDL_Renderer* renderer_ = nullptr;
    std::string project_root_;
    std::string save_directory_;
    /// Passed through for PKHeX bridge resolution (`SaveBridgeClient`); may be null (development fallback still works).
    const char* bridge_argv0_ = nullptr;
    std::string font_path_;
    std::shared_ptr<PokeSpriteAssets> sprite_assets_;
    TextureHandle background_;
    BackgroundAnimation background_animation_;
    std::unique_ptr<BoxViewport> resort_box_viewport_;
    std::unique_ptr<BoxViewport> game_save_box_viewport_;
    GameTransferPillToggleStyle pill_style_;
    GameTransferToolCarouselStyle carousel_style_;
    bool exit_button_enabled_ = true;
    int exit_button_gap_pixels_ = 0;
    double exit_button_icon_scale_ = 1.0;
    Color exit_button_icon_mod_color_{255, 255, 255, 255};
    GameTransferBoxNameDropdownStyle box_name_dropdown_style_;
    GameTransferSelectionCursorStyle selection_cursor_style_;
    GameTransferMiniPreviewStyle mini_preview_style_;
    GameTransferBoxViewportStyle box_viewport_style_;
    GameTransferPokemonActionMenuStyle pokemon_action_menu_style_;
    GameTransferBoxSpaceLongPressStyle box_space_long_press_style_;
    GameTransferInfoBannerStyle info_banner_style_;
    transfer_system::ExitSaveModalStyle exit_save_modal_style_;
    std::array<TextureHandle, 4> tool_icons_{};
    TextureHandle exit_button_icon_{};
    FontHandle pill_font_;
    FontHandle dropdown_item_font_;
    FontHandle box_rename_modal_body_font_;
    FontHandle speech_bubble_font_;
    FontHandle pokemon_action_menu_font_;
    FontHandle exit_save_modal_font_;
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
    bool mini_preview_model_from_resort_ = false;
    BoxViewportModel mini_preview_model_{};
    void updateMiniPreview(double dt);
    void syncBoxSpaceMiniPreviewHoverFromPointer(int logical_x, int logical_y);
    void syncBoxSpaceMiniPreviewFromFocusedBoxSpaceCell();
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
    int resort_pc_box_count_ = 60;
    std::vector<TransferSaveSelection::PcBox> resort_pc_boxes_{};
    resort::PokemonResortService* resort_service_{nullptr};
    transfer_system::GameBoxBrowserController resort_box_browser_{};
    mutable std::vector<TextureHandle> resort_dropdown_item_textures_{};
    mutable bool resort_dropdown_labels_dirty_ = false;
    enum class BoxSpaceInteractionPanel : std::uint8_t {
        None,
        Game,
        Resort,
    };
    BoxSpaceInteractionPanel box_space_interaction_panel_{BoxSpaceInteractionPanel::None};
    mutable std::unordered_map<int, FontHandle> info_banner_font_cache_{};
    mutable std::unordered_map<std::string, TextureHandle> info_banner_text_cache_{};
    mutable std::unordered_map<std::string, TextureHandle> info_banner_icon_cache_{};
    mutable TextureHandle speech_bubble_label_tex_{};
    mutable std::string speech_bubble_label_cache_{};

    // --- Box Space (shared gesture state; panel tracked via BoxSpaceInteractionPanel) ---
    TextureHandle box_space_full_tex_{};
    TextureHandle box_space_empty_tex_{};
    TextureHandle box_space_noempty_tex_{};

    void setGameBoxSpaceMode(bool enabled);
    void stepGameBoxSpaceRowDown();
    void stepGameBoxSpaceRowUp();
    int gameBoxSpaceMaxRowOffset() const;
    BoxViewportModel gameBoxSpaceViewportModelAt(int row_offset) const;

    void advanceResortBox(int dir);
    void jumpResortBoxToIndex(int target_index);
    void setResortBoxSpaceMode(bool enabled);
    void stepResortBoxSpaceRowDown();
    void stepResortBoxSpaceRowUp();
    int resortBoxSpaceMaxRowOffset() const;
    BoxViewportModel resortBoxViewportModelAt(int box_index) const;
    BoxViewportModel resortBoxSpaceViewportModelAt(int row_offset) const;
    bool swapResortPcBoxes(int a, int b);

    bool gameDropdownNavigationActive() const;
    bool resortDropdownNavigationActive() const;
    void applyActiveDropdownSelection();
    void updateResortBoxDropdown(double dt);
    void rebuildResortDropdownItemTextures(SDL_Renderer* renderer);
    bool hitTestResortBoxNamePlate(int logical_x, int logical_y) const;
    bool computeResortBoxDropdownOuterRect(
        SDL_Rect& out_outer,
        float expand_scale,
        int& out_list_inner_h,
        int& out_list_clip_top_y) const;
    std::optional<int> resortDropdownRowIndexAtScreen(int logical_x, int logical_y) const;
    void clampResortDropdownScroll(int inner_draw_h);
    void syncResortDropdownScrollToHighlight(int inner_draw_h);
    void toggleResortBoxDropdown();
    void closeResortBoxDropdown();
    void stepResortDropdownHighlight(int delta);
    void applyResortBoxDropdownSelection();
    std::optional<int> focusedResortSlotIndex() const;
    /// Box index under focus while in Resort Box Space mode (left panel).
    std::optional<int> focusedResortBoxSpaceBoxIndex() const;
    bool activateFocusedResortSlot();
    bool openResortBoxFromBoxSpaceSelection(int box_index);
    bool resortBoxHasEmptySlots(int box_index, int required_count) const;
    bool dropHeldMultiPokemonIntoFirstEmptyResortBox(int box_index);
    void drawResortBoxNameDropdownChrome(SDL_Renderer* renderer) const;
    void drawResortBoxNameDropdownList(SDL_Renderer* renderer) const;

    bool box_space_drag_active_ = false;
    int box_space_drag_last_y_ = 0;
    double box_space_drag_accum_ = 0.0;
    int box_space_pressed_cell_ = -1;
    enum class BoxSpaceQuickDropKind : std::uint8_t {
        None,
        PokemonSingle,
        PokemonMulti,
        Item,
    };

    /// Which InputRouter long-press path is driving Box Space charge visuals (mutually exclusive).
    enum class KeyboardBoxSpaceChargeKind : std::uint8_t {
        None,
        NavigateQuickDrop,
        AdvanceQuickDrop,
        AdvanceBoxPickup,
    };

    bool box_space_quick_drop_pending_ = false;
    BoxSpaceQuickDropKind box_space_quick_drop_kind_ = BoxSpaceQuickDropKind::None;
    double box_space_quick_drop_elapsed_seconds_ = 0.0;
    SDL_Point box_space_quick_drop_start_pointer_{0, 0};
    SDL_Rect box_space_quick_drop_start_cell_bounds_{0, 0, 0, 0};
    int box_space_quick_drop_target_box_index_ = -1;
    KeyboardBoxSpaceChargeKind box_space_keyboard_charge_kind_ = KeyboardBoxSpaceChargeKind::None;
    void applyKeyboardBoxSpaceQuickDropCharge(double elapsed_seconds, KeyboardBoxSpaceChargeKind source);
    void applyKeyboardBoxSpaceAdvanceBoxPickupCharge(double elapsed_seconds);
    std::array<int, 30> box_space_slot_wiggle_dx_{};
    double box_space_wiggle_phase_ = 0.0;
    float box_space_multi_collapse_t_ = 0.f;
    double held_sprite_shake_timer_ = 0.0;
    double held_sprite_shake_phase_ = 0.0;
    int held_sprite_shake_offset_px_ = 0;
    transfer_system::move::HoldWithinRect box_space_box_move_hold_{};
    int box_space_box_move_source_box_index_ = -1;

    transfer_system::move::HeldMoveController held_move_{};

    FocusManager focus_;
    /// After a mouse hit on a focusable control, hide the controller selection ring until keyboard/gamepad input.
    bool selection_cursor_hidden_after_mouse_ = false;
    /// Mouse is over a Pokémon slot or game icon (speech bubble stays visible even if `selection_cursor_hidden_after_mouse_`).
    bool speech_hover_active_ = false;
    FocusNodeId mouse_hover_focus_node_ = -1;
    SDL_Point last_pointer_position_{0, 0};

    // --- Pointer drag-to-move (normal tool, mouse mode) ---
    bool pointer_drag_pickup_pending_ = false;
    bool pointer_drag_pickup_from_game_ = true;
    int pointer_drag_pickup_slot_index_ = -1;
    SDL_Rect pointer_drag_pickup_bounds_{0, 0, 0, 0};
    SDL_Point pointer_drag_pickup_start_{0, 0};

    // --- Pointer drag-to-move (item tool, mouse mode) ---
    bool pointer_drag_item_pickup_pending_ = false;
    bool pointer_drag_item_pickup_from_game_ = true;
    int pointer_drag_item_pickup_slot_index_ = -1;
    SDL_Rect pointer_drag_item_pickup_bounds_{0, 0, 0, 0};
    SDL_Point pointer_drag_item_pickup_start_{0, 0};

    transfer_system::PokemonActionMenuController pokemon_action_menu_;
    transfer_system::ItemActionMenuController item_action_menu_;
    transfer_system::PokemonMoveController pokemon_move_;
    transfer_system::MultiPokemonMoveController multi_pokemon_move_;
    bool pickup_sfx_requested_ = false;
    bool putdown_sfx_requested_ = false;
    /// Snapshot of the held Pokémon sprite at pickup / hand-swap so the in-hand texture does not change over time.
    TextureHandle held_move_sprite_tex_{};
    void updatePokemonActionMenu(double dt);
    void openPokemonActionMenu(bool from_game_box, int slot_index, const SDL_Rect& anchor_rect);
    void closePokemonActionMenu();
    bool pokemonActionMenuInteractive() const;
    SDL_Rect pokemonActionMenuFinalRect() const;
    int pokemonActionMenuBottomLimitY() const;
    const PcSlotSpecies* pokemonActionMenuPokemon() const;
    std::optional<int> pokemonActionMenuRowAtPoint(int logical_x, int logical_y) const;
    void activatePokemonActionMenuRow(int row);
    void hoverPokemonActionMenuRow(int logical_x, int logical_y);
    bool activateFocusedPokemonSlotActionMenu();
    bool handlePokemonActionMenuPointerPressed(int logical_x, int logical_y);
    bool handleItemActionMenuPointerPressed(int logical_x, int logical_y);
    bool handlePokemonSlotActionPointerPressed(int logical_x, int logical_y);
    bool handleItemSlotActionPointerPressed(int logical_x, int logical_y);
    bool swapToolActive() const;
    bool pokemonMoveActive() const;
    void requestPickupSfx();
    void requestPutdownSfx();
    BoxViewportModel resortBoxViewportModel() const;
    void refreshResortBoxViewportModel();
    void refreshGameBoxViewportModel();
    std::optional<transfer_system::PokemonMoveController::SlotRef> slotRefForFocus(FocusNodeId focus_id) const;
    std::optional<transfer_system::PokemonMoveController::SlotRef> slotRefAtPointer(int logical_x, int logical_y) const;
    bool pointerOverExpandedGameDropdown(int logical_x, int logical_y) const;
    bool pointerOverExpandedResortDropdown(int logical_x, int logical_y) const;
    PcSlotSpecies* mutablePokemonAt(const transfer_system::PokemonMoveController::SlotRef& ref);
    const PcSlotSpecies* pokemonAt(const transfer_system::PokemonMoveController::SlotRef& ref) const;
    void clearPokemonAt(const transfer_system::PokemonMoveController::SlotRef& ref);
    void setPokemonAt(const transfer_system::PokemonMoveController::SlotRef& ref, PcSlotSpecies pokemon);
    bool beginPokemonMoveFromSlot(
        const transfer_system::PokemonMoveController::SlotRef& ref,
        transfer_system::PokemonMoveController::InputMode input_mode,
        transfer_system::PokemonMoveController::PickupSource source,
        SDL_Point pointer);
    bool dropHeldPokemonAt(const transfer_system::PokemonMoveController::SlotRef& target);
    bool cancelHeldPokemonMove();
    bool beginMultiPokemonMoveFromSlots(
        const std::vector<transfer_system::PokemonMoveController::SlotRef>& refs,
        transfer_system::MultiPokemonMoveController::InputMode input_mode,
        SDL_Point pointer);
    bool dropHeldMultiPokemonAt(const transfer_system::PokemonMoveController::SlotRef& target);
    bool cancelHeldMultiPokemonMove();
    bool dropHeldMultiPokemonIntoFirstEmptySlotsInBox(int box_index);
    bool gameBoxHasEmptySlots(int box_index, int required_count) const;
    void drawHeldMultiPokemon(SDL_Renderer* renderer);
    std::optional<transfer_system::PokemonMoveController::SlotRef> multiPokemonAnchorSlotAtPointer(int logical_x, int logical_y) const;
    std::optional<transfer_system::PokemonMoveController::SlotRef> heldMultiPokemonAnchorSlot() const;
    void drawMultiSelectionDrag(SDL_Renderer* renderer) const;
    void drawKeyboardMultiMarquee(SDL_Renderer* renderer) const;
    void refreshHeldMoveSpriteTexture();
    void drawHeldPokemon(SDL_Renderer* renderer);
    void drawHeldItem(SDL_Renderer* renderer);
    void drawHeldBoxSpaceBox(SDL_Renderer* renderer);

    // --- Multi tool drag selection ---
    bool multi_select_drag_active_ = false;
    bool multi_select_from_game_ = true;
    SDL_Point multi_select_drag_start_{0, 0};
    SDL_Point multi_select_drag_current_{0, 0};
    SDL_Rect multi_select_drag_rect_{0, 0, 0, 0};
    std::vector<transfer_system::PokemonMoveController::SlotRef> multiSlotRefsIntersectingRect(bool from_game, const SDL_Rect& rect) const;

    /// Keyboard/controller: marquee pick (green tool only, normal box view — not Box Space).
    bool keyboard_multi_marquee_active_ = false;
    bool keyboard_multi_marquee_from_game_ = true;
    int keyboard_multi_marquee_anchor_slot_ = 0;
    int keyboard_multi_marquee_corner_slot_ = 0;
    std::vector<transfer_system::PokemonMoveController::SlotRef> keyboardMultiMarqueeOccupiedRefs() const;
    SDL_Rect keyboardMultiMarqueeScreenRect() const;

    bool box_space_suppress_click_open_on_release_ = false;

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
    std::optional<int> focusedBoxSpaceBoxIndex() const;
    bool swapGamePcBoxes(int a, int b);
    bool swapGameAndResortPcBoxes(int game_box_index, int resort_box_index);
    bool dropHeldPokemonIntoFirstEmptySlotInBox(int box_index);
    bool dropHeldPokemonIntoFirstEmptySlotInResortBox(int box_index);
    bool gameBoxHasEmptySlot(int box_index) const;
    bool gameBoxHasPreviewContent(int box_index) const;
    bool resortBoxHasPreviewContent(int box_index) const;
    bool shouldShowMiniPreviewForBox(int box_index, MiniPreviewContext context) const;
    bool shouldShowMiniPreviewForResortBox(int box_index, MiniPreviewContext context) const;
    bool openGameBoxFromBoxSpaceSelection(int box_index);
    bool activateFocusedGameSlot();
    bool handleGameBoxSpacePointerPressed(int logical_x, int logical_y);
    bool handleResortBoxSpacePointerPressed(int logical_x, int logical_y);
    bool handleDropdownPointerPressed(int logical_x, int logical_y);
    bool handleGameBoxNavigationPointerPressed(int logical_x, int logical_y);
    bool handleResortBoxNavigationPointerPressed(int logical_x, int logical_y);
    bool handleResortDropdownPointerPressed(int logical_x, int logical_y);

    void updateActionMenus(double dt);
    void updateBoxSpaceLongPressGestures(double dt);
    void updateBoxViewportsAndFocusDimming(double dt);
    void initializeResortPcBoxesFromStorage(SDL_Renderer* renderer);
    PcSlotSpecies pcSlotFromResortSlotView(const resort::PokemonSlotView& view, int box_id, int slot_index) const;
    void persistResortPokemonDropToStorage(
        const transfer_system::PokemonMoveController::SlotRef& target,
        const transfer_system::PokemonMoveController::SlotRef& return_slot,
        bool target_was_occupied,
        bool swap_into_hand,
        const std::string& held_pkrid,
        const std::string& target_pkrid_before);

    void clearBoxSpaceQuickDropGesture();
    void triggerHeldSpriteRejectFeedback();
    void updateBoxSpaceQuickDropVisuals(double dt);
    bool tryGiveHeldItemToFirstEligiblePokemonInGameBox(int box_index);
    bool tryGiveHeldItemToFirstEligiblePokemonInResortBox(int box_index);
    bool completeBoxSpaceQuickDrop(int target_box);

    enum class BoxRenameModalPanel { Game, Resort };
    enum class BoxRenameFocusSlot { Field, Cancel, Confirm };

    void openBoxRenameModal(BoxRenameModalPanel panel);
    void closeBoxRenameModal(bool commit);
    void syncBoxRenameModalLayout();
    void drawBoxRenameModal(SDL_Renderer* renderer);
    void drawBoxRenameFocusRing(SDL_Renderer* renderer) const;
    bool handleBoxRenameModalPointerPressed(int logical_x, int logical_y);
    void drawExitSaveModal(SDL_Renderer* renderer) const;

    bool box_rename_modal_open_ = false;
    bool box_rename_editing_ = false;
    BoxRenameFocusSlot box_rename_focus_slot_{BoxRenameFocusSlot::Field};
    BoxRenameModalPanel box_rename_modal_panel_{BoxRenameModalPanel::Game};
    int box_rename_box_index_ = -1;
    std::string box_rename_original_utf8_;
    std::string box_rename_text_utf8_;
    std::string box_rename_ime_utf8_;
    SDL_Rect box_rename_text_field_rect_virt_{0, 0, 0, 0};
    SDL_Rect box_rename_ok_button_rect_virt_{0, 0, 0, 0};
    SDL_Rect box_rename_cancel_button_rect_virt_{0, 0, 0, 0};
    SDL_Rect box_rename_card_rect_virt_{0, 0, 0, 0};
    double box_rename_caret_blink_phase_ = 0.0;
};

} // namespace pr
