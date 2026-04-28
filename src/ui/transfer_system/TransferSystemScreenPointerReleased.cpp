#include "ui/TransferSystemScreen.hpp"

#include <SDL.h>

#include <cmath>

namespace pr {

bool TransferSystemScreen::handlePointerReleased(int logical_x, int logical_y) {
    last_pointer_position_ = SDL_Point{logical_x, logical_y};
    if (multi_select_drag_active_) {
        multi_select_drag_active_ = false;
        multi_select_drag_current_ = last_pointer_position_;
        const int x0 = std::min(multi_select_drag_start_.x, multi_select_drag_current_.x);
        const int y0 = std::min(multi_select_drag_start_.y, multi_select_drag_current_.y);
        const int x1 = std::max(multi_select_drag_start_.x, multi_select_drag_current_.x);
        const int y1 = std::max(multi_select_drag_start_.y, multi_select_drag_current_.y);
        multi_select_drag_rect_ = SDL_Rect{x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)};
        const auto refs = multiSlotRefsIntersectingRect(multi_select_from_game_, multi_select_drag_rect_);
        if (!refs.empty()) {
            (void)beginMultiPokemonMoveFromSlots(
                refs,
                transfer_system::MultiPokemonMoveController::InputMode::Pointer,
                last_pointer_position_);
        }
        return true;
    }
    if (pointer_drag_pickup_pending_) {
        // Treat as a click: open the action menu. Drag pickup would have cleared `pointer_drag_pickup_pending_`.
        const bool from_game = pointer_drag_pickup_from_game_;
        const int slot = pointer_drag_pickup_slot_index_;
        const SDL_Rect r = pointer_drag_pickup_bounds_;
        pointer_drag_pickup_pending_ = false;
        if (from_game) {
            openPokemonActionMenu(true, slot, r);
        } else {
            openPokemonActionMenu(false, slot, r);
        }
        return true;
    }
    if (pointer_drag_item_pickup_pending_) {
        // Treat as a click: open the item action menu. Drag pickup clears this flag.
        const bool from_game = pointer_drag_item_pickup_from_game_;
        const int slot = pointer_drag_item_pickup_slot_index_;
        const SDL_Rect r = pointer_drag_item_pickup_bounds_;
        pointer_drag_item_pickup_pending_ = false;
        item_action_menu_.setPutAwayGameLabel(selection_game_title_);
        item_action_menu_.open(from_game, slot, r);
        ui_state_.requestButtonSfx();
        return true;
    }
    if (pokemon_move_.active()) {
        pokemon_move_.updatePointer(last_pointer_position_, window_config_.virtual_width, window_config_.virtual_height);
    }
    if (multi_pokemon_move_.active()) {
        multi_pokemon_move_.updatePointer(last_pointer_position_, window_config_.virtual_width, window_config_.virtual_height);
    }
    if (box_space_drag_active_) {
        constexpr double kClickDragThresholdPx = 6.0;
        const bool treat_as_click = std::fabs(box_space_drag_accum_) < kClickDragThresholdPx;
        box_space_drag_active_ = false;
        box_space_drag_accum_ = 0.0;
        if (box_space_suppress_click_open_on_release_) {
            box_space_suppress_click_open_on_release_ = false;
            box_space_pressed_cell_ = -1;
            box_space_box_move_hold_.cancel();
            box_space_box_move_source_box_index_ = -1;
            return true;
        }
        if (treat_as_click && box_space_pressed_cell_ >= 0 && box_space_pressed_cell_ < 30 &&
            box_space_interaction_panel_ == BoxSpaceInteractionPanel::Game && game_box_browser_.gameBoxSpaceMode() &&
            game_save_box_viewport_) {
            const int box_index = game_box_browser_.gameBoxSpaceRowOffset() * 6 + box_space_pressed_cell_;
            box_space_pressed_cell_ = -1;
            box_space_box_move_hold_.cancel();
            box_space_box_move_source_box_index_ = -1;
            box_space_interaction_panel_ = BoxSpaceInteractionPanel::None;
            (void)openGameBoxFromBoxSpaceSelection(box_index);
        } else if (
            treat_as_click && box_space_pressed_cell_ >= 0 && box_space_pressed_cell_ < 30 &&
            box_space_interaction_panel_ == BoxSpaceInteractionPanel::Resort && resort_box_browser_.gameBoxSpaceMode() &&
            resort_box_viewport_) {
            const int box_index = resort_box_browser_.gameBoxSpaceRowOffset() * 6 + box_space_pressed_cell_;
            box_space_pressed_cell_ = -1;
            box_space_box_move_hold_.cancel();
            box_space_box_move_source_box_index_ = -1;
            box_space_interaction_panel_ = BoxSpaceInteractionPanel::None;
            (void)openResortBoxFromBoxSpaceSelection(box_index);
        } else {
            box_space_pressed_cell_ = -1;
            box_space_box_move_hold_.cancel();
            box_space_box_move_source_box_index_ = -1;
            box_space_interaction_panel_ = BoxSpaceInteractionPanel::None;
        }
        return true;
    }

