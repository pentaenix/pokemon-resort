#include "ui/TransferSystemScreen.hpp"

#include <algorithm>
#include <cmath>

namespace pr {

bool TransferSystemScreen::captureAdvanceForLongPress() const {
    const bool game_focus =
        game_box_browser_.gameBoxSpaceMode() && focusedBoxSpaceBoxIndex().has_value();
    const bool resort_focus =
        resort_box_browser_.gameBoxSpaceMode() && focusedResortBoxSpaceBoxIndex().has_value();
    if (!game_focus && !resort_focus) {
        return false;
    }
    if (pokemon_move_.active() || multi_pokemon_move_.active()) {
        return box_space_long_press_style_.quick_drop_hold_seconds > 1e-6;
    }
    return box_space_long_press_style_.box_swap_hold_seconds > 1e-6;
}

std::optional<double> TransferSystemScreen::advanceLongPressSeconds() const {
    const bool game_focus =
        game_box_browser_.gameBoxSpaceMode() && focusedBoxSpaceBoxIndex().has_value();
    const bool resort_focus =
        resort_box_browser_.gameBoxSpaceMode() && focusedResortBoxSpaceBoxIndex().has_value();
    if (!game_focus && !resort_focus) {
        return std::nullopt;
    }
    return (pokemon_move_.active() || multi_pokemon_move_.active())
        ? std::optional<double>(box_space_long_press_style_.quick_drop_hold_seconds)
        : std::optional<double>(box_space_long_press_style_.box_swap_hold_seconds);
}

void TransferSystemScreen::onAdvanceLongPress() {
    std::optional<int> box_index;
    bool resort_panel = false;
    if (resort_box_browser_.gameBoxSpaceMode()) {
        box_index = focusedResortBoxSpaceBoxIndex();
        resort_panel = box_index.has_value();
    }
    if (!box_index.has_value() && game_box_browser_.gameBoxSpaceMode()) {
        box_index = focusedBoxSpaceBoxIndex();
        resort_panel = false;
    }
    if (!box_index.has_value()) {
        return;
    }
    box_space_interaction_panel_ =
        resort_panel ? BoxSpaceInteractionPanel::Resort : BoxSpaceInteractionPanel::Game;
    if (pokemon_move_.active() || multi_pokemon_move_.active()) {
        if (!completeBoxSpaceQuickDrop(*box_index)) {
            triggerHeldSpriteRejectFeedback();
        }
        clearBoxSpaceQuickDropGesture();
        return;
    }
    held_move_.pickUpBox(
        resort_panel ? transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Resort
                     : transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Game,
        *box_index,
        transfer_system::move::HeldMoveController::InputMode::Keyboard,
        last_pointer_position_);
    refreshGameBoxViewportModel();
    refreshResortBoxViewportModel();
    requestPickupSfx();
    clearBoxSpaceQuickDropGesture();
}

void TransferSystemScreen::applyKeyboardBoxSpaceQuickDropCharge(double elapsed_seconds, KeyboardBoxSpaceChargeKind source) {
    if (!pokemon_move_.active() && !multi_pokemon_move_.active()) {
        return;
    }
    if (!game_box_browser_.gameBoxSpaceMode() && !resort_box_browser_.gameBoxSpaceMode()) {
        return;
    }
    const FocusNodeId cur = focus_.current();

    if (cur >= 1000 && cur <= 1029 && resort_box_browser_.gameBoxSpaceMode() && resort_box_viewport_) {
        const auto tgt = focusedResortBoxSpaceBoxIndex();
        if (!tgt.has_value()) {
            return;
        }
        box_space_quick_drop_kind_ =
            multi_pokemon_move_.active() ? BoxSpaceQuickDropKind::PokemonMulti : BoxSpaceQuickDropKind::PokemonSingle;
        box_space_keyboard_charge_kind_ = source;
        box_space_quick_drop_pending_ = true;
        box_space_quick_drop_elapsed_seconds_ = elapsed_seconds;
        box_space_interaction_panel_ = BoxSpaceInteractionPanel::Resort;
        box_space_pressed_cell_ = cur - 1000;
        box_space_quick_drop_target_box_index_ = *tgt;
        SDL_Rect cell_bounds{};
        if (resort_box_viewport_->getSlotBounds(box_space_pressed_cell_, cell_bounds)) {
            box_space_quick_drop_start_cell_bounds_ = cell_bounds;
            box_space_quick_drop_start_pointer_ =
                SDL_Point{cell_bounds.x + cell_bounds.w / 2, cell_bounds.y + cell_bounds.h / 2};
        }
        return;
    }
    if (cur >= 2000 && cur <= 2029 && game_box_browser_.gameBoxSpaceMode() && game_save_box_viewport_) {
        const auto tgt = focusedBoxSpaceBoxIndex();
        if (!tgt.has_value()) {
            return;
        }
        box_space_quick_drop_kind_ =
            multi_pokemon_move_.active() ? BoxSpaceQuickDropKind::PokemonMulti : BoxSpaceQuickDropKind::PokemonSingle;
        box_space_keyboard_charge_kind_ = source;
        box_space_quick_drop_pending_ = true;
        box_space_quick_drop_elapsed_seconds_ = elapsed_seconds;
        box_space_interaction_panel_ = BoxSpaceInteractionPanel::Game;
        box_space_pressed_cell_ = cur - 2000;
        box_space_quick_drop_target_box_index_ = *tgt;
        SDL_Rect cell_bounds{};
        if (game_save_box_viewport_->getSlotBounds(box_space_pressed_cell_, cell_bounds)) {
            box_space_quick_drop_start_cell_bounds_ = cell_bounds;
            box_space_quick_drop_start_pointer_ =
                SDL_Point{cell_bounds.x + cell_bounds.w / 2, cell_bounds.y + cell_bounds.h / 2};
        }
    }
}

void TransferSystemScreen::applyKeyboardBoxSpaceAdvanceBoxPickupCharge(double elapsed_seconds) {
    if (pokemon_move_.active() || multi_pokemon_move_.active() || held_move_.heldItem() || held_move_.heldBox()) {
        return;
    }
    const FocusNodeId cur = focus_.current();

    if (cur >= 1000 && cur <= 1029 && resort_box_browser_.gameBoxSpaceMode() && resort_box_viewport_) {
        const auto tgt = focusedResortBoxSpaceBoxIndex();
        if (!tgt.has_value()) {
            return;
        }
        box_space_keyboard_charge_kind_ = KeyboardBoxSpaceChargeKind::AdvanceBoxPickup;
        box_space_quick_drop_pending_ = true;
        box_space_quick_drop_kind_ = BoxSpaceQuickDropKind::None;
        box_space_quick_drop_elapsed_seconds_ = elapsed_seconds;
        box_space_interaction_panel_ = BoxSpaceInteractionPanel::Resort;
        box_space_pressed_cell_ = cur - 1000;
        box_space_quick_drop_target_box_index_ = *tgt;
        SDL_Rect cell_bounds{};
        if (resort_box_viewport_->getSlotBounds(box_space_pressed_cell_, cell_bounds)) {
            box_space_quick_drop_start_cell_bounds_ = cell_bounds;
            box_space_quick_drop_start_pointer_ =
                SDL_Point{cell_bounds.x + cell_bounds.w / 2, cell_bounds.y + cell_bounds.h / 2};
        }
        return;
    }
    if (cur >= 2000 && cur <= 2029 && game_box_browser_.gameBoxSpaceMode() && game_save_box_viewport_) {
        const auto tgt = focusedBoxSpaceBoxIndex();
        if (!tgt.has_value()) {
            return;
        }
        box_space_keyboard_charge_kind_ = KeyboardBoxSpaceChargeKind::AdvanceBoxPickup;
        box_space_quick_drop_pending_ = true;
        box_space_quick_drop_kind_ = BoxSpaceQuickDropKind::None;
        box_space_quick_drop_elapsed_seconds_ = elapsed_seconds;
        box_space_interaction_panel_ = BoxSpaceInteractionPanel::Game;
        box_space_pressed_cell_ = cur - 2000;
        box_space_quick_drop_target_box_index_ = *tgt;
        SDL_Rect cell_bounds{};
        if (game_save_box_viewport_->getSlotBounds(box_space_pressed_cell_, cell_bounds)) {
            box_space_quick_drop_start_cell_bounds_ = cell_bounds;
            box_space_quick_drop_start_pointer_ =
                SDL_Point{cell_bounds.x + cell_bounds.w / 2, cell_bounds.y + cell_bounds.h / 2};
        }
    }
}

void TransferSystemScreen::onAdvanceLongPressCharge(double elapsed_seconds) {
    if (pokemon_move_.active() || multi_pokemon_move_.active()) {
        applyKeyboardBoxSpaceQuickDropCharge(elapsed_seconds, KeyboardBoxSpaceChargeKind::AdvanceQuickDrop);
        return;
    }
    applyKeyboardBoxSpaceAdvanceBoxPickupCharge(elapsed_seconds);
}

void TransferSystemScreen::onAdvanceLongPressEnded(bool long_press_action_fired) {
    if (box_space_keyboard_charge_kind_ != KeyboardBoxSpaceChargeKind::AdvanceQuickDrop &&
        box_space_keyboard_charge_kind_ != KeyboardBoxSpaceChargeKind::AdvanceBoxPickup) {
        return;
    }
    box_space_keyboard_charge_kind_ = KeyboardBoxSpaceChargeKind::None;
    if (!long_press_action_fired) {
        clearBoxSpaceQuickDropGesture();
    }
}

bool TransferSystemScreen::captureNavigate2dForLongPress(int dx, int dy) const {
    if (dx != 0 || dy != 1) {
        return false;
    }
    if (keyboard_multi_marquee_active_) {
        return false;
    }
    if (!pokemon_move_.active() && !multi_pokemon_move_.active()) {
        return false;
    }
    const bool game_focus =
        game_box_browser_.gameBoxSpaceMode() && focusedBoxSpaceBoxIndex().has_value();
    const bool resort_focus =
        resort_box_browser_.gameBoxSpaceMode() && focusedResortBoxSpaceBoxIndex().has_value();
    return (game_focus || resort_focus) && box_space_long_press_style_.quick_drop_hold_seconds > 1e-6;
}

std::optional<double> TransferSystemScreen::navigate2dLongPressSeconds(int dx, int dy) const {
    if (dx != 0 || dy != 1) {
        return std::nullopt;
    }
    if (keyboard_multi_marquee_active_) {
        return std::nullopt;
    }
    if (!pokemon_move_.active() && !multi_pokemon_move_.active()) {
        return std::nullopt;
    }
    const bool game_focus =
        game_box_browser_.gameBoxSpaceMode() && focusedBoxSpaceBoxIndex().has_value();
    const bool resort_focus =
        resort_box_browser_.gameBoxSpaceMode() && focusedResortBoxSpaceBoxIndex().has_value();
    if (!game_focus && !resort_focus) {
        return std::nullopt;
    }
    return box_space_long_press_style_.quick_drop_hold_seconds;
}

void TransferSystemScreen::onNavigate2dLongPress(int dx, int dy) {
    if (dx != 0 || dy != 1) {
        return;
    }
    if (!pokemon_move_.active() && !multi_pokemon_move_.active()) {
        return;
    }
    std::optional<int> box_index;
    bool resort_panel = false;
    if (resort_box_browser_.gameBoxSpaceMode()) {
        box_index = focusedResortBoxSpaceBoxIndex();
        resort_panel = box_index.has_value();
    }
    if (!box_index.has_value() && game_box_browser_.gameBoxSpaceMode()) {
        box_index = focusedBoxSpaceBoxIndex();
        resort_panel = false;
    }
    if (!box_index.has_value()) {
        return;
    }
    box_space_interaction_panel_ =
        resort_panel ? BoxSpaceInteractionPanel::Resort : BoxSpaceInteractionPanel::Game;
    if (!completeBoxSpaceQuickDrop(*box_index)) {
        triggerHeldSpriteRejectFeedback();
    }
    clearBoxSpaceQuickDropGesture();
}

void TransferSystemScreen::onNavigationLongPressCharge(double elapsed_seconds, int dx, int dy) {
    if (dx != 0 || dy != 1) {
        return;
    }
    applyKeyboardBoxSpaceQuickDropCharge(elapsed_seconds, KeyboardBoxSpaceChargeKind::NavigateQuickDrop);
}

void TransferSystemScreen::onNavigationLongPressEnded(bool long_press_action_fired) {
    if (box_space_keyboard_charge_kind_ != KeyboardBoxSpaceChargeKind::NavigateQuickDrop) {
        return;
    }
    box_space_keyboard_charge_kind_ = KeyboardBoxSpaceChargeKind::None;
    if (!long_press_action_fired) {
        clearBoxSpaceQuickDropGesture();
    }
}

void TransferSystemScreen::clearBoxSpaceQuickDropGesture() {
    box_space_quick_drop_pending_ = false;
    box_space_quick_drop_kind_ = BoxSpaceQuickDropKind::None;
    box_space_quick_drop_target_box_index_ = -1;
    box_space_quick_drop_elapsed_seconds_ = 0.0;
    box_space_pressed_cell_ = -1;
    box_space_interaction_panel_ = BoxSpaceInteractionPanel::None;
    box_space_keyboard_charge_kind_ = KeyboardBoxSpaceChargeKind::None;
    box_space_wiggle_phase_ = 0.0;
    for (int& w : box_space_slot_wiggle_dx_) {
        w = 0;
    }
}

void TransferSystemScreen::triggerHeldSpriteRejectFeedback() {
    ui_state_.requestErrorSfx();
    held_sprite_shake_timer_ = std::max(held_sprite_shake_timer_, 0.28);
}

bool TransferSystemScreen::tryGiveHeldItemToFirstEligiblePokemonInGameBox(int box_index) {
    const auto* held = held_move_.heldItem();
    if (!held || box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
        return false;
    }
    auto& slots = game_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        if (!gameSaveSlotAccessible(i)) {
            continue;
        }
        auto& slot = slots[static_cast<std::size_t>(i)];
        if (slot.occupied() && slot.held_item_id <= 0) {
            if (!syncGamePcSlotHeldItemPayload(slot, held->item_id, held->item_name)) {
                ui_state_.requestErrorSfx();
                return false;
            }
            held_move_.clear();
            markGameBoxesDirty();
            refreshGameBoxViewportModel();
            refreshResortBoxViewportModel();
            requestPutdownSfx();
            return true;
        }
    }
    return false;
}

