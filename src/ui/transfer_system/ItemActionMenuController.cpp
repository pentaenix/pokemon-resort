#include "ui/transfer_system/ItemActionMenuController.hpp"

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

void ItemActionMenuController::setPutAwayGameLabel(std::string game_title) {
    if (!game_title.empty()) {
        put_away_game_label_ = std::move(game_title);
    }
}

void ItemActionMenuController::setPreferredWidth(int px) {
    preferred_width_px_ = std::max(0, px);
}

int ItemActionMenuController::rowCount() const {
    switch (page_) {
        case Page::Root:
            return 4; // Move, Swap, Put Away, Cancel
        case Page::PutAway:
            return 3; // Resort, Game, Back
        default:
            return 4;
    }
}

const std::string& ItemActionMenuController::labelAt(int row) const {
    static const std::array<std::string, 4> kRoot{
        "Move Item",
        "Swap Item",
        "Put Away",
        "Cancel",
    };
    static const std::array<std::string, 3> kPutAwayFixed{
        "Pokemon Resort",
        "", // game title injected
        "Back",
    };
    if (page_ == Page::Root) {
        row = std::clamp(row, 0, static_cast<int>(kRoot.size()) - 1);
        return kRoot[static_cast<std::size_t>(row)];
    }
    // PutAway
    row = std::clamp(row, 0, static_cast<int>(kPutAwayFixed.size()) - 1);
    if (row == 1) {
        return put_away_game_label_;
    }
    return kPutAwayFixed[static_cast<std::size_t>(row)];
}

void ItemActionMenuController::goToRootPage() {
    page_ = Page::Root;
    selected_row_ = 0;
}

void ItemActionMenuController::goToPutAwayPage() {
    page_ = Page::PutAway;
    selected_row_ = 0;
}

void ItemActionMenuController::open(bool from_game_box, int slot_index, const SDL_Rect& anchor_rect) {
    visible_ = true;
    closing_ = false;
    from_game_box_ = from_game_box;
    slot_index_ = slot_index;
    selected_row_ = 0;
    anchor_rect_ = anchor_rect;
    page_ = Page::Root;
    preferred_width_px_ = 0;
    t_ = std::max(0.04, t_);
}

void ItemActionMenuController::close() {
    if (visible_) {
        closing_ = true;
    }
}

void ItemActionMenuController::clear() {
    *this = ItemActionMenuController{};
}

void ItemActionMenuController::update(double dt, const GameTransferPokemonActionMenuStyle& style) {
    if (!visible_) {
        return;
    }
    approachExponential(t_, closing_ ? 0.0 : 1.0, dt, std::max(1.0, style.grow_smoothing));
    if (closing_ && t_ <= 0.01) {
        *this = ItemActionMenuController{};
    }
}

SDL_Rect ItemActionMenuController::finalRect(
    const GameTransferPokemonActionMenuStyle& style,
    int screen_w,
    int screen_h) const {
    const int base_w = std::max(style.width, preferred_width_px_);
    const int w = std::clamp(base_w, 140, std::max(140, screen_w));
    const int h = std::max(1, style.padding_y * 2 + std::max(1, style.row_height) * rowCount());
    const int anchor_cy = anchor_rect_.y + anchor_rect_.h / 2;
    const int gap = std::max(0, style.gap_from_slot);
    int x = from_game_box_ ? (anchor_rect_.x - gap - w) : (anchor_rect_.x + anchor_rect_.w + gap);
    int y = anchor_cy - h / 2;
    constexpr int kEdgePad = 8;
    x = std::clamp(x, kEdgePad, std::max(kEdgePad, screen_w - w - kEdgePad));
    y = std::clamp(y, kEdgePad, std::max(kEdgePad, screen_h - h - kEdgePad));
    return SDL_Rect{x, y, w, h};
}

std::optional<int> ItemActionMenuController::rowAtPoint(
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
    return row >= 0 && row < rowCount() ? std::optional<int>(row) : std::nullopt;
}

void ItemActionMenuController::stepSelection(int delta) {
    if (!interactive() || delta == 0) {
        return;
    }
    const int n = std::max(1, rowCount());
    selected_row_ = (selected_row_ + delta) % n;
    if (selected_row_ < 0) {
        selected_row_ += n;
    }
}

void ItemActionMenuController::selectRow(int row) {
    if (!interactive()) {
        return;
    }
    selected_row_ = std::clamp(row, 0, std::max(0, rowCount() - 1));
}

ItemActionMenuController::Action ItemActionMenuController::selectedAction() const {
    return actionForRow(selected_row_);
}

ItemActionMenuController::Action ItemActionMenuController::actionForRow(int row) const {
    if (page_ == Page::Root) {
        switch (std::clamp(row, 0, 3)) {
            case 0:
                return Action::MoveItem;
            case 1:
                return Action::SwapItem;
            case 2:
                return Action::PutAway;
            default:
                return Action::Cancel;
        }
    }
    // PutAway page
    switch (std::clamp(row, 0, 2)) {
        case 0:
            return Action::PutAwayResort;
        case 1:
            return Action::PutAwayGame;
        default:
            return Action::Back;
    }
}

} // namespace pr::transfer_system

