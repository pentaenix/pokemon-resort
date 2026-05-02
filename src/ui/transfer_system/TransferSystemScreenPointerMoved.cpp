#include "ui/TransferSystemScreen.hpp"

#include <SDL.h>

#include <cmath>

namespace pr {

void TransferSystemScreen::handlePointerMoved(int logical_x, int logical_y) {
    const bool pointer_moved =
        logical_x != last_pointer_position_.x || logical_y != last_pointer_position_.y;
    last_pointer_position_ = SDL_Point{logical_x, logical_y};
    if (box_rename_modal_open_) {
        return;
    }
    if (pointer_moved) {
        selection_cursor_hidden_after_mouse_ = true;
        if (pokemon_move_.active()) {
            if (auto* h = pokemon_move_.held()) {
                h->input_mode = transfer_system::PokemonMoveController::InputMode::Pointer;
            }
        }
        if (multi_pokemon_move_.active()) {
            multi_pokemon_move_.updatePointer(last_pointer_position_, window_config_.virtual_width, window_config_.virtual_height);
        }
        if (auto* h = held_move_.heldItem()) {
            h->input_mode = transfer_system::move::HeldMoveController::InputMode::Pointer;
        }
    }
    mouse_hover_mini_preview_box_index_ = -1;
    mouse_hover_focus_node_ = -1;

    // Exit button hover should drive info-banner tooltips (not speech bubbles).
    if (exit_button_enabled_) {
        const int bs = carousel_style_.viewport_height;
        const int bx = carousel_style_.offset_from_left_wall;
        const int by = exitButtonScreenY();
        if (bs > 0 && logical_x >= bx && logical_x < bx + bs && logical_y >= by && logical_y < by + bs) {
            mouse_hover_focus_node_ = 5000;
            focus_.setCurrent(5000);
            selection_cursor_hidden_after_mouse_ = true;
            speech_hover_active_ = false;
            return;
        }
    }

    if (pokemon_move_.active()) {
        pokemon_move_.updatePointer(last_pointer_position_, window_config_.virtual_width, window_config_.virtual_height);
    }
    if (multi_pokemon_move_.active()) {
        multi_pokemon_move_.updatePointer(last_pointer_position_, window_config_.virtual_width, window_config_.virtual_height);
    }
    if (held_move_.heldItem()) {
        held_move_.updatePointer(last_pointer_position_, window_config_.virtual_width, window_config_.virtual_height);
    }

    if (held_move_.heldBox()) {
        held_move_.updatePointer(last_pointer_position_, window_config_.virtual_width, window_config_.virtual_height);
    }

    if (box_space_quick_drop_pending_) {
        auto in = [](int px, int py, const SDL_Rect& r) {
            return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
        };
        const int dx = logical_x - box_space_quick_drop_start_pointer_.x;
        const int dy = logical_y - box_space_quick_drop_start_pointer_.y;
        constexpr int kQuickDropCancelThresholdPx = 6;
        const bool moved_far = (dx * dx + dy * dy) >= kQuickDropCancelThresholdPx * kQuickDropCancelThresholdPx;
        if (!in(logical_x, logical_y, box_space_quick_drop_start_cell_bounds_) || moved_far) {
            clearBoxSpaceQuickDropGesture();
        }
    }

    if (multi_select_drag_active_) {
        multi_select_drag_current_ = last_pointer_position_;
        const int x0 = std::min(multi_select_drag_start_.x, multi_select_drag_current_.x);
        const int y0 = std::min(multi_select_drag_start_.y, multi_select_drag_current_.y);
        const int x1 = std::max(multi_select_drag_start_.x, multi_select_drag_current_.x);
        const int y1 = std::max(multi_select_drag_start_.y, multi_select_drag_current_.y);
        multi_select_drag_rect_ = SDL_Rect{x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)};
        return;
    }

