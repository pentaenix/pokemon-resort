#include "ui/title_screen/ResortMenuController.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

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
    using pr::title_screen::ResortMenuAction;
    using pr::title_screen::ResortMenuController;

    ResortMenuController menu;
    expect(ResortMenuController::itemCount() == 4, "resort menu exposes four items");

    const std::vector<std::string> labels = menu.labels();
    expect(labels.size() == 4, "resort menu returns four labels");
    expect(labels[0] == "3D TEST", "first label is 3D TEST");
    expect(labels[1] == "START CINEMATIC", "second label is START CINEMATIC");
    expect(labels[2] == "TEST ATTEND", "third label is TEST ATTEND");
    expect(labels[3] == "BACK", "fourth label is BACK");

    expect(menu.selectedIndex() == 0, "default selection starts on 3D TEST");
    expect(menu.activate() == ResortMenuAction::Open3DTest, "3D TEST maps to Open3DTest");

    expect(menu.selectIndex(1), "selecting START CINEMATIC changes selection");
    expect(menu.activate() == ResortMenuAction::StartCinematic,
           "START CINEMATIC maps to StartCinematic");

    expect(menu.selectIndex(2), "selecting TEST ATTEND changes selection");
    expect(menu.activate() == ResortMenuAction::OpenTestAttend,
           "TEST ATTEND maps to OpenTestAttend");

    expect(menu.selectIndex(3), "selecting BACK changes selection");
    expect(menu.activate() == ResortMenuAction::CloseResortMenu, "BACK maps to CloseResortMenu");

    expect(menu.navigate(1), "navigation wraps downward from BACK");
    expect(menu.selectedIndex() == 0, "downward wrap lands on 3D TEST");
    expect(menu.navigate(-1), "navigation wraps upward from 3D TEST");
    expect(menu.selectedIndex() == 3, "upward wrap lands on BACK");

    expect(!menu.selectIndex(99), "out-of-range selection is rejected");
    expect(menu.selectedIndex() == 3, "rejected selection keeps previous index");

    menu.resetSelection();
    expect(menu.selectedIndex() == 0, "reset returns selection to 3D TEST");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
