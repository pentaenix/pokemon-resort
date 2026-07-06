#pragma once

#include <string>
#include <vector>

namespace pr::title_screen {

enum class ResortMenuAction {
    None,
    Open3DTest,
    StartCinematic,
    OpenTestAttend,
    CloseResortMenu,
}
;

class ResortMenuController {
public:
    int selectedIndex() const { return selected_index_; }
    bool navigate(int delta);
    bool selectIndex(int index);
    void resetSelection();

    ResortMenuAction activate() const;
    std::vector<std::string> labels() const;

    static int itemCount();

private:
    static int wrapIndex(int value, int size);
    int selected_index_ = 0;
};

} // namespace pr::title_screen