    if (pointer_drag_pickup_pending_) {
        const int dx = logical_x - pointer_drag_pickup_start_.x;
        const int dy = logical_y - pointer_drag_pickup_start_.y;
        const int dist2 = dx * dx + dy * dy;
        auto in = [](int px, int py, const SDL_Rect& r) {
            return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
        };
        constexpr int kDragPickupThresholdPx = 7;
        const bool moved_far = dist2 >= kDragPickupThresholdPx * kDragPickupThresholdPx;
        const bool left_bounds = !in(logical_x, logical_y, pointer_drag_pickup_bounds_);
        if (moved_far || left_bounds) {
            using Move = transfer_system::PokemonMoveController;
            const Move::SlotRef ref{
                pointer_drag_pickup_from_game_ ? Move::Panel::Game : Move::Panel::Resort,
                pointer_drag_pickup_from_game_ ? game_box_browser_.gameBoxIndex() : resort_box_browser_.gameBoxIndex(),
                pointer_drag_pickup_slot_index_};
            pointer_drag_pickup_pending_ = false;
            (void)beginPokemonMoveFromSlot(ref, Move::InputMode::Pointer, Move::PickupSource::ActionMenu, last_pointer_position_);
        }
    }

    if (pointer_drag_item_pickup_pending_) {
        const int dx = logical_x - pointer_drag_item_pickup_start_.x;
        const int dy = logical_y - pointer_drag_item_pickup_start_.y;
        const int dist2 = dx * dx + dy * dy;
        auto in = [](int px, int py, const SDL_Rect& r) {
            return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
        };
        constexpr int kDragPickupThresholdPx = 7;
        const bool moved_far = dist2 >= kDragPickupThresholdPx * kDragPickupThresholdPx;
        const bool left_bounds = !in(logical_x, logical_y, pointer_drag_item_pickup_bounds_);
        if (moved_far || left_bounds) {
            using Move = transfer_system::PokemonMoveController;
            const Move::SlotRef ref{
                pointer_drag_item_pickup_from_game_ ? Move::Panel::Game : Move::Panel::Resort,
                pointer_drag_item_pickup_from_game_ ? game_box_browser_.gameBoxIndex() : resort_box_browser_.gameBoxIndex(),
                pointer_drag_item_pickup_slot_index_};
            pointer_drag_item_pickup_pending_ = false;

            PcSlotSpecies* src = mutablePokemonAt(ref);
            if (src && src->occupied() && src->held_item_id > 0) {
                held_move_.pickUpItem(
                    src->held_item_id,
                    src->held_item_name,
                    transfer_system::move::HeldMoveController::PokemonSlotRef{
                        pointer_drag_item_pickup_from_game_
                            ? transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Game
                            : transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Resort,
                        pointer_drag_item_pickup_from_game_ ? game_box_browser_.gameBoxIndex() : resort_box_browser_.gameBoxIndex(),
                        pointer_drag_item_pickup_slot_index_},
                    transfer_system::move::HeldMoveController::InputMode::Pointer,
                    last_pointer_position_);
                if (pointer_drag_item_pickup_from_game_) {
                    if (!syncGamePcSlotHeldItemPayload(*src, -1, std::string{})) {
                        ui_state_.requestErrorSfx();
                        held_move_.clear();
                        refreshResortBoxViewportModel();
                        refreshGameBoxViewportModel();
                        return;
                    }
                    markGameBoxesDirty();
                } else {
                    src->held_item_id = -1;
                    src->held_item_name.clear();
                    markResortBoxesDirty();
                }
                refreshResortBoxViewportModel();
                refreshGameBoxViewportModel();
                requestPickupSfx();
            }
        }
    }

    if (pokemon_action_menu_.visible()) {
        hoverPokemonActionMenuRow(logical_x, logical_y);
        return;
    }
    if (item_action_menu_.visible()) {
        if (const std::optional<int> row = item_action_menu_.rowAtPoint(
                logical_x,
                logical_y,
                pokemon_action_menu_style_,
                window_config_.virtual_width,
                pokemonActionMenuBottomLimitY())) {
            item_action_menu_.selectRow(*row);
        }
        return;
    }

