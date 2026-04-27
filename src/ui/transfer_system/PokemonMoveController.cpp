#include "ui/transfer_system/PokemonMoveController.hpp"

#include <algorithm>

namespace pr::transfer_system {

namespace {
move::HeldMoveController::PokemonSlotRef toImpl(const PokemonMoveController::SlotRef& ref) {
    move::HeldMoveController::PokemonSlotRef out;
    out.panel = (ref.panel == PokemonMoveController::Panel::Resort)
        ? move::HeldMoveController::PokemonSlotRef::Panel::Resort
        : move::HeldMoveController::PokemonSlotRef::Panel::Game;
    out.box_index = ref.box_index;
    out.slot_index = ref.slot_index;
    return out;
}

PokemonMoveController::SlotRef fromImpl(const move::HeldMoveController::PokemonSlotRef& ref) {
    PokemonMoveController::SlotRef out;
    out.panel = (ref.panel == move::HeldMoveController::PokemonSlotRef::Panel::Resort)
        ? PokemonMoveController::Panel::Resort
        : PokemonMoveController::Panel::Game;
    out.box_index = ref.box_index;
    out.slot_index = ref.slot_index;
    return out;
}

PokemonMoveController::InputMode fromImpl(move::HeldMoveController::InputMode m) {
    return m == move::HeldMoveController::InputMode::Pointer ? PokemonMoveController::InputMode::Pointer
                                                             : PokemonMoveController::InputMode::Keyboard;
}

move::HeldMoveController::InputMode toImpl(PokemonMoveController::InputMode m) {
    return m == PokemonMoveController::InputMode::Pointer ? move::HeldMoveController::InputMode::Pointer
                                                          : move::HeldMoveController::InputMode::Keyboard;
}

move::HeldMoveController::PokemonPickupSource toImpl(PokemonMoveController::PickupSource s) {
    return s == PokemonMoveController::PickupSource::SwapTool ? move::HeldMoveController::PokemonPickupSource::SwapTool
                                                              : move::HeldMoveController::PokemonPickupSource::ActionMenu;
}

PokemonMoveController::PickupSource fromImpl(move::HeldMoveController::PokemonPickupSource s) {
    return s == move::HeldMoveController::PokemonPickupSource::SwapTool ? PokemonMoveController::PickupSource::SwapTool
                                                                        : PokemonMoveController::PickupSource::ActionMenu;
}
} // namespace

const PokemonMoveController::HeldPokemon* PokemonMoveController::held() const {
    const auto* h = impl_.heldPokemon();
    if (!h) {
        return nullptr;
    }
    static thread_local HeldPokemon out;
    out.pokemon = h->pokemon;
    out.return_slot = fromImpl(h->return_slot);
    out.input_mode = fromImpl(h->input_mode);
    out.source = fromImpl(h->source);
    out.pointer = h->pointer;
    return &out;
}

PokemonMoveController::HeldPokemon* PokemonMoveController::held() {
    return const_cast<HeldPokemon*>(static_cast<const PokemonMoveController*>(this)->held());
}

void PokemonMoveController::pickUp(
    const PcSlotSpecies& pokemon,
    const SlotRef& from,
    InputMode input_mode,
    PickupSource source,
    SDL_Point pointer) {
    impl_.pickUpPokemon(pokemon, toImpl(from), toImpl(input_mode), toImpl(source), pointer);
}

void PokemonMoveController::clear() {
    impl_.clear();
}

void PokemonMoveController::updatePointer(SDL_Point pointer, int screen_w, int screen_h) {
    impl_.updatePointer(pointer, screen_w, screen_h);
}

void PokemonMoveController::swapHeldWith(const PcSlotSpecies& target, const SlotRef& next_return_slot) {
    impl_.swapHeldPokemonWith(target, toImpl(next_return_slot));
}

} // namespace pr::transfer_system
