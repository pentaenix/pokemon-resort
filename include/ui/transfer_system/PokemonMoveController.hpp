#pragma once

#include "core/PcSlotSpecies.hpp"
#include "ui/transfer_system/move/HeldMoveController.hpp"

#include <SDL.h>

#include <optional>

namespace pr::transfer_system {

class PokemonMoveController {
public:
    enum class Panel {
        Resort,
        Game,
    };

    enum class InputMode {
        Keyboard,
        Pointer,
    };

    enum class PickupSource {
        ActionMenu,
        SwapTool,
    };

    enum class OccupiedDropPolicy {
        SwapIntoHand,
        SendTargetToReturnSlot,
    };

    struct SlotRef {
        Panel panel = Panel::Game;
        int box_index = 0;
        int slot_index = 0;

        friend bool operator==(const SlotRef& a, const SlotRef& b) {
            return a.panel == b.panel && a.box_index == b.box_index && a.slot_index == b.slot_index;
        }
        friend bool operator!=(const SlotRef& a, const SlotRef& b) {
            return !(a == b);
        }
    };

    struct HeldPokemon {
        PcSlotSpecies pokemon;
        SlotRef return_slot{};
        InputMode input_mode = InputMode::Keyboard;
        PickupSource source = PickupSource::ActionMenu;
        SDL_Point pointer{0, 0};
    };

    bool active() const { return impl_.active(); }
    const HeldPokemon* held() const;
    HeldPokemon* held();

    void pickUp(
        const PcSlotSpecies& pokemon,
        const SlotRef& from,
        InputMode input_mode,
        PickupSource source,
        SDL_Point pointer);
    void clear();
    void updatePointer(SDL_Point pointer, int screen_w, int screen_h);
    void swapHeldWith(const PcSlotSpecies& target, const SlotRef& next_return_slot);

private:
    // Implementation is shared with other draggable entities (e.g. Box Space boxes, Items later).
    transfer_system::move::HeldMoveController impl_{};
};

} // namespace pr::transfer_system
