#include "ui/TransferSystemScreen.hpp"

#include <SDL.h>

namespace pr {

bool TransferSystemScreen::handlePointerPressed(int logical_x, int logical_y) {
    last_pointer_position_ = SDL_Point{logical_x, logical_y};
    pointer_drag_pickup_pending_ = false;
    pointer_drag_item_pickup_pending_ = false;

    if (exit_save_modal_open_) {
        return handleExitSaveModalPointerPressed(logical_x, logical_y);
    }
    if (box_rename_modal_open_) {
        return handleBoxRenameModalPointerPressed(logical_x, logical_y);
    }

    // Dropdown is modal: while open, block other interactions.
    if (box_name_dropdown_style_.enabled && game_box_browser_.dropdownOpenTarget() && game_box_browser_.dropdownExpandT() > 0.08 &&
        game_pc_boxes_.size() >= 2) {
        SDL_Rect outer{};
        int list_h = 0;
        int list_clip_y = 0;
        const bool has_outer =
            computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_browser_.dropdownExpandT()), list_h, list_clip_y);
        const bool in_outer =
            has_outer && logical_x >= outer.x && logical_x < outer.x + outer.w && logical_y >= outer.y &&
            logical_y < outer.y + outer.h;
        if (!in_outer && !hitTestGameBoxNamePlate(logical_x, logical_y)) {
            closeGameBoxDropdown();
            ui_state_.requestButtonSfx();
            return true;
        }
    }
    if (box_name_dropdown_style_.enabled && resort_box_browser_.dropdownOpenTarget() && resort_box_browser_.dropdownExpandT() > 0.08 &&
        resort_pc_boxes_.size() >= 2) {
        SDL_Rect outer{};
        int list_h = 0;
        int list_clip_y = 0;
        const bool has_outer =
            computeResortBoxDropdownOuterRect(outer, static_cast<float>(resort_box_browser_.dropdownExpandT()), list_h, list_clip_y);
        const bool in_outer =
            has_outer && logical_x >= outer.x && logical_x < outer.x + outer.w && logical_y >= outer.y &&
            logical_y < outer.y + outer.h;
        if (!in_outer && !hitTestResortBoxNamePlate(logical_x, logical_y)) {
            closeResortBoxDropdown();
            ui_state_.requestButtonSfx();
            return true;
        }
    }
    if (multi_pokemon_move_.active()) {
        multi_pokemon_move_.updatePointer(last_pointer_position_, window_config_.virtual_width, window_config_.virtual_height);
        if (handleDropdownPointerPressed(logical_x, logical_y) ||
            handleResortDropdownPointerPressed(logical_x, logical_y) ||
            handleGameBoxSpacePointerPressed(logical_x, logical_y) ||
            handleResortBoxSpacePointerPressed(logical_x, logical_y) ||
            handleGameBoxNavigationPointerPressed(logical_x, logical_y) ||
            handleResortBoxNavigationPointerPressed(logical_x, logical_y)) {
            return true;
        }
        if (hitTestToolCarousel(logical_x, logical_y) || hitTestPillTrack(logical_x, logical_y)) {
            return true;
        }
        if (game_box_browser_.gameBoxSpaceMode()) {
            if (const auto picked = focusNodeAtPointer(logical_x, logical_y)) {
                if (*picked >= 2000 && *picked <= 2029) {
                    const int cell = *picked - 2000;
                    const int box_index = game_box_browser_.gameBoxSpaceRowOffset() * 6 + cell;
                    if (box_index >= 0 && box_index < static_cast<int>(game_pc_boxes_.size())) {
                        focus_.setCurrent(*picked);
                        const bool dropped = dropHeldMultiPokemonIntoFirstEmptySlotsInBox(box_index);
                        if (!dropped) {
                            triggerHeldSpriteRejectFeedback();
                        }
                        return true;
                    }
                }
            }
        }
        if (resort_box_browser_.gameBoxSpaceMode()) {
            if (const auto picked = focusNodeAtPointer(logical_x, logical_y)) {
                if (*picked >= 1000 && *picked <= 1029) {
                    const int cell = *picked - 1000;
                    const int box_index = resort_box_browser_.gameBoxSpaceRowOffset() * 6 + cell;
                    if (box_index >= 0 && box_index < static_cast<int>(resort_pc_boxes_.size())) {
                        focus_.setCurrent(*picked);
                        const bool dropped = dropHeldMultiPokemonIntoFirstEmptyResortBox(box_index);
                        if (!dropped) {
                            triggerHeldSpriteRejectFeedback();
                        }
                        return true;
                    }
                }
            }
        }
        if (const auto target = slotRefAtPointer(logical_x, logical_y)) {
            const bool dropped = dropHeldMultiPokemonAt(*target);
            if (!dropped) {
                triggerHeldSpriteRejectFeedback();
            }
            return true;
        }
        return true;
    }

    if (pokemon_move_.active()) {
        pokemon_move_.updatePointer(last_pointer_position_, window_config_.virtual_width, window_config_.virtual_height);
        if (handleDropdownPointerPressed(logical_x, logical_y) ||
            handleResortDropdownPointerPressed(logical_x, logical_y) ||
            handleGameBoxSpacePointerPressed(logical_x, logical_y) ||
            handleResortBoxSpacePointerPressed(logical_x, logical_y) ||
            handleGameBoxNavigationPointerPressed(logical_x, logical_y) ||
            handleResortBoxNavigationPointerPressed(logical_x, logical_y)) {
            return true;
        }
        if (hitTestToolCarousel(logical_x, logical_y) || hitTestPillTrack(logical_x, logical_y)) {
            return true;
        }
        if (const auto target = slotRefAtPointer(logical_x, logical_y)) {
            return dropHeldPokemonAt(*target);
        }
        return true;
    }

    if (held_move_.heldItem()) {
        held_move_.updatePointer(last_pointer_position_, window_config_.virtual_width, window_config_.virtual_height);
        if (handleDropdownPointerPressed(logical_x, logical_y) ||
            handleResortDropdownPointerPressed(logical_x, logical_y) ||
            handleGameBoxSpacePointerPressed(logical_x, logical_y) ||
            handleResortBoxSpacePointerPressed(logical_x, logical_y) ||
            handleGameBoxNavigationPointerPressed(logical_x, logical_y) ||
            handleResortBoxNavigationPointerPressed(logical_x, logical_y)) {
            return true;
        }
        if (hitTestToolCarousel(logical_x, logical_y) || hitTestPillTrack(logical_x, logical_y)) {
            return true;
        }
        if (const auto target = slotRefAtPointer(logical_x, logical_y)) {
            using Move = transfer_system::PokemonMoveController;
            PcSlotSpecies* dst = mutablePokemonAt(*target);
            if (dst && dst->occupied()) {
                const auto* held = held_move_.heldItem();
                if (dst->held_item_id > 0) {
                    const int next_item_id = dst->held_item_id;
                    std::string next_item_name = dst->held_item_name;
                    if (target->panel == Move::Panel::Game) {
                        if (!syncGamePcSlotHeldItemPayload(*dst, held->item_id, held->item_name)) {
                            ui_state_.requestErrorSfx();
                            return true;
                        }
                        markGameBoxesDirty();
                    } else {
                        dst->held_item_id = held->item_id;
                        dst->held_item_name = held->item_name;
                        markResortBoxesDirty();
                    }
                    held_move_.swapHeldItemWith(
                        next_item_id,
                        std::move(next_item_name),
                        transfer_system::move::HeldMoveController::PokemonSlotRef{
                            target->panel == Move::Panel::Game
                                ? transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Game
                                : transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Resort,
                            target->box_index,
                            target->slot_index});
                    refreshResortBoxViewportModel();
                    refreshGameBoxViewportModel();
                    requestPickupSfx();
                } else {
                    if (target->panel == Move::Panel::Game) {
                        if (!syncGamePcSlotHeldItemPayload(*dst, held->item_id, held->item_name)) {
                            ui_state_.requestErrorSfx();
                            return true;
                        }
                        markGameBoxesDirty();
                    } else {
                        dst->held_item_id = held->item_id;
                        dst->held_item_name = held->item_name;
                        markResortBoxesDirty();
                    }
                    held_move_.clear();
                    refreshResortBoxViewportModel();
                    refreshGameBoxViewportModel();
                    requestPutdownSfx();
                }
            }
            return true;
        }
        return true;
    }

    if (handlePokemonActionMenuPointerPressed(logical_x, logical_y)) {
        return true;
    }
    if (handleItemActionMenuPointerPressed(logical_x, logical_y)) {
        return true;
    }

    if (handleDropdownPointerPressed(logical_x, logical_y)) {
        return true;
    }
    if (handleResortDropdownPointerPressed(logical_x, logical_y)) {
        return true;
    }

    if (const std::optional<FocusNodeId> picked = focusNodeAtPointer(logical_x, logical_y)) {
        focus_.setCurrent(*picked);
        // Pointer input puts us in "mouse mode" (no legacy yellow rectangle).
        selection_cursor_hidden_after_mouse_ = true;
        speech_hover_active_ = true;
        if (*picked == 5000) {
            ui_state_.requestButtonSfx();
            onBackPressed();
            return true;
        }
    }

    if (multiPokemonToolActive() && selection_cursor_hidden_after_mouse_ && panelsReadyForInteraction() &&
        !game_box_browser_.dropdownOpenTarget() && !resort_box_browser_.dropdownOpenTarget() &&
        !multi_pokemon_move_.active() && !pokemon_move_.active()) {
        const std::optional<FocusNodeId> picked = focusNodeAtPointer(logical_x, logical_y);
        if (picked &&
            ((*picked >= 1000 && *picked <= 1029 && !resort_box_browser_.gameBoxSpaceMode()) ||
             (*picked >= 2000 && *picked <= 2029 && !game_box_browser_.gameBoxSpaceMode()))) {
            multi_select_drag_active_ = true;
            multi_select_from_game_ = *picked >= 2000;
            multi_select_drag_start_ = last_pointer_position_;
            multi_select_drag_current_ = last_pointer_position_;
            multi_select_drag_rect_ = SDL_Rect{logical_x, logical_y, 1, 1};
            return true;
        }
    }

    // Mouse mode: allow click-drag pickup directly from a slot with the normal tool.
    if (normalPokemonToolActive() && selection_cursor_hidden_after_mouse_ && panelsReadyForInteraction() &&
        !game_box_browser_.dropdownOpenTarget() && !resort_box_browser_.dropdownOpenTarget() &&
        !pokemon_action_menu_.visible() && !pokemon_move_.active()) {
        auto in = [](int px, int py, const SDL_Rect& r) {
            return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
        };
        SDL_Rect r{};
        if (game_save_box_viewport_ && !game_box_browser_.gameBoxSpaceMode()) {
            if (!pointerOverExpandedGameDropdown(logical_x, logical_y)) {
                for (int i = 0; i < 30; ++i) {
                    if (game_save_box_viewport_->getSlotBounds(i, r) && in(logical_x, logical_y, r) && gameSaveSlotHasSpecies(i)) {
                        pointer_drag_pickup_pending_ = true;
                        pointer_drag_pickup_from_game_ = true;
                        pointer_drag_pickup_slot_index_ = i;
                        pointer_drag_pickup_bounds_ = r;
                        pointer_drag_pickup_start_ = last_pointer_position_;
                        return true;
                    }
                }
            }
        }
        if (resort_box_viewport_ && !resort_box_browser_.gameBoxSpaceMode()) {
            if (!pointerOverExpandedResortDropdown(logical_x, logical_y)) {
                for (int i = 0; i < 30; ++i) {
                    if (resort_box_viewport_->getSlotBounds(i, r) && in(logical_x, logical_y, r) && resortSlotHasSpecies(i)) {
                        pointer_drag_pickup_pending_ = true;
                        pointer_drag_pickup_from_game_ = false;
                        pointer_drag_pickup_slot_index_ = i;
                        pointer_drag_pickup_bounds_ = r;
                        pointer_drag_pickup_start_ = last_pointer_position_;
                        return true;
                    }
                }
            }
        }
    }

    // Mouse mode: allow click-drag pickup directly from a slot with the item tool.
    if (itemToolActive() && selection_cursor_hidden_after_mouse_ && panelsReadyForInteraction() &&
        !game_box_browser_.dropdownOpenTarget() && !resort_box_browser_.dropdownOpenTarget() &&
        !item_action_menu_.visible() && !held_move_.heldItem() && !pokemon_move_.active()) {
        auto in = [](int px, int py, const SDL_Rect& r) {
            return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
        };
        SDL_Rect r{};
        if (game_save_box_viewport_ && !game_box_browser_.gameBoxSpaceMode()) {
            if (!pointerOverExpandedGameDropdown(logical_x, logical_y)) {
                for (int i = 0; i < 30; ++i) {
                    if (game_save_box_viewport_->getSlotBounds(i, r) && in(logical_x, logical_y, r) && gameSlotHasHeldItem(i)) {
                        pointer_drag_item_pickup_pending_ = true;
                        pointer_drag_item_pickup_from_game_ = true;
                        pointer_drag_item_pickup_slot_index_ = i;
                        pointer_drag_item_pickup_bounds_ = r;
                        pointer_drag_item_pickup_start_ = last_pointer_position_;
                        return true;
                    }
                }
            }
        }
        if (resort_box_viewport_ && !resort_box_browser_.gameBoxSpaceMode()) {
            if (!pointerOverExpandedResortDropdown(logical_x, logical_y)) {
                const int rbi = resort_box_browser_.gameBoxIndex();
                if (rbi >= 0 && rbi < static_cast<int>(resort_pc_boxes_.size())) {
                    const auto& rs = resort_pc_boxes_[static_cast<std::size_t>(rbi)].slots;
                    for (int i = 0; i < 30; ++i) {
                        if (resort_box_viewport_->getSlotBounds(i, r) && in(logical_x, logical_y, r)) {
                            if (i >= 0 && i < static_cast<int>(rs.size()) && rs[static_cast<std::size_t>(i)].occupied() &&
                                rs[static_cast<std::size_t>(i)].held_item_id > 0) {
                                pointer_drag_item_pickup_pending_ = true;
                                pointer_drag_item_pickup_from_game_ = false;
                                pointer_drag_item_pickup_slot_index_ = i;
                                pointer_drag_item_pickup_bounds_ = r;
                                pointer_drag_item_pickup_start_ = last_pointer_position_;
                                return true;
                            }
                        }
                    }
                }
            }
        }
    }

    if (handlePokemonSlotActionPointerPressed(logical_x, logical_y)) {
        return true;
    }
    if (handleItemSlotActionPointerPressed(logical_x, logical_y)) {
        return true;
    }

    if (handleGameBoxSpacePointerPressed(logical_x, logical_y)) {
        return true;
    }
    if (handleResortBoxSpacePointerPressed(logical_x, logical_y)) {
        return true;
    }

    if (handleGameBoxNavigationPointerPressed(logical_x, logical_y)) {
        return true;
    }

    if (handleResortBoxNavigationPointerPressed(logical_x, logical_y)) {
        return true;
    }

    if (ui_state_.panelsReveal() > 0.02 && !carouselSlideAnimating() && hitTestToolCarousel(logical_x, logical_y)) {
        const int vx = carousel_style_.offset_from_left_wall +
            (exit_button_enabled_ ? (carousel_style_.viewport_height + exit_button_gap_pixels_) : 0);
        const int vw = carousel_style_.viewport_width;
        const int rel = logical_x - vx;
        if (rel * 2 < vw) {
            cycleToolCarousel(-1);
        } else {
            cycleToolCarousel(1);
        }
        ui_state_.requestButtonSfx();
        return true;
    }
    if (hitTestPillTrack(logical_x, logical_y)) {
        togglePillTarget();
        return true;
    }
    return false;
}

} // namespace pr

