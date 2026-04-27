#pragma once

#include "core/PcSlotSpecies.hpp"

#include <SDL.h>

#include <optional>
#include <string>

namespace pr::transfer_system::move {

class HeldMoveController {
public:
    enum class Kind {
        Pokemon,
        BoxSpaceBox,
        Item,
    };

    enum class InputMode {
        Keyboard,
        Pointer,
    };

    struct PokemonSlotRef {
        enum class Panel {
            Resort,
            Game,
        };
        Panel panel = Panel::Game;
        int box_index = 0;
        int slot_index = 0;
        friend bool operator==(const PokemonSlotRef& a, const PokemonSlotRef& b) {
            return a.panel == b.panel && a.box_index == b.box_index && a.slot_index == b.slot_index;
        }
        friend bool operator!=(const PokemonSlotRef& a, const PokemonSlotRef& b) {
            return !(a == b);
        }
    };

    enum class PokemonPickupSource {
        ActionMenu,
        SwapTool,
    };

    struct HeldPokemon {
        PcSlotSpecies pokemon;
        PokemonSlotRef return_slot{};
        InputMode input_mode = InputMode::Keyboard;
        PokemonPickupSource source = PokemonPickupSource::ActionMenu;
        SDL_Point pointer{0, 0};
    };

    struct HeldBoxSpaceBox {
        int source_box_index = -1;
        InputMode input_mode = InputMode::Pointer;
        SDL_Point pointer{0, 0};
    };

    struct HeldItem {
        int item_id = -1;
        std::string item_name;
        PokemonSlotRef origin_slot{};
        PokemonSlotRef return_slot{};
        InputMode input_mode = InputMode::Keyboard;
        SDL_Point pointer{0, 0};
    };

    bool active() const { return kind_.has_value(); }
    std::optional<Kind> kind() const { return kind_; }

    const HeldPokemon* heldPokemon() const { return (kind_ && *kind_ == Kind::Pokemon && held_pokemon_) ? &*held_pokemon_ : nullptr; }
    HeldPokemon* heldPokemon() { return (kind_ && *kind_ == Kind::Pokemon && held_pokemon_) ? &*held_pokemon_ : nullptr; }

    const HeldBoxSpaceBox* heldBox() const { return (kind_ && *kind_ == Kind::BoxSpaceBox && held_box_) ? &*held_box_ : nullptr; }
    HeldBoxSpaceBox* heldBox() { return (kind_ && *kind_ == Kind::BoxSpaceBox && held_box_) ? &*held_box_ : nullptr; }

    const HeldItem* heldItem() const { return (kind_ && *kind_ == Kind::Item && held_item_) ? &*held_item_ : nullptr; }
    HeldItem* heldItem() { return (kind_ && *kind_ == Kind::Item && held_item_) ? &*held_item_ : nullptr; }

    void clear();

    // Pokemon
    void pickUpPokemon(const PcSlotSpecies& pokemon, const PokemonSlotRef& from, InputMode input_mode, PokemonPickupSource source, SDL_Point pointer);
    void swapHeldPokemonWith(const PcSlotSpecies& target, const PokemonSlotRef& next_return_slot);

    // Box Space
    void pickUpBox(int source_box_index, InputMode input_mode, SDL_Point pointer);
    void pickUpItem(int item_id, std::string item_name, const PokemonSlotRef& from, InputMode input_mode, SDL_Point pointer);
    void swapHeldItemWith(int item_id, std::string item_name, const PokemonSlotRef& next_return_slot);

    void updatePointer(SDL_Point pointer, int screen_w, int screen_h);

private:
    std::optional<Kind> kind_{};
    std::optional<HeldPokemon> held_pokemon_{};
    std::optional<HeldBoxSpaceBox> held_box_{};
    std::optional<HeldItem> held_item_{};
};

} // namespace pr::transfer_system::move

