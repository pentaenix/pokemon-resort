#include "ui/title_screen/MainMenuController.hpp"

namespace pr::title_screen {

namespace {

constexpr int kResortIndex = 0;
constexpr int kTransferIndex = 1;
constexpr int kTradeIndex = 2;
constexpr int kOptionsIndex = 3;

} // namespace

MainMenuController::MainMenuController(std::size_t item_count) {
    setItemCount(item_count);
}

void MainMenuController::setItemCount(std::size_t item_count) {
    item_count_ = static_cast<int>(item_count);
    selected_index_ = wrapIndex(selected_index_, item_count_);
}

bool MainMenuController::selectIndex(int index) {
    if (item_count_ <= 0 || index < 0 || index >= item_count_) {
        return false;
    }
    if (selected_index_ == index) {
        return false;
    }
    selected_index_ = index;
    return true;
}

bool MainMenuController::navigate(int delta) {
    const int next = wrapIndex(selected_index_ + delta, item_count_);
    if (next == selected_index_) {
        return false;
    }
    selected_index_ = next;
    return true;
}

MainMenuAction MainMenuController::activate() const {
    if (item_count_ <= 0) {
        return MainMenuAction::None;
    }
    return actionForIndex(selected_index_);
}

void MainMenuController::selectTransfer() {
    selectIndex(kTransferIndex);
}

void MainMenuController::selectTrade() {
    selectIndex(kTradeIndex);
}

void MainMenuController::selectOptions() {
    selectIndex(kOptionsIndex);
}

void MainMenuController::reset() {
    selected_index_ = wrapIndex(kResortIndex, item_count_);
}

int MainMenuController::wrapIndex(int value, int size) {
    if (size <= 0) {
        return 0;
    }
    value %= size;
    return value < 0 ? value + size : value;
}

MainMenuAction MainMenuController::actionForIndex(int index) {
    switch (index) {
        case kResortIndex:
            return MainMenuAction::OpenResort;
        case kTransferIndex:
            return MainMenuAction::OpenTransfer;
        case kTradeIndex:
            return MainMenuAction::OpenTrade;
        case kOptionsIndex:
            return MainMenuAction::OpenOptions;
        default:
            return MainMenuAction::None;
    }
}

} // namespace pr::title_screen
