#include "ui/TransferSystemScreen.hpp"

#include <SDL.h>

namespace pr {

PcSlotSpecies* TransferSystemScreen::mutablePokemonAt(const transfer_system::PokemonMoveController::SlotRef& ref) {
    using Move = transfer_system::PokemonMoveController;
    if (ref.slot_index < 0 || ref.slot_index >= 30) {
        return nullptr;
    }
    if (ref.panel == Move::Panel::Resort) {
        if (ref.box_index < 0 || ref.box_index >= static_cast<int>(resort_pc_boxes_.size())) {
            return nullptr;
        }
        auto& slots = resort_pc_boxes_[static_cast<std::size_t>(ref.box_index)].slots;
        if (ref.slot_index < 0 || ref.slot_index >= static_cast<int>(slots.size())) {
            return nullptr;
        }
        return &slots[static_cast<std::size_t>(ref.slot_index)];
    }
    if (ref.box_index < 0 || ref.box_index >= static_cast<int>(game_pc_boxes_.size())) {
        return nullptr;
    }
    if (!gameSaveSlotAccessible(ref.slot_index)) {
        return nullptr;
    }
    auto& slots = game_pc_boxes_[static_cast<std::size_t>(ref.box_index)].slots;
    if (ref.slot_index >= static_cast<int>(slots.size())) {
        return nullptr;
    }
    return &slots[static_cast<std::size_t>(ref.slot_index)];
}

const PcSlotSpecies* TransferSystemScreen::pokemonAt(const transfer_system::PokemonMoveController::SlotRef& ref) const {
    return const_cast<TransferSystemScreen*>(this)->mutablePokemonAt(ref);
}

void TransferSystemScreen::clearPokemonAt(const transfer_system::PokemonMoveController::SlotRef& ref) {
    if (PcSlotSpecies* slot = mutablePokemonAt(ref)) {
        *slot = PcSlotSpecies{};
        if (ref.panel == transfer_system::PokemonMoveController::Panel::Game) {
            markGameBoxesDirty();
        } else {
            markResortBoxesDirty();
        }
    }
}

void TransferSystemScreen::setPokemonAt(const transfer_system::PokemonMoveController::SlotRef& ref, PcSlotSpecies pokemon) {
    if (PcSlotSpecies* slot = mutablePokemonAt(ref)) {
        pokemon.present = true;
        pokemon.slot_index = ref.slot_index;
        pokemon.box_index = ref.box_index;
        pokemon.area = ref.panel == transfer_system::PokemonMoveController::Panel::Resort ? "resort" : "box";
        *slot = std::move(pokemon);
        if (ref.panel == transfer_system::PokemonMoveController::Panel::Game) {
            markGameBoxesDirty();
        } else {
            markResortBoxesDirty();
        }
    }
}

bool TransferSystemScreen::beginPokemonMoveFromSlot(
    const transfer_system::PokemonMoveController::SlotRef& ref,
    transfer_system::PokemonMoveController::InputMode input_mode,
    transfer_system::PokemonMoveController::PickupSource source,
    SDL_Point pointer) {
    if (pokemon_move_.active() || multi_pokemon_move_.active()) {
        return false;
    }
    const PcSlotSpecies* slot = pokemonAt(ref);
    if (!slot || !slot->occupied()) {
        return false;
    }
    pokemon_move_.pickUp(*slot, ref, input_mode, source, pointer);
    clearPokemonAt(ref);
    refreshHeldMoveSpriteTexture();
    refreshResortBoxViewportModel();
    refreshGameBoxViewportModel();
    pokemon_action_menu_.clear();
    requestPickupSfx();
    return true;
}

bool TransferSystemScreen::dropHeldPokemonAt(const transfer_system::PokemonMoveController::SlotRef& target) {
    using Move = transfer_system::PokemonMoveController;
    Move::HeldPokemon* held = pokemon_move_.held();
    if (!held) {
        return false;
    }
    PcSlotSpecies* target_slot = mutablePokemonAt(target);
    if (!target_slot) {
        return false;
    }

    const bool target_occupied = target_slot->occupied();
    PcSlotSpecies held_pokemon = held->pokemon;
    const Move::SlotRef return_slot = held->return_slot;
    const std::string held_pkrid_snapshot = held_pokemon.resort_pkrid;
    const std::string target_pkrid_before = target_occupied ? target_slot->resort_pkrid : "";
    const bool swap_into_hand =
        held->source == Move::PickupSource::SwapTool
            ? pokemon_action_menu_style_.swap_tool_swaps_into_hand
            : pokemon_action_menu_style_.modal_move_swaps_into_hand;

    if (!target_occupied) {
        setPokemonAt(target, std::move(held_pokemon));
        pokemon_move_.clear();
        held_move_sprite_tex_ = {};
        requestPutdownSfx();
    } else if (swap_into_hand) {
        PcSlotSpecies target_pokemon = *target_slot;
        setPokemonAt(target, std::move(held_pokemon));
        pokemon_move_.swapHeldWith(target_pokemon, return_slot);
        requestPickupSfx();
    } else {
        if (target != return_slot) {
            const PcSlotSpecies* return_pokemon = pokemonAt(return_slot);
            if (return_pokemon && return_pokemon->occupied()) {
                // Keep both Pokemon safe: if the configured return slot is unexpectedly occupied,
                // fall back to hand-swap semantics rather than overwriting anything.
                PcSlotSpecies target_pokemon = *target_slot;
                setPokemonAt(target, std::move(held_pokemon));
                pokemon_move_.swapHeldWith(target_pokemon, return_slot);
                requestPickupSfx();
                refreshResortBoxViewportModel();
                refreshGameBoxViewportModel();
                refreshHeldMoveSpriteTexture();
                if (!game_box_browser_.gameBoxSpaceMode()) {
                    if (target.panel == Move::Panel::Game) {
                        focus_.setCurrent(2000 + target.slot_index);
                    } else {
                        focus_.setCurrent(1000 + target.slot_index);
                    }
                    selection_cursor_hidden_after_mouse_ = false;
                }
                return true;
            }
            setPokemonAt(return_slot, *target_slot);
        }
        setPokemonAt(target, std::move(held_pokemon));
        pokemon_move_.clear();
        held_move_sprite_tex_ = {};
        requestPutdownSfx();
    }

    const bool persist_ok = persistResortPokemonDropToStorage(
        target,
        return_slot,
        target_occupied,
        swap_into_hand,
        held_pkrid_snapshot,
        target_pkrid_before);
    // Only revert for external-save → Resort import failures (return slot was cleared at pickup).
    const bool revert_game_to_resort = !persist_ok && target.panel == Move::Panel::Resort &&
        return_slot.panel == Move::Panel::Game && !swap_into_hand;
    if (revert_game_to_resort) {
        PcSlotSpecies* target_slot = mutablePokemonAt(target);
        PcSlotSpecies* return_ptr = mutablePokemonAt(return_slot);
        if (target_slot && return_ptr) {
            PcSlotSpecies back = *target_slot;
            clearPokemonAt(target);
            setPokemonAt(return_slot, std::move(back));
        }
        ui_state_.requestErrorSfx();
    }
    const bool revert_resort_to_game = !persist_ok && target.panel == Move::Panel::Game &&
        return_slot.panel == Move::Panel::Resort && !swap_into_hand;
    if (revert_resort_to_game) {
        PcSlotSpecies* target_slot = mutablePokemonAt(target);
        PcSlotSpecies* return_ptr = mutablePokemonAt(return_slot);
        if (target_slot && return_ptr) {
            PcSlotSpecies back = *target_slot;
            setPokemonAt(target, *return_ptr);
            setPokemonAt(return_slot, std::move(back));
        }
        ui_state_.requestErrorSfx();
    }

    refreshResortBoxViewportModel();
    refreshGameBoxViewportModel();
    if (!pokemon_move_.active()) {
        held_move_sprite_tex_ = {};
    } else {
        refreshHeldMoveSpriteTexture();
    }

    if (!game_box_browser_.gameBoxSpaceMode()) {
        if (target.panel == Move::Panel::Game) {
            focus_.setCurrent(2000 + target.slot_index);
        } else {
            focus_.setCurrent(1000 + target.slot_index);
        }
        selection_cursor_hidden_after_mouse_ = false;
    }

    return true;
}

bool TransferSystemScreen::cancelHeldPokemonMove() {
    using Move = transfer_system::PokemonMoveController;
    Move::HeldPokemon* held = pokemon_move_.held();
    if (!held) {
        return false;
    }
    const Move::SlotRef return_slot = held->return_slot;
    const PcSlotSpecies* occupant = pokemonAt(return_slot);
    if (occupant && occupant->occupied()) {
        return false;
    }
    setPokemonAt(return_slot, held->pokemon);
    pokemon_move_.clear();
    held_move_sprite_tex_ = {};
    refreshResortBoxViewportModel();
    refreshGameBoxViewportModel();
    requestPutdownSfx();
    return true;
}

} // namespace pr

