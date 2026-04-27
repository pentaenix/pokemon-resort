#include "ui/transfer_system/move/HeldMoveController.hpp"

#include <algorithm>

namespace pr::transfer_system::move {

void HeldMoveController::clear() {
    kind_.reset();
    held_pokemon_.reset();
    held_box_.reset();
    held_item_.reset();
}

void HeldMoveController::pickUpPokemon(
    const PcSlotSpecies& pokemon,
    const PokemonSlotRef& from,
    InputMode input_mode,
    PokemonPickupSource source,
    SDL_Point pointer) {
    HeldPokemon held;
    held.pokemon = pokemon;
    held.return_slot = from;
    held.input_mode = input_mode;
    held.source = source;
    held.pointer = pointer;
    kind_ = Kind::Pokemon;
    held_pokemon_ = std::move(held);
    held_box_.reset();
    held_item_.reset();
}

void HeldMoveController::swapHeldPokemonWith(const PcSlotSpecies& target, const PokemonSlotRef& next_return_slot) {
    if (!held_pokemon_ || !kind_ || *kind_ != Kind::Pokemon) {
        return;
    }
    held_pokemon_->pokemon = target;
    held_pokemon_->return_slot = next_return_slot;
}

void HeldMoveController::pickUpBox(int source_box_index, InputMode input_mode, SDL_Point pointer) {
    HeldBoxSpaceBox held;
    held.source_box_index = source_box_index;
    held.input_mode = input_mode;
    held.pointer = pointer;
    kind_ = Kind::BoxSpaceBox;
    held_box_ = std::move(held);
    held_pokemon_.reset();
    held_item_.reset();
}

void HeldMoveController::pickUpItem(
    int item_id,
    std::string item_name,
    const PokemonSlotRef& from,
    InputMode input_mode,
    SDL_Point pointer) {
    HeldItem held;
    held.item_id = item_id;
    held.item_name = std::move(item_name);
    held.origin_slot = from;
    held.return_slot = from;
    held.input_mode = input_mode;
    held.pointer = pointer;
    kind_ = Kind::Item;
    held_item_ = std::move(held);
    held_pokemon_.reset();
    held_box_.reset();
}

void HeldMoveController::swapHeldItemWith(int item_id, std::string item_name, const PokemonSlotRef& next_return_slot) {
    if (!held_item_ || !kind_ || *kind_ != Kind::Item) {
        return;
    }
    held_item_->item_id = item_id;
    held_item_->item_name = std::move(item_name);
    held_item_->return_slot = next_return_slot;
}

void HeldMoveController::updatePointer(SDL_Point pointer, int screen_w, int screen_h) {
    pointer.x = std::clamp(pointer.x, 0, std::max(0, screen_w));
    pointer.y = std::clamp(pointer.y, 0, std::max(0, screen_h));
    if (!kind_) return;
    if (*kind_ == Kind::Pokemon) {
        if (held_pokemon_) {
            held_pokemon_->pointer = pointer;
        }
    } else if (*kind_ == Kind::BoxSpaceBox) {
        if (held_box_) {
            held_box_->pointer = pointer;
        }
    } else if (*kind_ == Kind::Item) {
        if (held_item_) {
            held_item_->pointer = pointer;
        }
    }
}

} // namespace pr::transfer_system::move