    if (game_box_browser_.gameBoxSpaceMode() && box_space_drag_active_ && game_save_box_viewport_ &&
        box_space_interaction_panel_ == BoxSpaceInteractionPanel::Game) {
        const int dy = logical_y - box_space_drag_last_y_;
        box_space_drag_last_y_ = logical_y;
        box_space_drag_accum_ += static_cast<double>(dy);

        // Scroll threshold tuned by feel; avoids jitter.
        constexpr double kRowStepThresholdPx = 42.0;
        const int max_row = gameBoxSpaceMaxRowOffset();
        while (box_space_drag_accum_ >= kRowStepThresholdPx) {
            // Dragging down should reveal earlier rows (scroll up).
            if (game_box_browser_.gameBoxSpaceRowOffset() > 0) {
                stepGameBoxSpaceRowUp();
            }
            box_space_drag_accum_ -= kRowStepThresholdPx;
            if (game_box_browser_.gameBoxSpaceRowOffset() <= 0) {
                break;
            }
        }
        while (box_space_drag_accum_ <= -kRowStepThresholdPx) {
            if (game_box_browser_.gameBoxSpaceRowOffset() < max_row) {
                stepGameBoxSpaceRowDown();
            }
            box_space_drag_accum_ += kRowStepThresholdPx;
            if (game_box_browser_.gameBoxSpaceRowOffset() >= max_row) {
                break;
            }
        }
        if (pokemon_move_.active()) {
            syncBoxSpaceMiniPreviewHoverFromPointer(logical_x, logical_y);
        }
        return;
    }

    if (resort_box_browser_.gameBoxSpaceMode() && box_space_drag_active_ && resort_box_viewport_ &&
        box_space_interaction_panel_ == BoxSpaceInteractionPanel::Resort) {
        const int dy = logical_y - box_space_drag_last_y_;
        box_space_drag_last_y_ = logical_y;
        box_space_drag_accum_ += static_cast<double>(dy);

        constexpr double kRowStepThresholdPx = 42.0;
        const int max_row = resortBoxSpaceMaxRowOffset();
        while (box_space_drag_accum_ >= kRowStepThresholdPx) {
            if (resort_box_browser_.gameBoxSpaceRowOffset() > 0) {
                stepResortBoxSpaceRowUp();
            }
            box_space_drag_accum_ -= kRowStepThresholdPx;
            if (resort_box_browser_.gameBoxSpaceRowOffset() <= 0) {
                break;
            }
        }
        while (box_space_drag_accum_ <= -kRowStepThresholdPx) {
            if (resort_box_browser_.gameBoxSpaceRowOffset() < max_row) {
                stepResortBoxSpaceRowDown();
            }
            box_space_drag_accum_ += kRowStepThresholdPx;
            if (resort_box_browser_.gameBoxSpaceRowOffset() >= max_row) {
                break;
            }
        }
        if (pokemon_move_.active()) {
            syncBoxSpaceMiniPreviewHoverFromPointer(logical_x, logical_y);
        }
        return;
    }

    if (box_name_dropdown_style_.enabled && game_box_browser_.dropdownOpenTarget() &&
        game_box_browser_.dropdownExpandT() > 0.05 &&
        dropdown_lmb_down_in_panel_) {
        SDL_Rect outer{};
        int list_h = 0;
        int list_clip_y = 0;
        if (computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
            const int dy = logical_y - dropdown_lmb_last_y_;
            dropdown_lmb_last_y_ = logical_y;
            dropdown_lmb_drag_accum_ += std::fabs(static_cast<double>(dy));
            game_box_browser_.scrollDropdownBy(
                -static_cast<double>(dy) * box_name_dropdown_style_.scroll_drag_multiplier,
                static_cast<int>(game_pc_boxes_.size()),
                list_h);
        }
        return;
    }
    if (box_name_dropdown_style_.enabled && game_box_browser_.dropdownOpenTarget() &&
        game_box_browser_.dropdownExpandT() > 0.15 &&
        !dropdown_lmb_down_in_panel_) {
        if (const std::optional<int> row = dropdownRowIndexAtScreen(logical_x, logical_y)) {
            const int current = game_box_browser_.dropdownHighlightIndex();
            stepDropdownHighlight(*row - current);
            SDL_Rect outer{};
            int list_h = 0;
            int list_clip_y = 0;
            if (computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
                syncDropdownScrollToHighlight(list_h);
            }
        }
        return;
    }

