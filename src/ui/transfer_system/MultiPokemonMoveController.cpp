#include "ui/transfer_system/MultiPokemonMoveController.hpp"

#include <algorithm>

namespace pr::transfer_system {

void MultiPokemonMoveController::pickUp(std::vector<Entry> entries, InputMode input_mode, SDL_Point pointer) {
    entries_ = std::move(entries);
    input_mode_ = input_mode;
    pointer_ = pointer;
}

void MultiPokemonMoveController::clear() {
    entries_.clear();
    input_mode_ = InputMode::Keyboard;
    pointer_ = SDL_Point{0, 0};
}

void MultiPokemonMoveController::updatePointer(SDL_Point pointer, int screen_w, int screen_h) {
    pointer.x = std::clamp(pointer.x, 0, std::max(0, screen_w));
    pointer.y = std::clamp(pointer.y, 0, std::max(0, screen_h));
    pointer_ = pointer;
    input_mode_ = InputMode::Pointer;
}

std::optional<std::vector<MultiPokemonMoveController::SlotRef>> MultiPokemonMoveController::targetSlotsFor(
    const SlotRef& anchor) const {
    if (entries_.empty()) {
        return std::nullopt;
    }

    constexpr int kCols = 6;
    constexpr int kRows = 5;
    const int anchor_row = anchor.slot_index / kCols;
    const int anchor_col = anchor.slot_index % kCols;

    std::vector<SlotRef> out;
    out.reserve(entries_.size());
    for (const Entry& entry : entries_) {
        const int row = anchor_row + entry.row_offset;
        const int col = anchor_col + entry.col_offset;
        if (row < 0 || row >= kRows || col < 0 || col >= kCols) {
            return std::nullopt;
        }
        SlotRef ref = anchor;
        ref.slot_index = row * kCols + col;
        out.push_back(ref);
    }
    return out;
}

} // namespace pr::transfer_system
