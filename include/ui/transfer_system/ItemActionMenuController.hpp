#pragma once

#include "core/Types.hpp"

#include <SDL.h>

#include <optional>
#include <string>

namespace pr::transfer_system {

class ItemActionMenuController {
public:
    enum class Action {
        MoveItem,
        SwapItem,
        PutAway,
        PutAwayResort,
        PutAwayGame,
        Back,
        Cancel,
    };

    enum class Page {
        Root,
        PutAway,
    };

    void setPutAwayGameLabel(std::string game_title);
    void setPreferredWidth(int px);

    void open(bool from_game_box, int slot_index, const SDL_Rect& anchor_rect);
    void close();
    void clear();
    void update(double dt, const GameTransferPokemonActionMenuStyle& style);

    bool visible() const { return visible_; }
    bool closing() const { return closing_; }
    bool interactive() const { return visible_ && !closing_ && t_ > 0.65; }
    bool fromGameBox() const { return from_game_box_; }
    int slotIndex() const { return slot_index_; }
    int selectedRow() const { return selected_row_; }
    const SDL_Rect& anchorRect() const { return anchor_rect_; }
    double transitionT() const { return t_; }
    Page page() const { return page_; }
    int rowCount() const;
    const std::string& labelAt(int row) const;
    void goToRootPage();
    void goToPutAwayPage();

    SDL_Rect finalRect(
        const GameTransferPokemonActionMenuStyle& style,
        int screen_w,
        int screen_h) const;
    std::optional<int> rowAtPoint(
        int logical_x,
        int logical_y,
        const GameTransferPokemonActionMenuStyle& style,
        int screen_w,
        int screen_h) const;
    void stepSelection(int delta);
    void selectRow(int row);
    Action selectedAction() const;
    Action actionForRow(int row) const;

private:
    bool visible_ = false;
    bool closing_ = false;
    bool from_game_box_ = false;
    int slot_index_ = -1;
    int selected_row_ = 0;
    SDL_Rect anchor_rect_{0, 0, 0, 0};
    double t_ = 0.0;
    Page page_ = Page::Root;
    std::string put_away_game_label_ = "Game";
    int preferred_width_px_ = 0;
};

} // namespace pr::transfer_system

