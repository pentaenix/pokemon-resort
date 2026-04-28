#include "ui/TransferSystemScreen.hpp"

namespace pr {

void TransferSystemScreen::openPokemonActionMenu(bool from_game_box, int slot_index, const SDL_Rect& anchor_rect) {
    pokemon_action_menu_.open(from_game_box, slot_index, anchor_rect);
    ui_state_.requestButtonSfx();
}

void TransferSystemScreen::closePokemonActionMenu() {
    pokemon_action_menu_.close();
}

bool TransferSystemScreen::pokemonActionMenuInteractive() const {
    return pokemon_action_menu_.interactive();
}

SDL_Rect TransferSystemScreen::pokemonActionMenuFinalRect() const {
    return pokemon_action_menu_.finalRect(
        pokemon_action_menu_style_,
        window_config_.virtual_width,
        pokemonActionMenuBottomLimitY());
}

int TransferSystemScreen::pokemonActionMenuBottomLimitY() const {
    if (!info_banner_style_.enabled) {
        return window_config_.virtual_height;
    }
    constexpr int kGapAboveBanner = 5;
    const int total_h = std::max(0, info_banner_style_.separator_height) + std::max(0, info_banner_style_.info_height);
    const int banner_top = window_config_.virtual_height - total_h;
    return std::max(1, banner_top - kGapAboveBanner);
}

const PcSlotSpecies* TransferSystemScreen::pokemonActionMenuPokemon() const {
    if (!pokemon_action_menu_.visible()) {
        return nullptr;
    }
    const int slot_index = pokemon_action_menu_.slotIndex();
    if (!pokemon_action_menu_.fromGameBox()) {
        const int box_index = resort_box_browser_.gameBoxIndex();
        if (box_index < 0 || box_index >= static_cast<int>(resort_pc_boxes_.size()) || slot_index < 0) {
            return nullptr;
        }
        const auto& slots = resort_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
        if (slot_index >= static_cast<int>(slots.size())) {
            return nullptr;
        }
        const PcSlotSpecies& slot = slots[static_cast<std::size_t>(slot_index)];
        return slot.occupied() ? &slot : nullptr;
    }
    const int box_index = game_box_browser_.gameBoxIndex();
    if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size()) || slot_index < 0) {
        return nullptr;
    }
    const auto& slots = game_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    if (slot_index >= static_cast<int>(slots.size())) {
        return nullptr;
    }
    const PcSlotSpecies& slot = slots[static_cast<std::size_t>(slot_index)];
    return slot.occupied() ? &slot : nullptr;
}

std::optional<int> TransferSystemScreen::pokemonActionMenuRowAtPoint(int logical_x, int logical_y) const {
    return pokemon_action_menu_.rowAtPoint(
        logical_x,
        logical_y,
        pokemon_action_menu_style_,
        window_config_.virtual_width,
        pokemonActionMenuBottomLimitY());
}

void TransferSystemScreen::activatePokemonActionMenuRow(int row) {
    if (pokemon_action_menu_.actionForRow(row) == transfer_system::PokemonActionMenuController::Action::Move) {
        using Move = transfer_system::PokemonMoveController;
        const Move::SlotRef ref{
            pokemon_action_menu_.fromGameBox() ? Move::Panel::Game : Move::Panel::Resort,
            pokemon_action_menu_.fromGameBox() ? game_box_browser_.gameBoxIndex() : resort_box_browser_.gameBoxIndex(),
            pokemon_action_menu_.slotIndex()};
        const Move::InputMode input_mode = selection_cursor_hidden_after_mouse_
            ? Move::InputMode::Pointer
            : Move::InputMode::Keyboard;
        (void)beginPokemonMoveFromSlot(ref, input_mode, Move::PickupSource::ActionMenu, last_pointer_position_);
        return;
    }
    closePokemonActionMenu();
    ui_state_.requestButtonSfx();
}

void TransferSystemScreen::hoverPokemonActionMenuRow(int logical_x, int logical_y) {
    if (const std::optional<int> row = pokemonActionMenuRowAtPoint(logical_x, logical_y)) {
        pokemon_action_menu_.selectRow(*row);
    }
}

} // namespace pr

