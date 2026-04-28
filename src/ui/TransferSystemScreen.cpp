#include "ui/TransferSystemScreen.hpp"

namespace pr {

bool TransferSystemScreen::activateFocusedGameSlot() {
    const std::optional<int> slot = focusedGameSlotIndex();
    if (!slot.has_value()) {
        return false;
    }
    if (!panelsReadyForInteraction() || !game_save_box_viewport_) {
        return false;
    }
    if (game_box_browser_.gameBoxSpaceMode()) {
        const int box_index = game_box_browser_.gameBoxSpaceRowOffset() * 6 + *slot;
        return openGameBoxFromBoxSpaceSelection(box_index);
    }
    return false;
}

void TransferSystemScreen::update(double dt) {
    updateAnimations(dt);
    updateEnterExit(dt);
    updateCarouselSlide(dt);
    if (box_rename_modal_open_) {
        box_rename_caret_blink_phase_ += dt;
        if (box_rename_caret_blink_phase_ > 640.0) {
            box_rename_caret_blink_phase_ -= 640.0;
        }
    }
    updateGameBoxDropdown(dt);
    updateResortBoxDropdown(dt);
    updateMiniPreview(dt);
    updateActionMenus(dt);
    updateBoxSpaceLongPressGestures(dt);

    if (!multiPokemonToolActive()) {
        keyboard_multi_marquee_active_ = false;
    }
    updateBoxViewportsAndFocusDimming(dt);

    // Commit box index once the content slide finishes.
}

} // namespace pr
