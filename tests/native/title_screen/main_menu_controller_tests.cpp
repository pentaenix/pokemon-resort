#include "ui/title_screen/MainMenuController.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

} // namespace

int main() {
    using pr::title_screen::MainMenuAction;
    using pr::title_screen::MainMenuController;

    MainMenuController menu(4);
    expect(menu.selectedIndex() == 0, "default selection starts on RESORT");
    expect(menu.activate() == MainMenuAction::OpenResort, "RESORT action maps from index 0");

    expect(menu.navigate(-1), "navigation wraps upward from first item");
    expect(menu.selectedIndex() == 3, "upward wrap lands on OPTIONS");
    expect(menu.activate() == MainMenuAction::OpenOptions, "OPTIONS action maps from index 3");

    expect(menu.navigate(1), "navigation wraps downward from last item");
    expect(menu.selectedIndex() == 0, "downward wrap lands on RESORT");

    expect(menu.selectIndex(1), "selecting TRANSFER changes selection");
    expect(menu.activate() == MainMenuAction::OpenTransfer, "TRANSFER action maps from index 1");

    expect(menu.selectIndex(2), "selecting TRADE changes selection");
    expect(menu.activate() == MainMenuAction::OpenTrade, "TRADE action maps from index 2");

    expect(!menu.selectIndex(99), "out-of-range selection is rejected");
    expect(menu.selectedIndex() == 2, "rejected selection keeps previous index");

    menu.setItemCount(0);
    expect(menu.selectedIndex() == 0, "empty menus normalize selection to 0");
    expect(!menu.navigate(1), "empty menu navigation is a no-op");
    expect(menu.activate() == MainMenuAction::None, "empty menu has no activation action");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
