#include "ui/TransferSystemScreen.hpp"

namespace pr {

void TransferSystemScreen::togglePillTarget() {
    ui_state_.togglePillTarget();
}

Color TransferSystemScreen::carouselFrameColorForIndex(int tool_index) const {
    switch (tool_index) {
        case 0:
            return carousel_style_.frame_multiple;
        case 1:
            return carousel_style_.frame_basic;
        case 2:
            return carousel_style_.frame_swap;
        case 3:
            return carousel_style_.frame_items;
        default:
            return carousel_style_.frame_basic;
    }
}

bool TransferSystemScreen::carouselSlideAnimating() const {
    return ui_state_.carouselSlideAnimating();
}

bool TransferSystemScreen::itemToolActive() const {
    return ui_state_.selectedToolIndex() == 3;
}

bool TransferSystemScreen::normalPokemonToolActive() const {
    return ui_state_.selectedToolIndex() == 1;
}

bool TransferSystemScreen::multiPokemonToolActive() const {
    return ui_state_.selectedToolIndex() == 0;
}

bool TransferSystemScreen::swapToolActive() const {
    return ui_state_.selectedToolIndex() == 2;
}

bool TransferSystemScreen::pokemonMoveActive() const {
    return pokemon_move_.active() || multi_pokemon_move_.active();
}

void TransferSystemScreen::cycleToolCarousel(int dir) {
    if (pokemon_move_.active() || multi_pokemon_move_.active()) {
        return;
    }
    closePokemonActionMenu();
    ui_state_.cycleToolCarousel(dir, carousel_style_);
}

void TransferSystemScreen::requestPickupSfx() {
    pickup_sfx_requested_ = true;
}

void TransferSystemScreen::requestPutdownSfx() {
    putdown_sfx_requested_ = true;
}

bool TransferSystemScreen::consumeErrorSfxRequest() {
    return ui_state_.consumeErrorSfxRequest();
}

bool TransferSystemScreen::consumeButtonSfxRequest() {
    return ui_state_.consumeButtonSfxRequest();
}

bool TransferSystemScreen::consumeUiMoveSfxRequest() {
    return ui_state_.consumeUiMoveSfxRequest();
}

bool TransferSystemScreen::consumePickupSfxRequest() {
    const bool requested = pickup_sfx_requested_;
    pickup_sfx_requested_ = false;
    return requested;
}

bool TransferSystemScreen::consumePutdownSfxRequest() {
    const bool requested = putdown_sfx_requested_;
    putdown_sfx_requested_ = false;
    return requested;
}

bool TransferSystemScreen::consumeReturnToTicketListRequest() {
    return ui_state_.consumeReturnToTicketListRequest();
}

bool TransferSystemScreen::consumeSuccessfulSaveExitRequest() {
    const bool requested = successful_save_exit_requested_;
    successful_save_exit_requested_ = false;
    return requested;
}

void TransferSystemScreen::requestReturnToTicketList() {
    // Kept for call sites that still phrase this as a screen-level request.
    ui_state_.startExit();
}

} // namespace pr

