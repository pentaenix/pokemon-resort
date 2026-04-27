#include "ui/transfer_system/BoxSpaceBoxMoveController.hpp"

namespace pr::transfer_system {

void BoxSpaceBoxMoveController::beginPointerPending(int source_box_index, SDL_Point pointer, const SDL_Rect& cell_bounds) {
    pending_ = Pending{source_box_index, pointer, cell_bounds, 0.0};
    active_.reset();
}

bool BoxSpaceBoxMoveController::updatePointerPending(
    double dt,
    double hold_seconds,
    SDL_Point pointer,
    bool still_in_start_cell,
    bool moved_far) {
    if (!pending_ || active_) {
        return false;
    }
    pending_->elapsed_seconds += dt;
    if (hold_seconds <= 1e-6) {
        return false;
    }
    if (!still_in_start_cell || moved_far) {
        return false;
    }
    if (pending_->elapsed_seconds < hold_seconds) {
        return false;
    }
    active_ = Active{pending_->source_box_index, InputMode::Pointer, pointer};
    pending_.reset();
    return true;
}

void BoxSpaceBoxMoveController::updatePointer(SDL_Point pointer) {
    if (active_) {
        active_->pointer = pointer;
        active_->input_mode = InputMode::Pointer;
    }
}

void BoxSpaceBoxMoveController::beginKeyboardActive(int source_box_index) {
    active_ = Active{source_box_index, InputMode::Keyboard, SDL_Point{0, 0}};
    pending_.reset();
}

std::optional<std::pair<int, int>> BoxSpaceBoxMoveController::dropOn(int target_box_index) {
    if (!active_) {
        return std::nullopt;
    }
    const int from = active_->source_box_index;
    active_.reset();
    pending_.reset();
    if (from < 0 || target_box_index < 0) {
        return std::nullopt;
    }
    return std::make_pair(from, target_box_index);
}

} // namespace pr::transfer_system

