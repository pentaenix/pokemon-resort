#include "ui/TransferSystemScreen.hpp"

#include <SDL.h>

#include <algorithm>

namespace pr {

std::vector<transfer_system::PokemonMoveController::SlotRef> TransferSystemScreen::multiSlotRefsIntersectingRect(
    bool from_game,
    const SDL_Rect& rect) const {
    using Move = transfer_system::PokemonMoveController;
    std::vector<Move::SlotRef> refs;
    if (rect.w <= 0 || rect.h <= 0) {
        return refs;
    }
    auto intersects = [](const SDL_Rect& a, const SDL_Rect& b) {
        return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
    };

    SDL_Rect slot_bounds{};
    if (from_game) {
        if (!game_save_box_viewport_ || game_box_browser_.gameBoxSpaceMode()) {
            return refs;
        }
        const int box_index = game_box_browser_.gameBoxIndex();
        for (int i = 0; i < 30; ++i) {
            if (game_save_box_viewport_->getSlotBounds(i, slot_bounds) &&
                intersects(rect, slot_bounds) &&
                gameSaveSlotHasSpecies(i)) {
                refs.push_back(Move::SlotRef{Move::Panel::Game, box_index, i});
            }
        }
        return refs;
    }

    if (!resort_box_viewport_ || resort_box_browser_.gameBoxSpaceMode()) {
        return refs;
    }
    const int resort_box_index = resort_box_browser_.gameBoxIndex();
    for (int i = 0; i < 30; ++i) {
        if (resort_box_viewport_->getSlotBounds(i, slot_bounds) &&
            intersects(rect, slot_bounds) &&
            resortSlotHasSpecies(i)) {
            refs.push_back(Move::SlotRef{Move::Panel::Resort, resort_box_index, i});
        }
    }
    return refs;
}

std::vector<transfer_system::PokemonMoveController::SlotRef> TransferSystemScreen::keyboardMultiMarqueeOccupiedRefs()
    const {
    using Move = transfer_system::PokemonMoveController;
    std::vector<Move::SlotRef> refs;
    if (!keyboard_multi_marquee_active_) {
        return refs;
    }
    const int a = keyboard_multi_marquee_anchor_slot_;
    const int c = keyboard_multi_marquee_corner_slot_;
    const int r0 = std::min(a / 6, c / 6);
    const int r1 = std::max(a / 6, c / 6);
    const int col0 = std::min(a % 6, c % 6);
    const int col1 = std::max(a % 6, c % 6);

    if (keyboard_multi_marquee_from_game_) {
        const int box_index = game_box_browser_.gameBoxIndex();
        if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
            return refs;
        }
        for (int r = r0; r <= r1; ++r) {
            for (int col = col0; col <= col1; ++col) {
                const int idx = r * 6 + col;
                if (idx >= 0 && idx < 30 && gameSaveSlotHasSpecies(idx)) {
                    refs.push_back(Move::SlotRef{Move::Panel::Game, box_index, idx});
                }
            }
        }
    } else {
        const int resort_box_index = resort_box_browser_.gameBoxIndex();
        for (int r = r0; r <= r1; ++r) {
            for (int col = col0; col <= col1; ++col) {
                const int idx = r * 6 + col;
                if (idx >= 0 && idx < 30 && resortSlotHasSpecies(idx)) {
                    refs.push_back(Move::SlotRef{Move::Panel::Resort, resort_box_index, idx});
                }
            }
        }
    }
    return refs;
}

SDL_Rect TransferSystemScreen::keyboardMultiMarqueeScreenRect() const {
    SDL_Rect out{0, 0, 0, 0};
    if (!keyboard_multi_marquee_active_) {
        return out;
    }
    const int a = keyboard_multi_marquee_anchor_slot_;
    const int c = keyboard_multi_marquee_corner_slot_;
    const int r0 = std::min(a / 6, c / 6);
    const int r1 = std::max(a / 6, c / 6);
    const int col0 = std::min(a % 6, c % 6);
    const int col1 = std::max(a % 6, c % 6);

    bool any = false;
    int min_x = 0;
    int min_y = 0;
    int max_x = 0;
    int max_y = 0;
    for (int r = r0; r <= r1; ++r) {
        for (int col = col0; col <= col1; ++col) {
            const int idx = r * 6 + col;
            SDL_Rect b{};
            const bool ok = keyboard_multi_marquee_from_game_
                ? (game_save_box_viewport_ && game_save_box_viewport_->getSlotBounds(idx, b))
                : (resort_box_viewport_ && resort_box_viewport_->getSlotBounds(idx, b));
            if (!ok) {
                continue;
            }
            if (!any) {
                min_x = b.x;
                min_y = b.y;
                max_x = b.x + b.w;
                max_y = b.y + b.h;
                any = true;
            } else {
                min_x = std::min(min_x, b.x);
                min_y = std::min(min_y, b.y);
                max_x = std::max(max_x, b.x + b.w);
                max_y = std::max(max_y, b.y + b.h);
            }
        }
    }
    if (!any) {
        return out;
    }
    out.x = min_x;
    out.y = min_y;
    out.w = std::max(0, max_x - min_x);
    out.h = std::max(0, max_y - min_y);
    return out;
}

} // namespace pr

