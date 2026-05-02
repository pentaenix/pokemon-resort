#include "ui/TransferSystemScreen.hpp"

#include "resort/services/PokemonResortService.hpp"

#include <algorithm>

namespace pr {

namespace {
constexpr const char* kDefaultResortProfileId = "default";
}

bool TransferSystemScreen::openResortBoxFromBoxSpaceSelection(int box_index) {
    if (box_index < 0 || box_index >= static_cast<int>(resort_pc_boxes_.size())) {
        return false;
    }
    mini_preview_target_ = 0.0;
    mini_preview_t_ = 0.0;
    mini_preview_box_index_ = -1;
    mouse_hover_mini_preview_box_index_ = -1;
    setResortBoxSpaceMode(false);
    resort_box_browser_.jumpGameBoxToIndex(
        box_index, static_cast<int>(resort_pc_boxes_.size()), panelsReadyForInteraction());
    if (resort_box_viewport_) {
        resort_box_viewport_->snapContentToModel(resortBoxViewportModelAt(box_index));
    }
    ui_state_.requestButtonSfx();
    return true;
}

bool TransferSystemScreen::activateFocusedResortSlot() {
    const std::optional<int> slot = focusedResortSlotIndex();
    if (!slot.has_value()) {
        return false;
    }
    if (!panelsReadyForInteraction() || !resort_box_viewport_) {
        return false;
    }
    if (resort_box_browser_.gameBoxSpaceMode()) {
        const int box_index = resort_box_browser_.gameBoxSpaceRowOffset() * 6 + *slot;
        return openResortBoxFromBoxSpaceSelection(box_index);
    }
    return false;
}

bool TransferSystemScreen::swapGamePcBoxes(int a, int b) {
    if (a < 0 || b < 0 || a >= static_cast<int>(game_pc_boxes_.size()) || b >= static_cast<int>(game_pc_boxes_.size())) {
        return false;
    }
    if (a == b) {
        return true;
    }
    std::swap(game_pc_boxes_[static_cast<std::size_t>(a)], game_pc_boxes_[static_cast<std::size_t>(b)]);
    markGameBoxesDirty();
    if (game_save_box_viewport_ && game_box_browser_.gameBoxSpaceMode()) {
        const bool show_down = gameBoxSpaceMaxRowOffset() > 0;
        game_save_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::BoxSpace, show_down);
        game_save_box_viewport_->snapContentToModel(gameBoxSpaceViewportModelAt(game_box_browser_.gameBoxSpaceRowOffset()));
    }
    mini_preview_box_index_ = -1;
    mouse_hover_mini_preview_box_index_ = -1;
    ui_state_.requestButtonSfx();
    return true;
}

bool TransferSystemScreen::swapGameAndResortPcBoxes(int game_box_index, int resort_box_index) {
    if (game_box_index < 0 || resort_box_index < 0 ||
        game_box_index >= static_cast<int>(game_pc_boxes_.size()) ||
        resort_box_index >= static_cast<int>(resort_pc_boxes_.size())) {
        return false;
    }
    if (!boxFitsInGameSaveSlots(resort_pc_boxes_[static_cast<std::size_t>(resort_box_index)])) {
        return false;
    }
    std::swap(game_pc_boxes_[static_cast<std::size_t>(game_box_index)],
              resort_pc_boxes_[static_cast<std::size_t>(resort_box_index)]);
    markGameBoxesDirty();
    markResortBoxesDirty();
    mini_preview_box_index_ = -1;
    mouse_hover_mini_preview_box_index_ = -1;
    mini_preview_model_from_resort_ = false;
    if (game_save_box_viewport_) {
        if (game_box_browser_.gameBoxSpaceMode()) {
            game_save_box_viewport_->snapContentToModel(
                gameBoxSpaceViewportModelAt(game_box_browser_.gameBoxSpaceRowOffset()));
        } else {
            game_save_box_viewport_->snapContentToModel(gameBoxViewportModelAt(game_box_browser_.gameBoxIndex()));
        }
    }
    if (resort_box_viewport_) {
        if (resort_box_browser_.gameBoxSpaceMode()) {
            resort_box_viewport_->snapContentToModel(
                resortBoxSpaceViewportModelAt(resort_box_browser_.gameBoxSpaceRowOffset()));
        } else {
            resort_box_viewport_->snapContentToModel(resortBoxViewportModelAt(resort_box_browser_.gameBoxIndex()));
        }
    }
    refreshGameBoxViewportModel();
    refreshResortBoxViewportModel();
    ui_state_.requestButtonSfx();
    return true;
}

bool TransferSystemScreen::openGameBoxFromBoxSpaceSelection(int box_index) {
    if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
        return false;
    }
    mini_preview_target_ = 0.0;
    mini_preview_t_ = 0.0;
    mini_preview_box_index_ = -1;
    mouse_hover_mini_preview_box_index_ = -1;
    setGameBoxSpaceMode(false);
    // Box Space should feel like a direct jump, not the per-box slide animation.
    game_box_browser_.jumpGameBoxToIndex(box_index, static_cast<int>(game_pc_boxes_.size()), panelsReadyForInteraction());
    if (game_save_box_viewport_) {
        game_save_box_viewport_->snapContentToModel(gameBoxViewportModelAt(box_index));
    }
    ui_state_.requestButtonSfx();
    return true;
}

