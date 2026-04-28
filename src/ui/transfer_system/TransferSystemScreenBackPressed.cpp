#include "ui/TransferSystemScreen.hpp"

#include <SDL.h>

namespace pr {

void TransferSystemScreen::onBackPressed() {
    if (exit_save_modal_open_) {
        // Exit-save modal consumes Back: treat it as "continue box operations".
        closeExitSaveModal();
        ui_state_.requestButtonSfx();
        return;
    }
    if (box_rename_modal_open_) {
        if (box_rename_editing_) {
            box_rename_editing_ = false;
            SDL_StopTextInput();
            ui_state_.requestButtonSfx();
            return;
        }
        closeBoxRenameModal(false);
        ui_state_.requestButtonSfx();
        return;
    }
    if (keyboard_multi_marquee_active_) {
        keyboard_multi_marquee_active_ = false;
        ui_state_.requestButtonSfx();
        return;
    }
    if (held_move_.heldItem()) {
        // Cancel item move: always send the currently-held item back to the original pickup Pokemon.
        using Move = transfer_system::PokemonMoveController;
        const auto* held = held_move_.heldItem();
        const Move::SlotRef origin{
            held->origin_slot.panel == transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Game
                ? Move::Panel::Game
                : Move::Panel::Resort,
            held->origin_slot.box_index,
            held->origin_slot.slot_index};
        if (PcSlotSpecies* src = mutablePokemonAt(origin)) {
            if (src->occupied()) {
                src->held_item_id = held->item_id;
                src->held_item_name = held->item_name;
            }
        }
        held_move_.clear();
        refreshResortBoxViewportModel();
        refreshGameBoxViewportModel();
        requestPutdownSfx();
        return;
    }
    if (held_move_.heldBox() || box_space_box_move_hold_.active) {
        held_move_.clear();
        box_space_box_move_hold_.cancel();
        box_space_box_move_source_box_index_ = -1;
        refreshGameBoxViewportModel();
        refreshResortBoxViewportModel();
        requestPutdownSfx();
        return;
    }
    if (multi_pokemon_move_.active()) {
        (void)cancelHeldMultiPokemonMove();
        return;
    }
    if (pokemon_move_.active()) {
        (void)cancelHeldPokemonMove();
        return;
    }
    if (pokemon_action_menu_.visible()) {
        closePokemonActionMenu();
        return;
    }
    if (item_action_menu_.visible()) {
        item_action_menu_.close();
        return;
    }
    if (game_box_browser_.dropdownOpenTarget()) {
        closeGameBoxDropdown();
        return;
    }
    if (resort_box_browser_.dropdownOpenTarget()) {
        closeResortBoxDropdown();
        return;
    }
    if (game_boxes_dirty_) {
        openExitSaveModal();
        return;
    }
    // Pull UI away before returning.
    ui_state_.startExit();
}

} // namespace pr

