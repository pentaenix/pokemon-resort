#include "ui/TransferSystemScreen.hpp"

#include <SDL.h>

#include <algorithm>

namespace pr {

bool TransferSystemScreen::handleGameBoxSpacePointerPressed(int logical_x, int logical_y) {
    if (!game_save_box_viewport_ || !panelsReadyForInteraction()) {
        return false;
    }

    SDL_Rect r{};
    if (game_save_box_viewport_->getFooterBoxSpaceBounds(r) &&
        logical_x >= r.x && logical_x < r.x + r.w && logical_y >= r.y && logical_y < r.y + r.h) {
        setGameBoxSpaceMode(!game_box_browser_.gameBoxSpaceMode());
        ui_state_.requestButtonSfx();
        closeGameBoxDropdown();
        closeResortBoxDropdown();
        return true;
    }
    if (game_save_box_viewport_->hitTestBoxSpaceScrollArrow(logical_x, logical_y)) {
        stepGameBoxSpaceRowDown();
        closeGameBoxDropdown();
        return true;
    }
    if (!game_box_browser_.gameBoxSpaceMode()) {
        return false;
    }

    const SDL_Rect grid_clip = [&]() {
        SDL_Rect s0{};
        SDL_Rect s29{};
        if (!game_save_box_viewport_->getSlotBounds(0, s0) || !game_save_box_viewport_->getSlotBounds(29, s29)) {
            return SDL_Rect{0, 0, 0, 0};
        }
        const int left = s0.x;
        const int top = s0.y;
        const int right = s29.x + s29.w;
        const int bottom = s29.y + s29.h;
        return SDL_Rect{left, top, std::max(0, right - left), std::max(0, bottom - top)};
    }();
    if (grid_clip.w > 0 && grid_clip.h > 0 &&
        logical_x >= grid_clip.x && logical_x < grid_clip.x + grid_clip.w &&
        logical_y >= grid_clip.y && logical_y < grid_clip.y + grid_clip.h) {
        box_space_interaction_panel_ = BoxSpaceInteractionPanel::Game;
        box_space_drag_active_ = true;
        box_space_drag_last_y_ = logical_y;
        box_space_drag_accum_ = 0.0;
        box_space_quick_drop_pending_ = false;
        box_space_quick_drop_kind_ = BoxSpaceQuickDropKind::None;
        box_space_quick_drop_elapsed_seconds_ = 0.0;
        box_space_quick_drop_target_box_index_ = -1;
        if (const std::optional<FocusNodeId> picked = focusNodeAtPointer(logical_x, logical_y)) {
            if (*picked >= 2000 && *picked <= 2029) {
                // In Box Space, pointer press should update focus immediately so any callouts anchor correctly.
                focus_.setCurrent(*picked);
                selection_cursor_hidden_after_mouse_ = true;
                speech_hover_active_ = true;
                box_space_pressed_cell_ = *picked - 2000;
                const int box_index = game_box_browser_.gameBoxSpaceRowOffset() * 6 + box_space_pressed_cell_;
                SDL_Rect cell_bounds{};
                const bool have_cell =
                    box_index >= 0 && box_index < static_cast<int>(game_pc_boxes_.size()) &&
                    game_save_box_viewport_->getSlotBounds(box_space_pressed_cell_, cell_bounds);
                // If not holding Pokémon/items, a long hold on the cell can turn into a box move (drag-to-scroll stays default).
                if (!pokemon_move_.active() && !multi_pokemon_move_.active() && !held_move_.heldItem()) {
                    if (have_cell) {
                        box_space_box_move_source_box_index_ = box_index;
                        box_space_box_move_hold_.start(
                            SDL_Point{logical_x, logical_y},
                            cell_bounds,
                            box_space_long_press_style_.box_swap_hold_seconds);
                    }
                } else if (held_move_.heldItem()) {
                    if (have_cell && ui_state_.selectedToolIndex() == 3) {
                        box_space_quick_drop_pending_ = true;
                        box_space_quick_drop_kind_ = BoxSpaceQuickDropKind::Item;
                        box_space_quick_drop_elapsed_seconds_ = 0.0;
                        box_space_quick_drop_start_pointer_ = SDL_Point{logical_x, logical_y};
                        box_space_quick_drop_start_cell_bounds_ = cell_bounds;
                        box_space_quick_drop_target_box_index_ = box_index;
                    }
                } else {
                    // Holding Pokémon: press-and-hold (without moving) attempts quick-drop into first empty slots.
                    if (have_cell) {
                        box_space_quick_drop_pending_ = true;
                        box_space_quick_drop_kind_ =
                            multi_pokemon_move_.active()
                                ? BoxSpaceQuickDropKind::PokemonMulti
                                : BoxSpaceQuickDropKind::PokemonSingle;
                        box_space_quick_drop_elapsed_seconds_ = 0.0;
                        box_space_quick_drop_start_pointer_ = SDL_Point{logical_x, logical_y};
                        box_space_quick_drop_start_cell_bounds_ = cell_bounds;
                        box_space_quick_drop_target_box_index_ = box_index;
                    }
                }
            }
        }
        return true;
    }

    if (const std::optional<FocusNodeId> picked = focusNodeAtPointer(logical_x, logical_y)) {
        if (*picked >= 2000 && *picked <= 2029) {
            focus_.setCurrent(*picked);
            return activateFocusedGameSlot();
        }
    }
    return false;
}

bool TransferSystemScreen::handleResortBoxSpacePointerPressed(int logical_x, int logical_y) {
    if (!resort_box_viewport_ || !panelsReadyForInteraction()) {
        return false;
    }

    SDL_Rect r{};
    if (resort_box_viewport_->getFooterBoxSpaceBounds(r) &&
        logical_x >= r.x && logical_x < r.x + r.w && logical_y >= r.y && logical_y < r.y + r.h) {
        setResortBoxSpaceMode(!resort_box_browser_.gameBoxSpaceMode());
        ui_state_.requestButtonSfx();
        closeGameBoxDropdown();
        closeResortBoxDropdown();
        return true;
    }
    if (resort_box_browser_.gameBoxSpaceMode()) {
        SDL_Rect scroll_r{};
        if (resort_box_viewport_->getResortScrollArrowBounds(scroll_r) && logical_x >= scroll_r.x &&
            logical_x < scroll_r.x + scroll_r.w && logical_y >= scroll_r.y && logical_y < scroll_r.y + scroll_r.h) {
            stepResortBoxSpaceRowDown();
            closeResortBoxDropdown();
            return true;
        }
    }
    if (!resort_box_browser_.gameBoxSpaceMode()) {
        return false;
    }

    const SDL_Rect grid_clip = [&]() {
        SDL_Rect s0{};
        SDL_Rect s29{};
        if (!resort_box_viewport_->getSlotBounds(0, s0) || !resort_box_viewport_->getSlotBounds(29, s29)) {
            return SDL_Rect{0, 0, 0, 0};
        }
        const int left = s0.x;
        const int top = s0.y;
        const int right = s29.x + s29.w;
        const int bottom = s29.y + s29.h;
        return SDL_Rect{left, top, std::max(0, right - left), std::max(0, bottom - top)};
    }();
    if (grid_clip.w > 0 && grid_clip.h > 0 &&
        logical_x >= grid_clip.x && logical_x < grid_clip.x + grid_clip.w &&
        logical_y >= grid_clip.y && logical_y < grid_clip.y + grid_clip.h) {
        box_space_interaction_panel_ = BoxSpaceInteractionPanel::Resort;
        box_space_drag_active_ = true;
        box_space_drag_last_y_ = logical_y;
        box_space_drag_accum_ = 0.0;
        box_space_quick_drop_pending_ = false;
        box_space_quick_drop_kind_ = BoxSpaceQuickDropKind::None;
        box_space_quick_drop_elapsed_seconds_ = 0.0;
        box_space_quick_drop_target_box_index_ = -1;
        if (const std::optional<FocusNodeId> picked = focusNodeAtPointer(logical_x, logical_y)) {
            if (*picked >= 1000 && *picked <= 1029) {
                focus_.setCurrent(*picked);
                selection_cursor_hidden_after_mouse_ = true;
                speech_hover_active_ = true;
                box_space_pressed_cell_ = *picked - 1000;
                const int box_index = resort_box_browser_.gameBoxSpaceRowOffset() * 6 + box_space_pressed_cell_;
                SDL_Rect cell_bounds{};
                const bool have_cell =
                    box_index >= 0 && box_index < static_cast<int>(resort_pc_boxes_.size()) &&
                    resort_box_viewport_->getSlotBounds(box_space_pressed_cell_, cell_bounds);
                if (!pokemon_move_.active() && !multi_pokemon_move_.active() && !held_move_.heldItem()) {
                    if (have_cell) {
                        box_space_box_move_source_box_index_ = box_index;
                        box_space_box_move_hold_.start(
                            SDL_Point{logical_x, logical_y},
                            cell_bounds,
                            box_space_long_press_style_.box_swap_hold_seconds);
                    }
                } else if (held_move_.heldItem()) {
                    if (have_cell && ui_state_.selectedToolIndex() == 3) {
                        box_space_quick_drop_pending_ = true;
                        box_space_quick_drop_kind_ = BoxSpaceQuickDropKind::Item;
                        box_space_quick_drop_elapsed_seconds_ = 0.0;
                        box_space_quick_drop_start_pointer_ = SDL_Point{logical_x, logical_y};
                        box_space_quick_drop_start_cell_bounds_ = cell_bounds;
                        box_space_quick_drop_target_box_index_ = box_index;
                    }
                } else {
                    if (have_cell) {
                        box_space_quick_drop_pending_ = true;
                        box_space_quick_drop_kind_ =
                            multi_pokemon_move_.active()
                                ? BoxSpaceQuickDropKind::PokemonMulti
                                : BoxSpaceQuickDropKind::PokemonSingle;
                        box_space_quick_drop_elapsed_seconds_ = 0.0;
                        box_space_quick_drop_start_pointer_ = SDL_Point{logical_x, logical_y};
                        box_space_quick_drop_start_cell_bounds_ = cell_bounds;
                        box_space_quick_drop_target_box_index_ = box_index;
                    }
                }
            }
        }
        return true;
    }

    if (const std::optional<FocusNodeId> picked = focusNodeAtPointer(logical_x, logical_y)) {
        if (*picked >= 1000 && *picked <= 1029) {
            focus_.setCurrent(*picked);
            return activateFocusedResortSlot();
        }
    }
    return false;
}

} // namespace pr

