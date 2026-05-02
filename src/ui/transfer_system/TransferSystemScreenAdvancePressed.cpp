#include "ui/TransferSystemScreen.hpp"

#include <SDL.h>

namespace pr {

void TransferSystemScreen::onAdvancePressed() {
    selection_cursor_hidden_after_mouse_ = false;
    if (exit_save_modal_open_) {
        activateExitSaveModalRow(exit_save_modal_selected_row_);
        ui_state_.requestButtonSfx();
        return;
    }
    if (box_rename_modal_open_) {
        syncBoxRenameModalLayout();
        if (box_rename_editing_) {
            box_rename_editing_ = false;
            SDL_StopTextInput();
            box_rename_focus_slot_ = BoxRenameFocusSlot::Confirm;
            ui_state_.requestButtonSfx();
            return;
        }
        switch (box_rename_focus_slot_) {
            case BoxRenameFocusSlot::Field:
                box_rename_editing_ = true;
                SDL_StartTextInput();
                SDL_SetTextInputRect(&box_rename_text_field_rect_virt_);
                break;
            case BoxRenameFocusSlot::Confirm:
                closeBoxRenameModal(true);
                break;
            case BoxRenameFocusSlot::Cancel:
                closeBoxRenameModal(false);
                break;
        }
        ui_state_.requestButtonSfx();
        return;
    }
    if (held_move_.heldBox() && !pokemon_move_.active()) {
        if (dropdownAcceptsNavigation()) {
            // While holding a Box Space box, Accept should still confirm dropdown choices.
            applyActiveDropdownSelection();
            return;
        }
        const auto* hb = held_move_.heldBox();
        const int from = hb->source_box_index;
        const auto src_panel = hb->source_panel;
        const auto game_tgt = focusedBoxSpaceBoxIndex();
        const auto resort_tgt = focusedResortBoxSpaceBoxIndex();

        if (src_panel == transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Game) {
            if (game_tgt.has_value()) {
                held_move_.clear();
                (void)swapGamePcBoxes(from, *game_tgt);
                refreshGameBoxViewportModel();
                refreshResortBoxViewportModel();
                requestPutdownSfx();
                return;
            }
            if (resort_tgt.has_value()) {
                if (!swapGameAndResortPcBoxes(from, *resort_tgt)) {
                    held_move_.clear();
                    refreshGameBoxViewportModel();
                    refreshResortBoxViewportModel();
                    triggerHeldSpriteRejectFeedback();
                    return;
                }
                held_move_.clear();
                refreshGameBoxViewportModel();
                refreshResortBoxViewportModel();
                requestPutdownSfx();
                return;
            }
        } else {
            if (resort_tgt.has_value()) {
                held_move_.clear();
                (void)swapResortPcBoxes(from, *resort_tgt);
                refreshGameBoxViewportModel();
                refreshResortBoxViewportModel();
                requestPutdownSfx();
                return;
            }
            if (game_tgt.has_value()) {
                if (!swapGameAndResortPcBoxes(*game_tgt, from)) {
                    held_move_.clear();
                    refreshGameBoxViewportModel();
                    refreshResortBoxViewportModel();
                    triggerHeldSpriteRejectFeedback();
                    return;
                }
                held_move_.clear();
                refreshGameBoxViewportModel();
                refreshResortBoxViewportModel();
                requestPutdownSfx();
                return;
            }
        }
        return;
    }
    if (multi_pokemon_move_.active()) {
        if (dropdownAcceptsNavigation()) {
            applyActiveDropdownSelection();
            return;
        }
        if (game_box_browser_.gameBoxSpaceMode()) {
            const FocusNodeId cur = focus_.current();
            if (cur >= 2000 && cur <= 2029) {
                const int box_index =
                    game_box_browser_.gameBoxSpaceRowOffset() * 6 + (cur - 2000);
                if (box_index >= 0 && box_index < static_cast<int>(game_pc_boxes_.size())) {
                    if (!dropHeldMultiPokemonIntoFirstEmptySlotsInBox(box_index)) {
                        triggerHeldSpriteRejectFeedback();
                    }
                    return;
                }
            }
            if (activateFocusedGameSlot()) {
                return;
            }
        } else if (resort_box_browser_.gameBoxSpaceMode()) {
            const FocusNodeId cur = focus_.current();
            if (cur >= 1000 && cur <= 1029) {
                const int box_index =
                    resort_box_browser_.gameBoxSpaceRowOffset() * 6 + (cur - 1000);
                if (box_index >= 0 && box_index < static_cast<int>(resort_pc_boxes_.size())) {
                    if (!dropHeldMultiPokemonIntoFirstEmptyResortBox(box_index)) {
                        triggerHeldSpriteRejectFeedback();
                    }
                    return;
                }
            }
            if (activateFocusedResortSlot()) {
                return;
            }
        } else if (const auto target = slotRefForFocus(focus_.current())) {
            if (!dropHeldMultiPokemonAt(*target)) {
                triggerHeldSpriteRejectFeedback();
            }
            return;
        }
        if (focus_.current() == 2101) {
            advanceGameBox(-1);
            return;
        }
        if (focus_.current() == 2103) {
            advanceGameBox(1);
            return;
        }
        if (box_name_dropdown_style_.enabled && !game_box_browser_.dropdownOpenTarget() && focus_.current() == 2102 &&
            game_pc_boxes_.size() >= 2) {
            toggleGameBoxDropdown();
            return;
        }
        if (focus_.current() == 2110) {
            setGameBoxSpaceMode(!game_box_browser_.gameBoxSpaceMode());
            closeGameBoxDropdown();
            return;
        }
        if (focus_.current() == 1101) {
            advanceResortBox(-1);
            return;
        }
        if (focus_.current() == 1103) {
            advanceResortBox(1);
            return;
        }
        if (box_name_dropdown_style_.enabled && !resort_box_browser_.dropdownOpenTarget() && focus_.current() == 1102 &&
            resort_pc_boxes_.size() >= 2) {
            toggleResortBoxDropdown();
            return;
        }
        if (focus_.current() == 1110) {
            setResortBoxSpaceMode(!resort_box_browser_.gameBoxSpaceMode());
            closeResortBoxDropdown();
            return;
        }
        return;
    }
    if (pokemon_move_.active()) {
        if (dropdownAcceptsNavigation()) {
            applyActiveDropdownSelection();
            return;
        }
        if (const auto target = slotRefForFocus(focus_.current())) {
            (void)dropHeldPokemonAt(*target);
            return;
        }
        if (game_box_browser_.gameBoxSpaceMode()) {
            if (activateFocusedGameSlot()) {
                return;
            }
        }
        if (resort_box_browser_.gameBoxSpaceMode()) {
            if (activateFocusedResortSlot()) {
                return;
            }
        }
        if (focus_.current() == 2101) {
            advanceGameBox(-1);
            return;
        }
        if (focus_.current() == 2103) {
            advanceGameBox(1);
            return;
        }
        if (box_name_dropdown_style_.enabled && !game_box_browser_.dropdownOpenTarget() && focus_.current() == 2102 &&
            game_pc_boxes_.size() >= 2) {
            toggleGameBoxDropdown();
            return;
        }
        if (focus_.current() == 2110) {
            setGameBoxSpaceMode(!game_box_browser_.gameBoxSpaceMode());
            closeGameBoxDropdown();
            return;
        }
        if (focus_.current() == 1101) {
            advanceResortBox(-1);
            return;
        }
        if (focus_.current() == 1103) {
            advanceResortBox(1);
            return;
        }
        if (box_name_dropdown_style_.enabled && !resort_box_browser_.dropdownOpenTarget() && focus_.current() == 1102 &&
            resort_pc_boxes_.size() >= 2) {
            toggleResortBoxDropdown();
            return;
        }
        if (focus_.current() == 1110) {
            setResortBoxSpaceMode(!resort_box_browser_.gameBoxSpaceMode());
            closeResortBoxDropdown();
            return;
        }
        return;
    }
    if (held_move_.heldItem()) {
        if (dropdownAcceptsNavigation()) {
            applyActiveDropdownSelection();
            return;
        }
        // Holding an item: drop onto a focused Pokemon slot if it has no held item.
        if (const auto target = slotRefForFocus(focus_.current())) {
            using Move = transfer_system::PokemonMoveController;
            const int slot = target->slot_index;
            const int box_index = target->box_index;
            const bool in_game = target->panel == Move::Panel::Game;
            PcSlotSpecies* dst = mutablePokemonAt(transfer_system::PokemonMoveController::SlotRef{
                in_game ? Move::Panel::Game : Move::Panel::Resort, box_index, slot});
            if (dst && dst->occupied()) {
                const auto* held = held_move_.heldItem();
                if (dst->held_item_id > 0) {
                    // Swap: target item becomes the new held item and returns to this slot on cancel.
                    const int next_item_id = dst->held_item_id;
                    std::string next_item_name = dst->held_item_name;
                    if (in_game) {
                        if (!syncGamePcSlotHeldItemPayload(*dst, held->item_id, held->item_name)) {
                            ui_state_.requestErrorSfx();
                            return;
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
                            in_game ? transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Game
                                    : transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Resort,
                            box_index,
                            slot});
                    refreshResortBoxViewportModel();
                    refreshGameBoxViewportModel();
                    requestPickupSfx();
                    return;
                }
                if (dst->held_item_id <= 0) {
                    if (in_game) {
                        if (!syncGamePcSlotHeldItemPayload(*dst, held->item_id, held->item_name)) {
                            ui_state_.requestErrorSfx();
                            return;
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
                    return;
                }
            }
        }
        // Allow box navigation / Box Space open behavior even while holding an item.
        if (game_box_browser_.gameBoxSpaceMode()) {
            if (activateFocusedGameSlot()) {
                return;
            }
        }
        if (resort_box_browser_.gameBoxSpaceMode()) {
            if (activateFocusedResortSlot()) {
                return;
            }
        }
        if (focus_.current() == 2101) {
            advanceGameBox(-1);
            return;
        }
        if (focus_.current() == 2103) {
            advanceGameBox(1);
            return;
        }
        if (box_name_dropdown_style_.enabled && !game_box_browser_.dropdownOpenTarget() && focus_.current() == 2102 &&
            game_pc_boxes_.size() >= 2) {
            toggleGameBoxDropdown();
            return;
        }
        if (focus_.current() == 2110) {
            setGameBoxSpaceMode(!game_box_browser_.gameBoxSpaceMode());
            closeGameBoxDropdown();
            return;
        }
        if (focus_.current() == 1101) {
            advanceResortBox(-1);
            return;
        }
        if (focus_.current() == 1103) {
            advanceResortBox(1);
            return;
        }
        if (box_name_dropdown_style_.enabled && !resort_box_browser_.dropdownOpenTarget() && focus_.current() == 1102 &&
            resort_pc_boxes_.size() >= 2) {
            toggleResortBoxDropdown();
            return;
        }
        if (focus_.current() == 1110) {
            setResortBoxSpaceMode(!resort_box_browser_.gameBoxSpaceMode());
            closeResortBoxDropdown();
            return;
        }
        return;
    }
    if (pokemon_action_menu_.visible()) {
        activatePokemonActionMenuRow(pokemon_action_menu_.selectedRow());
        return;
    }
    if (item_action_menu_.visible()) {
        ui_state_.requestButtonSfx();
        const auto action = item_action_menu_.actionForRow(item_action_menu_.selectedRow());
        if (action == transfer_system::ItemActionMenuController::Action::MoveItem) {
            using Move = transfer_system::PokemonMoveController;
            const Move::SlotRef ref{
                item_action_menu_.fromGameBox() ? Move::Panel::Game : Move::Panel::Resort,
                item_action_menu_.fromGameBox() ? game_box_browser_.gameBoxIndex() : resort_box_browser_.gameBoxIndex(),
                item_action_menu_.slotIndex()};
            PcSlotSpecies* src = mutablePokemonAt(ref);
            if (src && src->occupied() && src->held_item_id > 0) {
                held_move_.pickUpItem(
                    src->held_item_id,
                    src->held_item_name,
                    transfer_system::move::HeldMoveController::PokemonSlotRef{
                        item_action_menu_.fromGameBox()
                            ? transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Game
                            : transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Resort,
                        item_action_menu_.fromGameBox() ? game_box_browser_.gameBoxIndex() : resort_box_browser_.gameBoxIndex(),
                        item_action_menu_.slotIndex()},
                    transfer_system::move::HeldMoveController::InputMode::Keyboard,
                    last_pointer_position_);
                if (item_action_menu_.fromGameBox()) {
                    if (!syncGamePcSlotHeldItemPayload(*src, -1, std::string{})) {
                        ui_state_.requestErrorSfx();
                        held_move_.clear();
                        item_action_menu_.close();
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
            item_action_menu_.close();
        } else if (action == transfer_system::ItemActionMenuController::Action::PutAway) {
            item_action_menu_.goToPutAwayPage();
        } else if (action == transfer_system::ItemActionMenuController::Action::Back) {
            item_action_menu_.goToRootPage();
        } else if (
            action == transfer_system::ItemActionMenuController::Action::PutAwayResort ||
            action == transfer_system::ItemActionMenuController::Action::PutAwayGame) {
            // Not implemented yet: bag/storage destinations.
            item_action_menu_.close();
        } else {
            item_action_menu_.close();
        }
        return;
    }
    if (dropdownAcceptsNavigation()) {
        applyActiveDropdownSelection();
        return;
    }
    if (multiPokemonToolActive()) {
        if (keyboard_multi_marquee_active_) {
            const auto refs = keyboardMultiMarqueeOccupiedRefs();
            if (!refs.empty()) {
                if (beginMultiPokemonMoveFromSlots(
                        refs,
                        transfer_system::MultiPokemonMoveController::InputMode::Keyboard,
                        last_pointer_position_)) {
                    keyboard_multi_marquee_active_ = false;
                    return;
                }
            }
            ui_state_.requestErrorSfx();
            keyboard_multi_marquee_active_ = false;
            return;
        }
        if (const auto ref = slotRefForFocus(focus_.current())) {
            const bool game_slot =
                ref->panel == transfer_system::PokemonMoveController::Panel::Game &&
                gameSaveSlotHasSpecies(ref->slot_index);
            const bool resort_slot =
                ref->panel == transfer_system::PokemonMoveController::Panel::Resort &&
                resortSlotHasSpecies(ref->slot_index);
            if (game_slot || resort_slot) {
                keyboard_multi_marquee_active_ = true;
                keyboard_multi_marquee_from_game_ =
                    ref->panel == transfer_system::PokemonMoveController::Panel::Game;
                keyboard_multi_marquee_anchor_slot_ = ref->slot_index;
                keyboard_multi_marquee_corner_slot_ = ref->slot_index;
                ui_state_.requestButtonSfx();
                return;
            }
        }
    }
    if (swapToolActive()) {
        if (const auto ref = slotRefForFocus(focus_.current())) {
            if (beginPokemonMoveFromSlot(
                    *ref,
                    transfer_system::PokemonMoveController::InputMode::Keyboard,
                    transfer_system::PokemonMoveController::PickupSource::SwapTool,
                    last_pointer_position_)) {
                return;
            }
        }
    }
    if (activateFocusedPokemonSlotActionMenu()) {
        return;
    }
    // Yellow tool: allow opening an item modal on slots with held items.
    if (itemToolActive()) {
        const FocusNodeId cur = focus_.current();
        SDL_Rect r{};
        if (cur >= 2000 && cur <= 2029 && game_save_box_viewport_) {
            if (game_box_browser_.gameBoxSpaceMode()) {
                return;
            }
            const int slot = cur - 2000;
            if (gameSlotHasHeldItem(slot) && game_save_box_viewport_->getSlotBounds(slot, r)) {
                item_action_menu_.setPutAwayGameLabel(selection_game_title_);
                item_action_menu_.open(true, slot, r);
                ui_state_.requestButtonSfx();
                return;
            }
        }
        if (cur >= 1000 && cur <= 1029 && resort_box_viewport_) {
            if (resort_box_browser_.gameBoxSpaceMode()) {
                return;
            }
            const int slot = cur - 1000;
            SDL_Rect rr{};
            const int rbi = resort_box_browser_.gameBoxIndex();
            if (rbi >= 0 && rbi < static_cast<int>(resort_pc_boxes_.size()) &&
                resort_box_viewport_->getSlotBounds(slot, rr)) {
                const auto& rs = resort_pc_boxes_[static_cast<std::size_t>(rbi)].slots;
                if (slot >= 0 && slot < static_cast<int>(rs.size()) && rs[static_cast<std::size_t>(slot)].occupied() &&
                    rs[static_cast<std::size_t>(slot)].held_item_id > 0) {
                    item_action_menu_.setPutAwayGameLabel(selection_game_title_);
                    item_action_menu_.open(false, slot, rr);
                    ui_state_.requestButtonSfx();
                    return;
                }
            }
        }
    }
    if (activateFocusedGameSlot()) {
        return;
    }
    if (activateFocusedResortSlot()) {
        return;
    }
    if (box_name_dropdown_style_.enabled && !game_box_browser_.dropdownOpenTarget() && focus_.current() == 2102 &&
        game_pc_boxes_.size() >= 2) {
        toggleGameBoxDropdown();
        return;
    }
    if (box_name_dropdown_style_.enabled && !resort_box_browser_.dropdownOpenTarget() && focus_.current() == 1102 &&
        resort_pc_boxes_.size() >= 2) {
        toggleResortBoxDropdown();
        return;
    }
    focus_.activate();
}

} // namespace pr

