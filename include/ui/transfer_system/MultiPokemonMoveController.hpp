#pragma once

#include "core/domain/PcSlotSpecies.hpp"
#include "ui/transfer_system/PokemonMoveController.hpp"

#include <SDL.h>

#include <optional>
#include <vector>

namespace pr::transfer_system {

class MultiPokemonMoveController {
public:
    using SlotRef = PokemonMoveController::SlotRef;

    enum class InputMode {
        Keyboard,
        Pointer,
    };

    struct Entry {
        PcSlotSpecies pokemon;
        SlotRef return_slot{};
        int row_offset = 0;
        int col_offset = 0;
    };

    bool active() const { return !entries_.empty(); }
    const std::vector<Entry>& entries() const { return entries_; }
    int count() const { return static_cast<int>(entries_.size()); }
    InputMode inputMode() const { return input_mode_; }
    SDL_Point pointer() const { return pointer_; }

    void pickUp(std::vector<Entry> entries, InputMode input_mode, SDL_Point pointer);
    void clear();
    void updatePointer(SDL_Point pointer, int screen_w, int screen_h);

    std::optional<std::vector<SlotRef>> targetSlotsFor(const SlotRef& anchor) const;

private:
    std::vector<Entry> entries_{};
    InputMode input_mode_ = InputMode::Keyboard;
    SDL_Point pointer_{0, 0};
};

} // namespace pr::transfer_system
