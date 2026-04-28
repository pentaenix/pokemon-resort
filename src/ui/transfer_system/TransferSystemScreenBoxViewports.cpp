#include "ui/TransferSystemScreen.hpp"

#include <algorithm>
#include <optional>

namespace pr {

void TransferSystemScreen::updateBoxViewportsAndFocusDimming(double dt) {
    const bool item_overlay_active = itemToolActive();
    const bool focus_dimming_active =
        pokemon_action_menu_style_.dim_background_sprites &&
        (pokemon_action_menu_.visible() || item_action_menu_.visible()) &&
        !pokemon_move_.active() &&
        !multi_pokemon_move_.active() &&
        !held_move_.heldItem();
    const std::optional<int> resort_dim_slot =
        focus_dimming_active && !pokemon_action_menu_.fromGameBox() && pokemon_action_menu_.visible()
            ? std::optional<int>(pokemon_action_menu_.slotIndex())
            : (focus_dimming_active && !item_action_menu_.fromGameBox() && item_action_menu_.visible()
                   ? std::optional<int>(item_action_menu_.slotIndex())
                   : std::nullopt);
    const std::optional<int> game_dim_slot =
        focus_dimming_active && pokemon_action_menu_.fromGameBox() && pokemon_action_menu_.visible()
            ? std::optional<int>(pokemon_action_menu_.slotIndex())
            : (focus_dimming_active && item_action_menu_.fromGameBox() && item_action_menu_.visible()
                   ? std::optional<int>(item_action_menu_.slotIndex())
                   : std::nullopt);
    if (resort_box_viewport_) {
        resort_box_viewport_->setItemOverlayActive(item_overlay_active);
        resort_box_viewport_->setFocusDimming(
            focus_dimming_active,
            resort_dim_slot,
            pokemon_action_menu_style_.dim_sprite_mod_color);
        resort_box_viewport_->update(dt);
    }
    if (game_save_box_viewport_) {
        game_save_box_viewport_->setItemOverlayActive(item_overlay_active);
        game_save_box_viewport_->setFocusDimming(
            focus_dimming_active,
            game_dim_slot,
            pokemon_action_menu_style_.dim_sprite_mod_color);
        game_save_box_viewport_->update(dt);
    }
    syncBoxViewportPositions();
}

} // namespace pr