bool TransferSystemScreen::tryGiveHeldItemToFirstEligiblePokemonInResortBox(int box_index) {
    const auto* held = held_move_.heldItem();
    if (!held || box_index < 0 || box_index >= static_cast<int>(resort_pc_boxes_.size())) {
        return false;
    }
    auto& slots = resort_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    for (auto& slot : slots) {
        if (slot.occupied() && slot.held_item_id <= 0) {
            slot.held_item_id = held->item_id;
            slot.held_item_name = held->item_name;
            held_move_.clear();
            markResortBoxesDirty();
            refreshGameBoxViewportModel();
            refreshResortBoxViewportModel();
            requestPutdownSfx();
            return true;
        }
    }
    return false;
}

bool TransferSystemScreen::completeBoxSpaceQuickDrop(int target_box) {
    BoxSpaceQuickDropKind kind = box_space_quick_drop_kind_;
    if (kind == BoxSpaceQuickDropKind::None) {
        if (multi_pokemon_move_.active()) {
            kind = BoxSpaceQuickDropKind::PokemonMulti;
        } else if (pokemon_move_.active()) {
            kind = BoxSpaceQuickDropKind::PokemonSingle;
        } else if (held_move_.heldItem()) {
            kind = BoxSpaceQuickDropKind::Item;
        }
    }
    const bool resort_panel = box_space_interaction_panel_ == BoxSpaceInteractionPanel::Resort;
    switch (kind) {
        case BoxSpaceQuickDropKind::PokemonMulti:
            return resort_panel ? dropHeldMultiPokemonIntoFirstEmptyResortBox(target_box)
                                : dropHeldMultiPokemonIntoFirstEmptySlotsInBox(target_box);
        case BoxSpaceQuickDropKind::PokemonSingle:
            return resort_panel ? dropHeldPokemonIntoFirstEmptySlotInResortBox(target_box)
                                : dropHeldPokemonIntoFirstEmptySlotInBox(target_box);
        case BoxSpaceQuickDropKind::Item:
            return resort_panel ? tryGiveHeldItemToFirstEligiblePokemonInResortBox(target_box)
                                : tryGiveHeldItemToFirstEligiblePokemonInGameBox(target_box);
        default:
            return false;
    }
}