    if (box_name_dropdown_style_.enabled && resort_box_browser_.dropdownOpenTarget() &&
        resort_box_browser_.dropdownExpandT() > 0.05 &&
        dropdown_lmb_down_in_panel_) {
        SDL_Rect outer{};
        int list_h = 0;
        int list_clip_y = 0;
        if (computeResortBoxDropdownOuterRect(
                outer, static_cast<float>(resort_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
            const int dy = logical_y - dropdown_lmb_last_y_;
            dropdown_lmb_last_y_ = logical_y;
            dropdown_lmb_drag_accum_ += std::fabs(static_cast<double>(dy));
            resort_box_browser_.scrollDropdownBy(
                -static_cast<double>(dy) * box_name_dropdown_style_.scroll_drag_multiplier,
                static_cast<int>(resort_pc_boxes_.size()),
                list_h);
        }
        return;
    }
    if (box_name_dropdown_style_.enabled && resort_box_browser_.dropdownOpenTarget() &&
        resort_box_browser_.dropdownExpandT() > 0.15 &&
        !dropdown_lmb_down_in_panel_) {
        if (const std::optional<int> row = resortDropdownRowIndexAtScreen(logical_x, logical_y)) {
            const int current = resort_box_browser_.dropdownHighlightIndex();
            stepResortDropdownHighlight(*row - current);
            SDL_Rect outer{};
            int list_h = 0;
            int list_clip_y = 0;
            if (computeResortBoxDropdownOuterRect(
                    outer, static_cast<float>(resort_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
                syncResortDropdownScrollToHighlight(list_h);
            }
        }
        return;
    }

    if (ui_state_.uiEnter() > 0.85 && hitTestPillTrack(logical_x, logical_y)) {
        mouse_hover_focus_node_ = 4000;
        focus_.setCurrent(4000);
        selection_cursor_hidden_after_mouse_ = true;
        speech_hover_active_ = false;
        return;
    }

    if (pokemon_move_.active() || multi_pokemon_move_.active()) {
        if ((game_box_browser_.gameBoxSpaceMode() || resort_box_browser_.gameBoxSpaceMode()) &&
            ui_state_.panelsReveal() > 0.85 && ui_state_.uiEnter() > 0.85) {
            syncBoxSpaceMiniPreviewHoverFromPointer(logical_x, logical_y);
        }
        return;
    }

    if (!(box_name_dropdown_style_.enabled && game_box_browser_.dropdownOpenTarget() &&
          game_box_browser_.dropdownExpandT() > 0.05) &&
        !(box_name_dropdown_style_.enabled && resort_box_browser_.dropdownOpenTarget() &&
          resort_box_browser_.dropdownExpandT() > 0.05) &&
        ui_state_.panelsReveal() > 0.85 && ui_state_.uiEnter() > 0.85) {
        if (const std::optional<FocusNodeId> hovered = focusNodeAtPointer(logical_x, logical_y)) {
            mouse_hover_focus_node_ = *hovered;
            focus_.setCurrent(*hovered);
            selection_cursor_hidden_after_mouse_ = true;
        }
        if (const auto hit = speechBubbleTargetAtPointer(logical_x, logical_y)) {
            focus_.setCurrent(hit->first);
            // Mouse hover should not show the legacy yellow rectangle; keep "mouse mode" on.
            selection_cursor_hidden_after_mouse_ = true;
            speech_hover_active_ = true;
        } else {
            speech_hover_active_ = false;
        }

        if (game_box_browser_.gameBoxSpaceMode() || resort_box_browser_.gameBoxSpaceMode()) {
            syncBoxSpaceMiniPreviewHoverFromPointer(logical_x, logical_y);
        }
    }
}

} // namespace pr

