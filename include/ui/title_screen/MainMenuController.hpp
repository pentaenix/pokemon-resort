#pragma once

#include <cstddef>

namespace pr::title_screen {

enum class MainMenuAction {
    None,
    OpenResort,
    OpenTransfer,
    OpenTrade,
    OpenOptions
};

class MainMenuController {
public:
    explicit MainMenuController(std::size_t item_count = 0);

    int selectedIndex() const { return selected_index_; }
    void setItemCount(std::size_t item_count);
    bool selectIndex(int index);
    bool navigate(int delta);
    MainMenuAction activate() const;

    void selectTransfer();
    void selectTrade();
    void selectOptions();
    void reset();

private:
    static int wrapIndex(int value, int size);
    static MainMenuAction actionForIndex(int index);

    int item_count_ = 0;
    int selected_index_ = 0;
};

} // namespace pr::title_screen