void TransferSystemScreen::updateBoxSpaceQuickDropVisuals(double dt) {
    for (int& w : box_space_slot_wiggle_dx_) {
        w = 0;
    }

    held_sprite_shake_offset_px_ = 0;
    if (held_sprite_shake_timer_ > 0.0) {
        held_sprite_shake_timer_ -= dt;
        held_sprite_shake_phase_ += dt * 52.0;
        held_sprite_shake_offset_px_ =
            static_cast<int>(std::lround(std::sin(held_sprite_shake_phase_) * 7.0));
        if (held_sprite_shake_timer_ <= 0.0) {
            held_sprite_shake_timer_ = 0.0;
            held_sprite_shake_offset_px_ = 0;
        }
    }

    float collapse_target = 0.f;
    if (box_space_quick_drop_pending_ &&
        box_space_quick_drop_kind_ == BoxSpaceQuickDropKind::PokemonMulti &&
        multi_pokemon_move_.active() &&
        ui_state_.selectedToolIndex() == 0 &&
        box_space_quick_drop_elapsed_seconds_ >= box_space_long_press_style_.long_press_feedback_seconds &&
        box_space_interaction_panel_ != BoxSpaceInteractionPanel::None) {
        collapse_target = 1.f;
    }
    box_space_multi_collapse_t_ +=
        (collapse_target - box_space_multi_collapse_t_) * static_cast<float>(std::clamp(dt * 14.0, 0.0, 1.0));

    const double feedback_sec = box_space_long_press_style_.long_press_feedback_seconds;
    const bool quick_drop_slot_wiggle =
        box_space_quick_drop_pending_ && box_space_quick_drop_elapsed_seconds_ >= feedback_sec &&
        box_space_pressed_cell_ >= 0 && box_space_pressed_cell_ < 30;
    // Mouse: empty-hand long press on a tile uses HoldWithinRect (box move), not quick-drop pending.
    const bool box_pickup_pointer_wiggle =
        box_space_box_move_hold_.active && box_space_box_move_hold_.elapsed_seconds >= feedback_sec &&
        box_space_pressed_cell_ >= 0 && box_space_pressed_cell_ < 30;

    if (quick_drop_slot_wiggle || box_pickup_pointer_wiggle) {
        box_space_wiggle_phase_ += dt * 32.0;
        box_space_slot_wiggle_dx_[static_cast<std::size_t>(box_space_pressed_cell_)] =
            static_cast<int>(std::lround(std::sin(box_space_wiggle_phase_) * 5.0));
    }

    const bool any_wiggle =
        std::any_of(box_space_slot_wiggle_dx_.begin(), box_space_slot_wiggle_dx_.end(), [](int x) { return x != 0; });
    const bool pending_game =
        box_space_quick_drop_pending_ && box_space_interaction_panel_ == BoxSpaceInteractionPanel::Game;
    const bool pending_resort =
        box_space_quick_drop_pending_ && box_space_interaction_panel_ == BoxSpaceInteractionPanel::Resort;
    const bool game_box_space_visual =
        game_save_box_viewport_ && game_box_browser_.gameBoxSpaceMode() &&
        (pending_game || box_space_multi_collapse_t_ > 0.02f || held_sprite_shake_timer_ > 0.0 ||
         (box_pickup_pointer_wiggle && box_space_interaction_panel_ == BoxSpaceInteractionPanel::Game) ||
         (any_wiggle && box_space_interaction_panel_ == BoxSpaceInteractionPanel::Game));
    const bool resort_box_space_visual =
        resort_box_viewport_ && resort_box_browser_.gameBoxSpaceMode() &&
        (pending_resort || box_space_multi_collapse_t_ > 0.02f || held_sprite_shake_timer_ > 0.0 ||
         (box_pickup_pointer_wiggle && box_space_interaction_panel_ == BoxSpaceInteractionPanel::Resort) ||
         (any_wiggle && box_space_interaction_panel_ == BoxSpaceInteractionPanel::Resort));
    if (game_box_space_visual) {
        game_save_box_viewport_->snapContentToModel(gameBoxSpaceViewportModelAt(game_box_browser_.gameBoxSpaceRowOffset()));
    }
    if (resort_box_space_visual) {
        resort_box_viewport_->snapContentToModel(
            resortBoxSpaceViewportModelAt(resort_box_browser_.gameBoxSpaceRowOffset()));
    }
}

} // namespace pr