    if (held_move_.heldBox()) {
        int target_box_index = -1;
        bool target_resort = false;
        if (const auto picked = focusNodeAtPointer(logical_x, logical_y)) {
            if (*picked >= 2000 && *picked <= 2029 && game_box_browser_.gameBoxSpaceMode()) {
                target_box_index = game_box_browser_.gameBoxSpaceRowOffset() * 6 + (*picked - 2000);
                target_resort = false;
            } else if (*picked >= 1000 && *picked <= 1029 && resort_box_browser_.gameBoxSpaceMode()) {
                target_box_index = resort_box_browser_.gameBoxSpaceRowOffset() * 6 + (*picked - 1000);
                target_resort = true;
            }
        }
        const auto* hb = held_move_.heldBox();
        const int from = hb->source_box_index;
        const auto from_panel = hb->source_panel;
        held_move_.clear();
        if (from >= 0 && target_box_index >= 0) {
            if (from_panel == transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Game && !target_resort) {
                (void)swapGamePcBoxes(from, target_box_index);
            } else if (
                from_panel == transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Resort && target_resort) {
                (void)swapResortPcBoxes(from, target_box_index);
            } else if (
                from_panel == transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Game && target_resort) {
                (void)swapGameAndResortPcBoxes(from, target_box_index);
            } else if (
                from_panel == transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Resort && !target_resort) {
                (void)swapGameAndResortPcBoxes(target_box_index, from);
            }
        }
        refreshGameBoxViewportModel();
        refreshResortBoxViewportModel();
        requestPutdownSfx();
        return true;
    }
    // If the item action menu is open, consume pointer release to avoid treating the press as a click-through.
    if (item_action_menu_.visible()) {
        return true;
    }
    if (!dropdown_lmb_down_in_panel_) {
        return false;
    }
    dropdown_lmb_down_in_panel_ = false;

    if (box_name_dropdown_style_.enabled && game_box_browser_.dropdownOpenTarget() &&
        game_box_browser_.dropdownExpandT() > 0.05 &&
        game_pc_boxes_.size() >= 2) {
        constexpr double kClickDragThresholdPx = 6.0;
        const bool treat_as_click = dropdown_lmb_drag_accum_ < kClickDragThresholdPx;
        dropdown_lmb_drag_accum_ = 0.0;

        if (treat_as_click) {
            SDL_Rect outer{};
            int list_h = 0;
            int list_clip_y = 0;
            if (computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
                const bool in_outer = logical_x >= outer.x && logical_x < outer.x + outer.w && logical_y >= outer.y &&
                                       logical_y < outer.y + outer.h;
                if (in_outer) {
                    if (const std::optional<int> row = dropdownRowIndexAtScreen(logical_x, logical_y)) {
                        stepDropdownHighlight(*row - game_box_browser_.dropdownHighlightIndex());
                        applyGameBoxDropdownSelection();
                        return true;
                    }
                }
            }
        }
    } else if (
        box_name_dropdown_style_.enabled && resort_box_browser_.dropdownOpenTarget() &&
        resort_box_browser_.dropdownExpandT() > 0.05 &&
        resort_pc_boxes_.size() >= 2) {
        constexpr double kClickDragThresholdPx = 6.0;
        const bool treat_as_click = dropdown_lmb_drag_accum_ < kClickDragThresholdPx;
        dropdown_lmb_drag_accum_ = 0.0;

        if (treat_as_click) {
            SDL_Rect outer{};
            int list_h = 0;
            int list_clip_y = 0;
            if (computeResortBoxDropdownOuterRect(outer, static_cast<float>(resort_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
                const bool in_outer = logical_x >= outer.x && logical_x < outer.x + outer.w && logical_y >= outer.y &&
                                       logical_y < outer.y + outer.h;
                if (in_outer) {
                    if (const std::optional<int> row = resortDropdownRowIndexAtScreen(logical_x, logical_y)) {
                        stepResortDropdownHighlight(*row - resort_box_browser_.dropdownHighlightIndex());
                        applyResortBoxDropdownSelection();
                        return true;
                    }
                }
            }
        }
    } else {
        dropdown_lmb_drag_accum_ = 0.0;
    }
    return false;
}

} // namespace pr

