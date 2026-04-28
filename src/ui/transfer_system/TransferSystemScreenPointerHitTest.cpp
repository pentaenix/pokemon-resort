#include "ui/TransferSystemScreen.hpp"

#include "ui/BoxViewport.hpp"

#include <SDL.h>

#include <cmath>
#include <optional>
#include <utility>

namespace pr {

namespace {
constexpr int kBoxViewportY = 100;

void getPillTrackBounds(const GameTransferPillToggleStyle& st, int screen_w, int& tx, int& ty, int& tw, int& th) {
    const int right_col_x = screen_w - 40 - BoxViewport::kViewportWidth;
    tx = right_col_x + (BoxViewport::kViewportWidth - st.track_width) / 2;
    ty = kBoxViewportY - st.gap_above_boxes - st.track_height;
    tw = st.track_width;
    th = st.track_height;
}
} // namespace

bool TransferSystemScreen::hitTestPillTrack(int logical_x, int logical_y) const {
    int tx = 0;
    int ty = 0;
    int tw = 0;
    int th = 0;
    getPillTrackBounds(pill_style_, window_config_.virtual_width, tx, ty, tw, th);
    // Pill enters from above on first open / exit only.
    const int enter_off =
        static_cast<int>(std::lround((1.0 - ui_state_.uiEnter()) * static_cast<double>(-(th + 24))));
    ty += enter_off;
    return logical_x >= tx && logical_x < tx + tw && logical_y >= ty && logical_y < ty + th;
}

int TransferSystemScreen::carouselScreenY() const {
    const double t = ui_state_.panelsReveal();
    const double y = static_cast<double>(carousel_style_.rest_y) +
        (1.0 - t) * static_cast<double>(carousel_style_.hidden_y - carousel_style_.rest_y);
    return static_cast<int>(std::round(y));
}

int TransferSystemScreen::exitButtonScreenY() const {
    // Keep the exit button anchored during Items view transitions.
    // It should still slide in/out with the global UI enter/exit.
    const double t = ui_state_.uiEnter();
    const double y = static_cast<double>(carousel_style_.rest_y) +
        (1.0 - t) * static_cast<double>(carousel_style_.hidden_y - carousel_style_.rest_y);
    return static_cast<int>(std::round(y));
}

bool TransferSystemScreen::hitTestToolCarousel(int logical_x, int logical_y) const {
    const int vx = carousel_style_.offset_from_left_wall +
        (exit_button_enabled_ ? (carousel_style_.viewport_height + exit_button_gap_pixels_) : 0);
    const int vy = carouselScreenY();
    const int vw = carousel_style_.viewport_width;
    const int vh = carousel_style_.viewport_height;
    return logical_x >= vx && logical_x < vx + vw && logical_y >= vy && logical_y < vy + vh;
}

std::optional<FocusNodeId> TransferSystemScreen::focusNodeAtPointer(int logical_x, int logical_y) const {
    auto in = [](int px, int py, const SDL_Rect& r) {
        return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
    };

    if (exit_button_enabled_) {
        const int bs = carousel_style_.viewport_height;
        const int bx = carousel_style_.offset_from_left_wall;
        const int by = exitButtonScreenY();
        if (bs > 0 && logical_x >= bx && logical_x < bx + bs && logical_y >= by && logical_y < by + bs) {
            return 5000;
        }
    }
    if (ui_state_.panelsReveal() > 0.02 && hitTestToolCarousel(logical_x, logical_y)) {
        return 3000;
    }
    if (hitTestPillTrack(logical_x, logical_y)) {
        return 4000;
    }

    SDL_Rect r{};
    if (game_save_box_viewport_) {
        if (pointerOverExpandedGameDropdown(logical_x, logical_y)) {
            return 2102;
        }
        if (game_save_box_viewport_->getPrevArrowBounds(r) && in(logical_x, logical_y, r)) {
            return 2101;
        }
        if (game_save_box_viewport_->getNextArrowBounds(r) && in(logical_x, logical_y, r)) {
            return 2103;
        }
        if (game_save_box_viewport_->getBoxSpaceScrollArrowBounds(r) && in(logical_x, logical_y, r)) {
            return 2112;
        }
        if (game_save_box_viewport_->getNamePlateBounds(r) && in(logical_x, logical_y, r)) {
            return 2102;
        }
        for (int i = 0; i < 30; ++i) {
            if (game_save_box_viewport_->getSlotBounds(i, r) && in(logical_x, logical_y, r)) {
                return 2000 + i;
            }
        }
        if (game_save_box_viewport_->getFooterBoxSpaceBounds(r) && in(logical_x, logical_y, r)) {
            return 2110;
        }
        if (game_save_box_viewport_->getFooterGameIconBounds(r) && in(logical_x, logical_y, r)) {
            return 2111;
        }
    }

    if (resort_box_viewport_) {
        if (pointerOverExpandedResortDropdown(logical_x, logical_y)) {
            return 1102;
        }
        if (resort_box_viewport_->getPrevArrowBounds(r) && in(logical_x, logical_y, r)) {
            return 1101;
        }
        if (resort_box_viewport_->getNextArrowBounds(r) && in(logical_x, logical_y, r)) {
            return 1103;
        }
        if (resort_box_viewport_->getNamePlateBounds(r) && in(logical_x, logical_y, r)) {
            return 1102;
        }
        for (int i = 0; i < 30; ++i) {
            if (resort_box_viewport_->getSlotBounds(i, r) && in(logical_x, logical_y, r)) {
                return 1000 + i;
            }
        }
        if (resort_box_viewport_->getFooterBoxSpaceBounds(r) && in(logical_x, logical_y, r)) {
            return 1110;
        }
        if (resort_box_viewport_->getFooterGameIconBounds(r) && in(logical_x, logical_y, r)) {
            return 1111;
        }
        if (resort_box_viewport_->getResortScrollArrowBounds(r) && in(logical_x, logical_y, r)) {
            return 1112;
        }
    }

    return std::nullopt;
}

bool TransferSystemScreen::gameSaveSlotHasSpecies(int slot_index) const {
    if (!game_save_box_viewport_ || slot_index < 0 || slot_index >= 30) {
        return false;
    }
    const int game_box_index = game_box_browser_.gameBoxIndex();
    if (game_box_index < 0 || game_box_index >= static_cast<int>(game_pc_boxes_.size())) {
        return false;
    }
    const auto& slots = game_pc_boxes_[static_cast<std::size_t>(game_box_index)].slots;
    if (slot_index >= static_cast<int>(slots.size())) {
        return false;
    }
    return slots[static_cast<std::size_t>(slot_index)].occupied();
}

bool TransferSystemScreen::resortSlotHasSpecies(int slot_index) const {
    if (!resort_box_viewport_ || slot_index < 0 || slot_index >= 30) {
        return false;
    }
    const int bi = resort_box_browser_.gameBoxIndex();
    if (bi < 0 || bi >= static_cast<int>(resort_pc_boxes_.size())) {
        return false;
    }
    const auto& slots = resort_pc_boxes_[static_cast<std::size_t>(bi)].slots;
    if (slot_index >= static_cast<int>(slots.size())) {
        return false;
    }
    return slots[static_cast<std::size_t>(slot_index)].occupied();
}

std::optional<std::pair<FocusNodeId, SDL_Rect>> TransferSystemScreen::speechBubbleTargetAtPointer(
    int logical_x,
    int logical_y) const {
    auto in = [](int px, int py, const SDL_Rect& r) {
        return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
    };
    SDL_Rect r{};

    if (game_save_box_viewport_) {
        for (int i = 0; i < 30; ++i) {
            if (game_save_box_viewport_->getSlotBounds(i, r) && in(logical_x, logical_y, r)) {
                if (game_box_browser_.gameBoxSpaceMode()) {
                    const int box_index = game_box_browser_.gameBoxSpaceRowOffset() * 6 + i;
                    if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
                        return std::nullopt;
                    }
                } else if (itemToolActive()) {
                    if (!gameSlotHasHeldItem(i)) {
                        return std::nullopt;
                    }
                } else if (!gameSaveSlotHasSpecies(i)) {
                    return std::nullopt;
                }
                return std::make_pair(2000 + i, r);
            }
        }
        if (game_save_box_viewport_->getFooterGameIconBounds(r) && in(logical_x, logical_y, r)) {
            return std::make_pair(2111, r);
        }
    }
    if (resort_box_viewport_) {
        for (int i = 0; i < 30; ++i) {
            if (resort_box_viewport_->getSlotBounds(i, r) && in(logical_x, logical_y, r)) {
                if (resort_box_browser_.gameBoxSpaceMode()) {
                    const int box_index = resort_box_browser_.gameBoxSpaceRowOffset() * 6 + i;
                    if (box_index < 0 || box_index >= static_cast<int>(resort_pc_boxes_.size())) {
                        return std::nullopt;
                    }
                    return std::make_pair(1000 + i, r);
                }
                if (itemToolActive()) {
                    if (!resortSlotHasHeldItem(i)) {
                        return std::nullopt;
                    }
                } else if (!resortSlotHasSpecies(i)) {
                    return std::nullopt;
                }
                return std::make_pair(1000 + i, r);
            }
        }
        if (resort_box_viewport_->getFooterGameIconBounds(r) && in(logical_x, logical_y, r)) {
            return std::make_pair(1111, r);
        }
    }
    return std::nullopt;
}

} // namespace pr

