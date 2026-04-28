#include "ui/TransferSystemScreen.hpp"

#include <SDL_ttf.h>

#include <algorithm>

namespace pr {

void TransferSystemScreen::updatePokemonActionMenu(double dt) {
    pokemon_action_menu_.update(dt, pokemon_action_menu_style_);
}

void TransferSystemScreen::updateActionMenus(double dt) {
    updatePokemonActionMenu(dt);

    item_action_menu_.update(dt, pokemon_action_menu_style_);
    if (item_action_menu_.visible() && pokemon_action_menu_font_.get()) {
        int max_w = 0;
        for (int i = 0; i < item_action_menu_.rowCount(); ++i) {
            const std::string& label = item_action_menu_.labelAt(i);
            int w = 0;
            int h = 0;
            if (!label.empty() && TTF_SizeUTF8(pokemon_action_menu_font_.get(), label.c_str(), &w, &h) == 0) {
                max_w = std::max(max_w, w);
            }
        }
        // Match renderer padding: text starts at x+28, add right padding + some slack.
        const int desired = std::max(140, max_w + 88);
        item_action_menu_.setPreferredWidth(desired);
    }
}

} // namespace pr