void TransferSystemScreen::setResortBoxSpaceMode(bool enabled) {
    if (!resort_box_viewport_) {
        resort_box_browser_.setGameBoxSpaceMode(false, static_cast<int>(resort_pc_boxes_.size()));
        return;
    }

    resort_box_browser_.setGameBoxSpaceMode(enabled, static_cast<int>(resort_pc_boxes_.size()));

    if (resort_box_browser_.gameBoxSpaceMode()) {
        const bool show_down = resortBoxSpaceMaxRowOffset() > 0;
        resort_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::BoxSpace, show_down);
        resort_box_viewport_->setBoxSpaceActive(true);
        resort_box_viewport_->snapContentToModel(
            resortBoxSpaceViewportModelAt(resort_box_browser_.gameBoxSpaceRowOffset()));
    } else {
        resort_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::Normal, false);
        resort_box_viewport_->setBoxSpaceActive(false);
        resort_box_viewport_->snapContentToModel(resortBoxViewportModelAt(resort_box_browser_.gameBoxIndex()));
    }

    mini_preview_box_index_ = -1;
    mouse_hover_mini_preview_box_index_ = -1;
    mini_preview_model_from_resort_ = false;
    box_space_drag_active_ = false;
    box_space_drag_last_y_ = 0;
    box_space_drag_accum_ = 0.0;
    box_space_pressed_cell_ = -1;
    box_space_quick_drop_pending_ = false;
    box_space_quick_drop_kind_ = BoxSpaceQuickDropKind::None;
    box_space_quick_drop_elapsed_seconds_ = 0.0;
    box_space_quick_drop_target_box_index_ = -1;
    keyboard_multi_marquee_active_ = false;
    box_space_interaction_panel_ = BoxSpaceInteractionPanel::None;
    if (!resort_box_browser_.gameBoxSpaceMode()) {
        box_space_box_move_hold_.cancel();
        box_space_box_move_source_box_index_ = -1;
        if (held_move_.heldBox()) {
            held_move_.clear();
        }
    }
    ui_state_.requestButtonSfx();
}

bool TransferSystemScreen::swapResortPcBoxes(int a, int b) {
    if (a < 0 || b < 0 || a >= static_cast<int>(resort_pc_boxes_.size()) || b >= static_cast<int>(resort_pc_boxes_.size())) {
        return false;
    }
    if (a == b) {
        return true;
    }
    std::swap(resort_pc_boxes_[static_cast<std::size_t>(a)], resort_pc_boxes_[static_cast<std::size_t>(b)]);
    markResortBoxesDirty();
    if (resort_service_) {
        resort_service_->swapResortBoxContents(kDefaultResortProfileId, a, b);
    }
    if (resort_box_viewport_ && resort_box_browser_.gameBoxSpaceMode()) {
        const bool show_down = resortBoxSpaceMaxRowOffset() > 0;
        resort_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::BoxSpace, show_down);
        resort_box_viewport_->snapContentToModel(resortBoxSpaceViewportModelAt(resort_box_browser_.gameBoxSpaceRowOffset()));
    }
    mini_preview_box_index_ = -1;
    mouse_hover_mini_preview_box_index_ = -1;
    ui_state_.requestButtonSfx();
    return true;
}

void TransferSystemScreen::setGameBoxSpaceMode(bool enabled) {
    if (!game_save_box_viewport_) {
        game_box_browser_.setGameBoxSpaceMode(false, static_cast<int>(game_pc_boxes_.size()));
        return;
    }

    game_box_browser_.setGameBoxSpaceMode(enabled, static_cast<int>(game_pc_boxes_.size()));

    if (game_box_browser_.gameBoxSpaceMode()) {
        const bool show_down = gameBoxSpaceMaxRowOffset() > 0;
        game_save_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::BoxSpace, show_down);
        game_save_box_viewport_->setBoxSpaceActive(true);
        game_save_box_viewport_->snapContentToModel(
            gameBoxSpaceViewportModelAt(game_box_browser_.gameBoxSpaceRowOffset()));
    } else {
        game_save_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::Normal, false);
        game_save_box_viewport_->setBoxSpaceActive(false);
        game_save_box_viewport_->snapContentToModel(gameBoxViewportModelAt(game_box_browser_.gameBoxIndex()));
    }

    box_space_drag_active_ = false;
    box_space_drag_last_y_ = 0;
    box_space_drag_accum_ = 0.0;
    box_space_pressed_cell_ = -1;
    box_space_quick_drop_pending_ = false;
    box_space_quick_drop_kind_ = BoxSpaceQuickDropKind::None;
    box_space_quick_drop_elapsed_seconds_ = 0.0;
    box_space_quick_drop_target_box_index_ = -1;
    keyboard_multi_marquee_active_ = false;
    box_space_interaction_panel_ = BoxSpaceInteractionPanel::None;
    mini_preview_model_from_resort_ = false;
    if (!game_box_browser_.gameBoxSpaceMode()) {
        box_space_box_move_hold_.cancel();
        box_space_box_move_source_box_index_ = -1;
        if (held_move_.heldBox()) {
            held_move_.clear();
        }
    }
}

} // namespace pr

