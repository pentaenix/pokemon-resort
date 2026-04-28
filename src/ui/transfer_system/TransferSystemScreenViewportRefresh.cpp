#include "ui/TransferSystemScreen.hpp"

namespace pr {

void TransferSystemScreen::refreshResortBoxViewportModel() {
    if (!resort_box_viewport_) {
        return;
    }
    if (resort_box_browser_.gameBoxSpaceMode()) {
        const bool show_down = resortBoxSpaceMaxRowOffset() > 0;
        resort_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::BoxSpace, show_down);
        resort_box_viewport_->snapContentToModel(
            resortBoxSpaceViewportModelAt(resort_box_browser_.gameBoxSpaceRowOffset()));
    } else {
        resort_box_viewport_->setModel(resortBoxViewportModelAt(resort_box_browser_.gameBoxIndex()));
    }
}

void TransferSystemScreen::refreshGameBoxViewportModel() {
    if (!game_save_box_viewport_ || game_pc_boxes_.empty()) {
        return;
    }
    if (game_box_browser_.gameBoxSpaceMode()) {
        game_save_box_viewport_->snapContentToModel(
            gameBoxSpaceViewportModelAt(game_box_browser_.gameBoxSpaceRowOffset()));
    } else {
        game_save_box_viewport_->setModel(gameBoxViewportModelAt(game_box_browser_.gameBoxIndex()));
    }
}

} // namespace pr

