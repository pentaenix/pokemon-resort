#include "ui/title_screen/ResortMenuController.hpp"

namespace pr::title_screen {

namespace {
constexpr int k3DTestIndex = 0;
constexpr int kStartCinematicIndex = 1;
constexpr int kTestAttendIndex = 2;
constexpr int kBackIndex = 3;
constexpr int kItemCount = 4;
} // namespace

bool ResortMenuController::navigate(int delta) {
    const int next = wrapIndex(selected_index_ + delta, kItemCount);
    if (next == selected_index_) {
        return false;
    }
    selected_index_ = next;
    return true;
}

bool ResortMenuController::selectIndex(int index) {
    if (index < 0 || index >= kItemCount) {
        return false;
    }
    if (selected_index_ == index) {
        return false;
    }
    selected_index_ = index;
    return true;
}

void ResortMenuController::resetSelection() {
    selected_index_ = 0;
}

ResortMenuAction ResortMenuController::activate() const {
    switch (selected_index_) {
        case k3DTestIndex:
            return ResortMenuAction::Open3DTest;
        case kStartCinematicIndex:
            return ResortMenuAction::StartCinematic;
        case kTestAttendIndex:
            return ResortMenuAction::OpenTestAttend;
        case kBackIndex:
            return ResortMenuAction::CloseResortMenu;
        default:
            return ResortMenuAction::None;
    }
}

std::vector<std::string> ResortMenuController::labels() const {
    return {
        "3D TEST",
        "START CINEMATIC",
        "TEST ATTEND",
        "BACK",
    };
}

int ResortMenuController::itemCount() {
    return kItemCount;
}

int ResortMenuController::wrapIndex(int value, int size) {
    if (size <= 0) {
        return 0;
    }
    value %= size;
    return value < 0 ? value + size : value;
}

} // namespace pr::title_screen
