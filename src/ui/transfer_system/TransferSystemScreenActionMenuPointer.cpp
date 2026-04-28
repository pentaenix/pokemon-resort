#include "ui/TransferSystemScreen.hpp"

#include <SDL.h>

namespace pr {

bool TransferSystemScreen::activateFocusedPokemonSlotActionMenu() {
    if (!normalPokemonToolActive()) {
        return false;
    }
    const FocusNodeId cur = focus_.current();
    SDL_Rect r{};
    if (cur >= 2000 && cur <= 2029 && game_save_box_viewport_) {
        if (game_box_browser_.gameBoxSpaceMode()) {
            return false;
        }
        const int slot = cur - 2000;
        if (gameSaveSlotHasSpecies(slot) && game_save_box_viewport_->getSlotBounds(slot, r)) {
            openPokemonActionMenu(true, slot, r);
            return true;
        }
    }
    if (cur >= 1000 && cur <= 1029 && resort_box_viewport_) {
        if (resort_box_browser_.gameBoxSpaceMode()) {
            return false;
        }
        const int slot = cur - 1000;
        if (resortSlotHasSpecies(slot) && resort_box_viewport_->getSlotBounds(slot, r)) {
            openPokemonActionMenu(false, slot, r);
            return true;
        }
    }
    return false;
}

bool TransferSystemScreen::handlePokemonActionMenuPointerPressed(int logical_x, int logical_y) {
    if (!pokemon_action_menu_.visible()) {
        return false;
    }
    const std::optional<int> row = pokemonActionMenuRowAtPoint(logical_x, logical_y);
    if (row.has_value()) {
        activatePokemonActionMenuRow(*row);
        return true;
    }
    closePokemonActionMenu();
    return true;
}

bool TransferSystemScreen::handleItemActionMenuPointerPressed(int logical_x, int logical_y) {
    if (!item_action_menu_.visible()) {
        return false;
    }
    const std::optional<int> row = item_action_menu_.rowAtPoint(
        logical_x,
        logical_y,
        pokemon_action_menu_style_,
        window_config_.virtual_width,
        pokemonActionMenuBottomLimitY());
    if (row.has_value()) {
        ui_state_.requestButtonSfx();
        item_action_menu_.selectRow(*row);
        const auto action = item_action_menu_.actionForRow(*row);
        if (action == transfer_system::ItemActionMenuController::Action::MoveItem) {
            using Move = transfer_system::PokemonMoveController;
            const Move::SlotRef ref{
                item_action_menu_.fromGameBox() ? Move::Panel::Game : Move::Panel::Resort,
                item_action_menu_.fromGameBox() ? game_box_browser_.gameBoxIndex() : resort_box_browser_.gameBoxIndex(),
                item_action_menu_.slotIndex()};
            PcSlotSpecies* src = mutablePokemonAt(ref);
            if (src && src->occupied() && src->held_item_id > 0) {
                held_move_.pickUpItem(
                    src->held_item_id,
                    src->held_item_name,
                    transfer_system::move::HeldMoveController::PokemonSlotRef{
                        item_action_menu_.fromGameBox()
                            ? transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Game
                            : transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Resort,
                        item_action_menu_.fromGameBox() ? game_box_browser_.gameBoxIndex() : resort_box_browser_.gameBoxIndex(),
                        item_action_menu_.slotIndex()},
                    transfer_system::move::HeldMoveController::InputMode::Pointer,
                    last_pointer_position_);
                src->held_item_id = -1;
                src->held_item_name.clear();
                if (item_action_menu_.fromGameBox()) {
                    markGameBoxesDirty();
                }
                refreshResortBoxViewportModel();
                refreshGameBoxViewportModel();
                requestPickupSfx();
            }
            item_action_menu_.close();
            return true;
        }
        if (action == transfer_system::ItemActionMenuController::Action::PutAway) {
            item_action_menu_.goToPutAwayPage();
            return true;
        }
        if (action == transfer_system::ItemActionMenuController::Action::Back) {
            item_action_menu_.goToRootPage();
            return true;
        }
        // Not implemented yet.
        item_action_menu_.close();
        return true;
    }
    item_action_menu_.close();
    return true;
}

bool TransferSystemScreen::handlePokemonSlotActionPointerPressed(int logical_x, int logical_y) {
    if (!normalPokemonToolActive() && !swapToolActive()) {
        return false;
    }
    auto in = [](int px, int py, const SDL_Rect& r) {
        return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
    };
    SDL_Rect r{};
    if (game_save_box_viewport_ && !game_box_browser_.gameBoxSpaceMode()) {
        if (pointerOverExpandedGameDropdown(logical_x, logical_y)) {
            return false;
        }
        for (int i = 0; i < 30; ++i) {
            if (game_save_box_viewport_->getSlotBounds(i, r) && in(logical_x, logical_y, r)) {
                if (!gameSaveSlotHasSpecies(i)) {
                    return false;
                }
                if (swapToolActive()) {
                    return beginPokemonMoveFromSlot(
                        transfer_system::PokemonMoveController::SlotRef{
                            transfer_system::PokemonMoveController::Panel::Game,
                            game_box_browser_.gameBoxIndex(),
                            i},
                        transfer_system::PokemonMoveController::InputMode::Pointer,
                        transfer_system::PokemonMoveController::PickupSource::SwapTool,
                        last_pointer_position_);
                }
                openPokemonActionMenu(true, i, r);
                return true;
            }
        }
    }
    if (resort_box_viewport_ && !resort_box_browser_.gameBoxSpaceMode()) {
        if (pointerOverExpandedResortDropdown(logical_x, logical_y)) {
            return false;
        }
        for (int i = 0; i < 30; ++i) {
            if (resort_box_viewport_->getSlotBounds(i, r) && in(logical_x, logical_y, r)) {
                if (!resortSlotHasSpecies(i)) {
                    return false;
                }
                if (swapToolActive()) {
                    return beginPokemonMoveFromSlot(
                        transfer_system::PokemonMoveController::SlotRef{
                            transfer_system::PokemonMoveController::Panel::Resort,
                            resort_box_browser_.gameBoxIndex(),
                            i},
                        transfer_system::PokemonMoveController::InputMode::Pointer,
                        transfer_system::PokemonMoveController::PickupSource::SwapTool,
                        last_pointer_position_);
                }
                openPokemonActionMenu(false, i, r);
                return true;
            }
        }
    }
    return false;
}

bool TransferSystemScreen::handleItemSlotActionPointerPressed(int logical_x, int logical_y) {
    if (!itemToolActive() || game_box_browser_.dropdownOpenTarget() || resort_box_browser_.dropdownOpenTarget() ||
        held_move_.heldItem() || pokemon_move_.active() || multi_pokemon_move_.active()) {
        return false;
    }
    auto in = [](int px, int py, const SDL_Rect& r) {
        return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
    };
    SDL_Rect r{};
    if (game_save_box_viewport_ && !game_box_browser_.gameBoxSpaceMode()) {
        if (pointerOverExpandedGameDropdown(logical_x, logical_y)) {
            return false;
        }
        for (int i = 0; i < 30; ++i) {
            if (game_save_box_viewport_->getSlotBounds(i, r) && in(logical_x, logical_y, r)) {
                if (!gameSlotHasHeldItem(i)) {
                    return false;
                }
                item_action_menu_.setPutAwayGameLabel(selection_game_title_);
                item_action_menu_.open(true, i, r);
                ui_state_.requestButtonSfx();
                return true;
            }
        }
    }
    if (resort_box_viewport_ && !resort_box_browser_.gameBoxSpaceMode()) {
        if (pointerOverExpandedResortDropdown(logical_x, logical_y)) {
            return false;
        }
        const int resort_bi = resort_box_browser_.gameBoxIndex();
        if (resort_bi < 0 || resort_bi >= static_cast<int>(resort_pc_boxes_.size())) {
            return false;
        }
        const auto& resort_slots = resort_pc_boxes_[static_cast<std::size_t>(resort_bi)].slots;
        for (int i = 0; i < 30; ++i) {
            if (resort_box_viewport_->getSlotBounds(i, r) && in(logical_x, logical_y, r)) {
                if (i < 0 || i >= static_cast<int>(resort_slots.size())) {
                    return false;
                }
                if (!resort_slots[static_cast<std::size_t>(i)].occupied() ||
                    resort_slots[static_cast<std::size_t>(i)].held_item_id <= 0) {
                    return false;
                }
                item_action_menu_.setPutAwayGameLabel(selection_game_title_);
                item_action_menu_.open(false, i, r);
                ui_state_.requestButtonSfx();
                return true;
            }
        }
    }
    return false;
}

} // namespace pr

