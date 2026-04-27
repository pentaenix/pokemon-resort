#include "ui/transfer_system/PokemonActionMenuController.hpp"

#include <algorithm>
#include <cmath>

namespace pr::transfer_system {
namespace {

void approachExponential(double& value, double target, double dt, double lambda) {
    const double safe_lambda = std::max(0.0, lambda);
    const double alpha = 1.0 - std::exp(-safe_lambda * std::max(0.0, dt));
    value += (target - value) * std::clamp(alpha, 0.0, 1.0);
}

} // namespace

const std::array<std::string, PokemonActionMenuController::kRowCount>& PokemonActionMenuController::labels() {
    static const std::array<std::string, kRowCount> kLabels{"Move", "Summary", "Mark", "Release", "Cancel"};
    return kLabels;
}

void PokemonActionMenuController::open(bool from_game_box, int slot_index, const SDL_Rect& anchor_rect) {
    visible_ = true;
    closing_ = false;
    from_game_box_ = from_game_box;
    slot_index_ = slot_index;
    selected_row_ = 0;
    anchor_rect_ = anchor_rect;
    t_ = std::max(0.04, t_);
}

void PokemonActionMenuController::close() {
    if (visible_) {
        closing_ = true;
    }
}

void PokemonActionMenuController::clear() {
    *this = PokemonActionMenuController{};
}

void PokemonActionMenuController::update(double dt, const GameTransferPokemonActionMenuStyle& style) {
    if (!visible_) {
        return;
    }
    approachExponential(t_, closing_ ? 0.0 : 1.0, dt, std::max(1.0, style.grow_smoothing));
    if (closing_ && t_ <= 0.01) {
        *this = PokemonActionMenuController{};
    }
}

SDL_Rect PokemonActionMenuController::finalRect(
    const GameTransferPokemonActionMenuStyle& style,
    int screen_w,
    int screen_h) const {
    const int w = std::clamp(style.width, 120, std::max(120, screen_w));
    const int h = std::max(1, style.padding_y * 2 + std::max(1, style.row_height) * kRowCount);
    const int anchor_cy = anchor_rect_.y + anchor_rect_.h / 2;
    const int gap = std::max(0, style.gap_from_slot);
    int x = from_game_box_ ? (anchor_rect_.x - gap - w) : (anchor_rect_.x + anchor_rect_.w + gap);
    int y = anchor_cy - h / 2;
    constexpr int kEdgePad = 8;
    x = std::clamp(x, kEdgePad, std::max(kEdgePad, screen_w - w - kEdgePad));
    y = std::clamp(y, kEdgePad, std::max(kEdgePad, screen_h - h - kEdgePad));
    return SDL_Rect{x, y, w, h};
}

std::optional<int> PokemonActionMenuController::rowAtPoint(
    int logical_x,
    int logical_y,
    const GameTransferPokemonActionMenuStyle& style,
    int screen_w,
    int screen_h) const {
    if (!interactive()) {
        return std::nullopt;
    }
    const SDL_Rect r = finalRect(style, screen_w, screen_h);
    if (logical_x < r.x || logical_x >= r.x + r.w || logical_y < r.y || logical_y >= r.y + r.h) {
        return std::nullopt;
    }
    const int rel_y = logical_y - r.y - std::max(0, style.padding_y);
    const int row_h = std::max(1, style.row_height);
    if (rel_y < 0) {
        return std::nullopt;
    }
    const int row = rel_y / row_h;
    return row >= 0 && row < kRowCount ? std::optional<int>(row) : std::nullopt;
}

void PokemonActionMenuController::stepSelection(int delta) {
    if (!interactive() || delta == 0) {
        return;
    }
    selected_row_ = (selected_row_ + delta) % kRowCount;
    if (selected_row_ < 0) {
        selected_row_ += kRowCount;
    }
}

void PokemonActionMenuController::selectRow(int row) {
    if (!interactive()) {
        return;
    }
    selected_row_ = std::clamp(row, 0, kRowCount - 1);
}

PokemonActionMenuController::Action PokemonActionMenuController::selectedAction() const {
    return actionForRow(selected_row_);
}

PokemonActionMenuController::Action PokemonActionMenuController::actionForRow(int row) const {
    switch (std::clamp(row, 0, kRowCount - 1)) {
        case 0:
            return Action::Move;
        case 1:
            return Action::Summary;
        case 2:
            return Action::Mark;
        case 3:
            return Action::Release;
        default:
            return Action::Cancel;
    }
}

} // namespace pr::transfer_system
