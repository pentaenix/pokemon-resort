#include "ui/TransferSystemScreen.hpp"

#include <cmath>

namespace pr {
namespace {

double easeOutCubic(double t) {
    if (t < 0.0) {
        t = 0.0;
    } else if (t > 1.0) {
        t = 1.0;
    }
    const double inv = 1.0 - t;
    return 1.0 - inv * inv * inv;
}

} // namespace

void TransferSystemScreen::updateAnimations(double dt) {
    ui_state_.update(dt, pill_style_, carousel_style_);
}

void TransferSystemScreen::updateEnterExit(double dt) {
    (void)dt;
}

void TransferSystemScreen::updateCarouselSlide(double dt) {
    (void)dt;
}

void TransferSystemScreen::syncBoxViewportPositions() {
    const int screen_w = window_config_.virtual_width;
    const int resort_hidden_x = -BoxViewport::kViewportWidth;
    const int game_hidden_x = screen_w;
    const int resort_rest_x = 40; // kLeftBoxColumnX in original TU
    const int game_rest_x = screen_w - 40 - BoxViewport::kViewportWidth;
    const double summary_t = easeOutCubic(pokemon_summary_reveal_);
    const bool summary_left = pokemonSummaryPanelOpenOnLeft();

    const int resort_x =
        static_cast<int>(std::round(resort_rest_x + (resort_hidden_x - resort_rest_x) *
            (summary_left ? summary_t : 0.0) +
            (1.0 - ui_state_.panelsReveal()) * (resort_hidden_x - resort_rest_x)));
    const int game_x =
        static_cast<int>(std::round(game_rest_x + (game_hidden_x - game_rest_x) *
            (!summary_left ? summary_t : 0.0) +
            (1.0 - ui_state_.panelsReveal()) * (game_hidden_x - game_rest_x)));

    constexpr int kBoxViewportY = 100;
    if (resort_box_viewport_) {
        resort_box_viewport_->setViewportOrigin(resort_x, kBoxViewportY);
    }
    if (game_save_box_viewport_) {
        game_save_box_viewport_->setViewportOrigin(game_x, kBoxViewportY);
    }
}

bool TransferSystemScreen::panelsReadyForInteraction() const {
    return ui_state_.panelsReveal() > 0.85 && ui_state_.uiEnter() > 0.85;
}

bool TransferSystemScreen::dropdownAcceptsNavigation() const {
    return gameDropdownNavigationActive() || resortDropdownNavigationActive();
}

std::optional<int> TransferSystemScreen::focusedGameSlotIndex() const {
    const FocusNodeId current = focus_.current();
    if (current < 2000 || current > 2029) {
        return std::nullopt;
    }
    return current - 2000;
}

std::optional<int> TransferSystemScreen::focusedBoxSpaceBoxIndex() const {
    if (!game_box_browser_.gameBoxSpaceMode()) {
        return std::nullopt;
    }
    const FocusNodeId current = focus_.current();
    if (current < 2000 || current > 2029) {
        return std::nullopt;
    }
    const int cell = current - 2000;
    const int box_index = game_box_browser_.gameBoxSpaceRowOffset() * 6 + cell;
    if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
        return std::nullopt;
    }
    return box_index;
}

std::optional<int> TransferSystemScreen::focusedResortBoxSpaceBoxIndex() const {
    if (!resort_box_browser_.gameBoxSpaceMode()) {
        return std::nullopt;
    }
    const FocusNodeId current = focus_.current();
    if (current < 1000 || current > 1029) {
        return std::nullopt;
    }
    const int cell = current - 1000;
    const int box_index = resort_box_browser_.gameBoxSpaceRowOffset() * 6 + cell;
    if (box_index < 0 || box_index >= static_cast<int>(resort_pc_boxes_.size())) {
        return std::nullopt;
    }
    return box_index;
}

std::optional<int> TransferSystemScreen::focusedResortSlotIndex() const {
    const FocusNodeId current = focus_.current();
    if (current < 1000 || current > 1029) {
        return std::nullopt;
    }
    return current - 1000;
}

} // namespace pr
