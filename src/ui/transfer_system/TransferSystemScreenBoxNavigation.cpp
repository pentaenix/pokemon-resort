#include "ui/TransferSystemScreen.hpp"

namespace pr {

void TransferSystemScreen::stepResortBoxSpaceRowDown() {
    if (!resort_box_viewport_) {
        return;
    }
    const int box_count = static_cast<int>(resort_pc_boxes_.size());
    const int max_row = resort_box_browser_.gameBoxSpaceMaxRowOffset(box_count);
    if (!resort_box_browser_.stepGameBoxSpaceRowDown(box_count)) {
        return;
    }
    const bool show_down = max_row > 0;
    resort_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::BoxSpace, show_down);
    resort_box_viewport_->snapContentToModel(resortBoxSpaceViewportModelAt(resort_box_browser_.gameBoxSpaceRowOffset()));
    ui_state_.requestButtonSfx();
}

void TransferSystemScreen::stepResortBoxSpaceRowUp() {
    if (!resort_box_viewport_) {
        return;
    }
    const int box_count = static_cast<int>(resort_pc_boxes_.size());
    if (!resort_box_browser_.stepGameBoxSpaceRowUp()) {
        return;
    }
    const bool show_down = resortBoxSpaceMaxRowOffset() > 0;
    resort_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::BoxSpace, show_down);
    resort_box_viewport_->snapContentToModel(resortBoxSpaceViewportModelAt(resort_box_browser_.gameBoxSpaceRowOffset()));
    ui_state_.requestButtonSfx();
}

void TransferSystemScreen::advanceResortBox(int dir) {
    if (!resort_box_viewport_ || resort_pc_boxes_.empty() || dir == 0) {
        return;
    }
    const int count = static_cast<int>(resort_pc_boxes_.size());
    if (!resort_box_browser_.advanceGameBox(dir, count, ui_state_.panelsReveal() > 0.85 && ui_state_.uiEnter() > 0.85)) {
        return;
    }
    const int next = resort_box_browser_.gameBoxIndex();
    resort_box_viewport_->queueContentSlide(resortBoxViewportModelAt(next), dir);
    ui_state_.requestButtonSfx();
}

void TransferSystemScreen::jumpResortBoxToIndex(int target_index) {
    if (!resort_box_viewport_ || resort_pc_boxes_.empty()) {
        return;
    }
    const int previous_index = resort_box_browser_.gameBoxIndex();
    const int n = static_cast<int>(resort_pc_boxes_.size());
    const bool changed =
        resort_box_browser_.jumpGameBoxToIndex(target_index, n, ui_state_.panelsReveal() > 0.85 && ui_state_.uiEnter() > 0.85);
    if (!changed) {
        return;
    }
    const int next_index = resort_box_browser_.gameBoxIndex();
    const int slide_dir = (next_index >= previous_index) ? 1 : -1;
    resort_box_viewport_->queueContentSlide(resortBoxViewportModelAt(next_index), slide_dir);
    ui_state_.requestButtonSfx();
}

void TransferSystemScreen::stepGameBoxSpaceRowDown() {
    if (!game_save_box_viewport_) {
        return;
    }
    const int box_count = static_cast<int>(game_pc_boxes_.size());
    const int max_row = game_box_browser_.gameBoxSpaceMaxRowOffset(box_count);
    if (!game_box_browser_.stepGameBoxSpaceRowDown(box_count)) {
        return;
    }
    const bool show_down = max_row > 0;
    game_save_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::BoxSpace, show_down);
    game_save_box_viewport_->snapContentToModel(gameBoxSpaceViewportModelAt(game_box_browser_.gameBoxSpaceRowOffset()));
    ui_state_.requestButtonSfx();
}

void TransferSystemScreen::stepGameBoxSpaceRowUp() {
    if (!game_save_box_viewport_) {
        return;
    }
    if (!game_box_browser_.stepGameBoxSpaceRowUp()) {
        return;
    }
    const bool show_down = gameBoxSpaceMaxRowOffset() > 0;
    game_save_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::BoxSpace, show_down);
    game_save_box_viewport_->snapContentToModel(gameBoxSpaceViewportModelAt(game_box_browser_.gameBoxSpaceRowOffset()));
    ui_state_.requestButtonSfx();
}

void TransferSystemScreen::advanceGameBox(int dir) {
    if (!game_save_box_viewport_ || game_pc_boxes_.empty() || dir == 0) {
        return;
    }
    const int count = static_cast<int>(game_pc_boxes_.size());
    if (!game_box_browser_.advanceGameBox(dir, count, ui_state_.panelsReveal() > 0.85 && ui_state_.uiEnter() > 0.85)) {
        return;
    }
    const int next = game_box_browser_.gameBoxIndex();

    game_save_box_viewport_->queueContentSlide(gameBoxViewportModelAt(next), dir);
    ui_state_.requestButtonSfx();
}

void TransferSystemScreen::jumpGameBoxToIndex(int target_index) {
    if (!game_save_box_viewport_ || game_pc_boxes_.empty()) {
        return;
    }
    const int previous_index = game_box_browser_.gameBoxIndex();
    const int n = static_cast<int>(game_pc_boxes_.size());
    const bool changed =
        game_box_browser_.jumpGameBoxToIndex(target_index, n, ui_state_.panelsReveal() > 0.85 && ui_state_.uiEnter() > 0.85);
    if (!changed) {
        return;
    }
    const int next_index = game_box_browser_.gameBoxIndex();
    const int slide_dir = (next_index >= previous_index) ? 1 : -1;
    game_save_box_viewport_->queueContentSlide(gameBoxViewportModelAt(next_index), slide_dir);
    ui_state_.requestButtonSfx();
}

bool TransferSystemScreen::handleGameBoxNavigationPointerPressed(int logical_x, int logical_y) {
    if (!game_save_box_viewport_ || !panelsReadyForInteraction() || game_pc_boxes_.empty()) {
        return false;
    }
    int dir = 0;
    if (game_save_box_viewport_->hitTestPrevBoxArrow(logical_x, logical_y)) {
        dir = -1;
    } else if (game_save_box_viewport_->hitTestNextBoxArrow(logical_x, logical_y)) {
        dir = 1;
    }
    if (dir == 0) {
        return false;
    }
    advanceGameBox(dir);
    return true;
}

bool TransferSystemScreen::handleResortBoxNavigationPointerPressed(int logical_x, int logical_y) {
    if (!resort_box_viewport_ || !panelsReadyForInteraction() || resort_pc_boxes_.empty()) {
        return false;
    }
    int dir = 0;
    if (resort_box_viewport_->hitTestPrevBoxArrow(logical_x, logical_y)) {
        dir = -1;
    } else if (resort_box_viewport_->hitTestNextBoxArrow(logical_x, logical_y)) {
        dir = 1;
    }
    if (dir == 0) {
        return false;
    }
    advanceResortBox(dir);
    return true;
}

} // namespace pr

