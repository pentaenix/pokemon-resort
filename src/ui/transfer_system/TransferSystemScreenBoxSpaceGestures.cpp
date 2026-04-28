#include "ui/TransferSystemScreen.hpp"

#include <SDL.h>

namespace pr {

void TransferSystemScreen::updateBoxSpaceLongPressGestures(double dt) {
    // Box Space: if the user is holding the mouse down in the grid, allow a hold-to-pickup box move
    // without interfering with normal drag-to-scroll (movement cancels the hold activation).
    if (box_space_drag_active_ &&
        box_space_box_move_hold_.active &&
        !pokemon_move_.active() &&
        !multi_pokemon_move_.active() &&
        !held_move_.heldBox()) {
        const bool game_bs = game_box_browser_.gameBoxSpaceMode();
        const bool resort_bs = resort_box_browser_.gameBoxSpaceMode();
        const bool panel_ok =
            (game_bs && box_space_interaction_panel_ == BoxSpaceInteractionPanel::Game) ||
            (resort_bs && box_space_interaction_panel_ == BoxSpaceInteractionPanel::Resort);
        if (panel_ok && box_space_box_move_hold_.update(dt, last_pointer_position_)) {
            if (box_space_box_move_source_box_index_ >= 0) {
                if (box_space_interaction_panel_ == BoxSpaceInteractionPanel::Resort) {
                    held_move_.pickUpBox(
                        transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Resort,
                        box_space_box_move_source_box_index_,
                        transfer_system::move::HeldMoveController::InputMode::Pointer,
                        last_pointer_position_);
                } else {
                    held_move_.pickUpBox(
                        transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Game,
                        box_space_box_move_source_box_index_,
                        transfer_system::move::HeldMoveController::InputMode::Pointer,
                        last_pointer_position_);
                }
                // Cancel scroll drag and treat the rest of the gesture as a held box move.
                box_space_drag_active_ = false;
                box_space_drag_accum_ = 0.0;
                box_space_pressed_cell_ = -1;
                box_space_box_move_hold_.cancel();
                refreshGameBoxViewportModel();
                refreshResortBoxViewportModel();
                requestPickupSfx();
            }
        }
    }

    // Box Space: pointer press-and-hold tries to quick-drop Pokémon or place an item onto eligible party slots.
    if ((game_box_browser_.gameBoxSpaceMode() || resort_box_browser_.gameBoxSpaceMode()) &&
        box_space_drag_active_ &&
        box_space_quick_drop_pending_) {
        auto in = [](int px, int py, const SDL_Rect& r) {
            return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
        };
        const bool still_in_cell = in(
            last_pointer_position_.x,
            last_pointer_position_.y,
            box_space_quick_drop_start_cell_bounds_);
        const int dx = last_pointer_position_.x - box_space_quick_drop_start_pointer_.x;
        const int dy = last_pointer_position_.y - box_space_quick_drop_start_pointer_.y;
        constexpr int kQuickDropCancelThresholdPx = 6;
        const bool moved_far = (dx * dx + dy * dy) >= kQuickDropCancelThresholdPx * kQuickDropCancelThresholdPx;
        if (!still_in_cell || moved_far) {
            clearBoxSpaceQuickDropGesture();
        } else {
            box_space_quick_drop_elapsed_seconds_ += dt;
            if (box_space_quick_drop_elapsed_seconds_ >= box_space_long_press_style_.quick_drop_hold_seconds) {
                const int target_box = box_space_quick_drop_target_box_index_;
                const bool dropped = completeBoxSpaceQuickDrop(target_box);
                clearBoxSpaceQuickDropGesture();
                if (dropped) {
                    // Prevent the corresponding release from being treated as a click (open box).
                    box_space_drag_active_ = false;
                    box_space_drag_accum_ = 0.0;
                    box_space_pressed_cell_ = -1;
                    refreshGameBoxViewportModel();
                    refreshResortBoxViewportModel();
                    // Force mini-preview + hover preview to refresh (box occupancy and contents changed).
                    mini_preview_box_index_ = -1;
                    mouse_hover_mini_preview_box_index_ = target_box;
                    mini_preview_model_from_resort_ =
                        box_space_interaction_panel_ == BoxSpaceInteractionPanel::Resort;
                } else {
                    box_space_suppress_click_open_on_release_ = true;
                    triggerHeldSpriteRejectFeedback();
                }
            }
        }
    }

    updateBoxSpaceQuickDropVisuals(dt);
}

} // namespace pr

