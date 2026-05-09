#include "ui/TransferSystemScreen.hpp"

namespace pr {

bool TransferSystemScreen::activateHeldBoxOnAdvance() {
    if (dropdownAcceptsNavigation()) {
        // While holding a Box Space box, Accept should still confirm dropdown choices.
        applyActiveDropdownSelection();
        return true;
    }

    const auto* held_box = held_move_.heldBox();
    if (!held_box) {
        return false;
    }

    const int from = held_box->source_box_index;
    const auto source_panel = held_box->source_panel;
    const auto game_target = focusedBoxSpaceBoxIndex();
    const auto resort_target = focusedResortBoxSpaceBoxIndex();

    auto finishAccepted = [&]() {
        held_move_.clear();
        refreshGameBoxViewportModel();
        refreshResortBoxViewportModel();
        requestPutdownSfx();
        return true;
    };
    auto finishRejected = [&]() {
        held_move_.clear();
        refreshGameBoxViewportModel();
        refreshResortBoxViewportModel();
        triggerHeldSpriteRejectFeedback();
        return true;
    };

    using Panel = transfer_system::move::HeldMoveController::PokemonSlotRef::Panel;
    if (source_panel == Panel::Game) {
        if (game_target.has_value()) {
            (void)swapGamePcBoxes(from, *game_target);
            return finishAccepted();
        }
        if (resort_target.has_value()) {
            return swapGameAndResortPcBoxes(from, *resort_target) ? finishAccepted() : finishRejected();
        }
    } else {
        if (resort_target.has_value()) {
            (void)swapResortPcBoxes(from, *resort_target);
            return finishAccepted();
        }
        if (game_target.has_value()) {
            return swapGameAndResortPcBoxes(*game_target, from) ? finishAccepted() : finishRejected();
        }
    }

    return false;
}

} // namespace pr
