#include "ui/TransferSystemScreen.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>

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

#ifdef PR_ENABLE_TEST_HOOKS
std::optional<SDL_Rect> TransferSystemScreen::debugPillTrackBounds() const {
    int tx = 0;
    int ty = 0;
    int tw = 0;
    int th = 0;
    getPillTrackBounds(pill_style_, window_config_.virtual_width, tx, ty, tw, th);
    const int enter_off =
        static_cast<int>(std::lround((1.0 - ui_state_.uiEnter()) * static_cast<double>(-(th + 24))));
    ty += enter_off;
    return SDL_Rect{tx, ty, tw, th};
}

std::optional<int> TransferSystemScreen::debugDropdownRowAtScreen(int logical_x, int logical_y) const {
    return dropdownRowIndexAtScreen(logical_x, logical_y);
}
#endif

} // namespace pr

