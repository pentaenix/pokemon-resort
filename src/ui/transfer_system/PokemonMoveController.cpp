#include "ui/transfer_system/PokemonMoveController.hpp"

#include <algorithm>

namespace pr::transfer_system {

void PokemonMoveController::pickUp(
    const PcSlotSpecies& pokemon,
    const SlotRef& from,
    InputMode input_mode,
    PickupSource source,
    SDL_Point pointer) {
    HeldPokemon held;
    held.pokemon = pokemon;
    held.return_slot = from;
    held.input_mode = input_mode;
    held.source = source;
    held.pointer = pointer;
    held_ = std::move(held);
}

void PokemonMoveController::clear() {
    held_.reset();
}

void PokemonMoveController::updatePointer(SDL_Point pointer, int screen_w, int screen_h) {
    if (!held_) {
        return;
    }
    held_->pointer.x = std::clamp(pointer.x, 0, std::max(0, screen_w));
    held_->pointer.y = std::clamp(pointer.y, 0, std::max(0, screen_h));
}

void PokemonMoveController::swapHeldWith(const PcSlotSpecies& target, const SlotRef& next_return_slot) {
    if (!held_) {
        return;
    }
    held_->pokemon = target;
    held_->return_slot = next_return_slot;
}

} // namespace pr::transfer_system
