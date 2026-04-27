#pragma once

#include <SDL.h>

#include <optional>

namespace pr::transfer_system {

class BoxSpaceBoxMoveController {
public:
    enum class InputMode {
        Keyboard,
        Pointer,
    };

    struct Pending {
        int source_box_index = -1;
        SDL_Point start_pointer{0, 0};
        SDL_Rect start_cell_bounds{0, 0, 0, 0};
        double elapsed_seconds = 0.0;
    };

    struct Active {
        int source_box_index = -1;
        InputMode input_mode = InputMode::Pointer;
        SDL_Point pointer{0, 0};
    };

    bool pending() const { return pending_.has_value(); }
    bool active() const { return active_.has_value(); }
    const Pending* pendingState() const { return pending_ ? &*pending_ : nullptr; }
    const Active* activeState() const { return active_ ? &*active_ : nullptr; }

    void clear() {
        pending_.reset();
        active_.reset();
    }

    void beginPointerPending(int source_box_index, SDL_Point pointer, const SDL_Rect& cell_bounds);
    /// Returns true if the pending hold activated the move.
    bool updatePointerPending(double dt, double hold_seconds, SDL_Point pointer, bool still_in_start_cell, bool moved_far);
    void updatePointer(SDL_Point pointer);

    void beginKeyboardActive(int source_box_index);
    void setKeyboardMode() {
        if (active_) active_->input_mode = InputMode::Keyboard;
    }

    /// Returns the (from,to) swap indices if a drop is valid.
    std::optional<std::pair<int, int>> dropOn(int target_box_index);

private:
    std::optional<Pending> pending_{};
    std::optional<Active> active_{};
};

} // namespace pr::transfer_system

