#include "ui/TransferSystemScreen.hpp"

#include <algorithm>

namespace pr {

bool TransferSystemScreen::dropHeldPokemonIntoFirstEmptySlotInBox(int box_index) {
    using Move = transfer_system::PokemonMoveController;
    if (!pokemon_move_.active()) {
        return false;
    }
    if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
        return false;
    }
    auto& slots = game_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        if (!gameSaveSlotAccessible(i)) {
            continue;
        }
        if (!slots[static_cast<std::size_t>(i)].occupied()) {
            return dropHeldPokemonAt(Move::SlotRef{Move::Panel::Game, box_index, i});
        }
    }
    return false;
}

bool TransferSystemScreen::dropHeldPokemonIntoFirstEmptySlotInResortBox(int box_index) {
    using Move = transfer_system::PokemonMoveController;
    if (!pokemon_move_.active()) {
        return false;
    }
    if (box_index < 0 || box_index >= static_cast<int>(resort_pc_boxes_.size())) {
        return false;
    }
    auto& slots = resort_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        if (!slots[static_cast<std::size_t>(i)].occupied()) {
            return dropHeldPokemonAt(Move::SlotRef{Move::Panel::Resort, box_index, i});
        }
    }
    return false;
}

bool TransferSystemScreen::gameBoxHasEmptySlot(int box_index) const {
    if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
        return false;
    }
    const auto& slots = game_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        if (gameSaveSlotAccessible(i) && !slots[static_cast<std::size_t>(i)].occupied()) {
            return true;
        }
    }
    return false;
}

bool TransferSystemScreen::gameBoxHasPreviewContent(int box_index) const {
    if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
        return false;
    }
    const auto& slots = game_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    const int visible_slots = gameSaveSlotsPerBox();
    for (int i = 0; i < visible_slots && i < static_cast<int>(slots.size()); ++i) {
        if (slots[static_cast<std::size_t>(i)].occupied()) {
            return true;
        }
    }
    return false;
}

bool TransferSystemScreen::resortBoxHasPreviewContent(int box_index) const {
    if (box_index < 0 || box_index >= static_cast<int>(resort_pc_boxes_.size())) {
        return false;
    }
    const auto& slots = resort_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    return std::any_of(slots.begin(), slots.end(), [](const PcSlotSpecies& slot) {
        return slot.occupied();
    });
}

bool TransferSystemScreen::boxFitsInGameSaveSlots(const TransferSaveSelection::PcBox& box) const {
    const int capacity = gameSaveSlotsPerBox();
    if (capacity >= 30) {
        return true;
    }
    for (int i = capacity; i < static_cast<int>(box.slots.size()); ++i) {
        if (box.slots[static_cast<std::size_t>(i)].occupied()) {
            return false;
        }
    }
    return true;
}

bool TransferSystemScreen::gameSlotHasHeldItem(int slot_index) const {
    if (!gameSaveSlotAccessible(slot_index)) {
        return false;
    }
    const int game_box_index = game_box_browser_.gameBoxIndex();
    if (game_box_index < 0 || game_box_index >= static_cast<int>(game_pc_boxes_.size())) {
        return false;
    }
    const auto& slots = game_pc_boxes_[static_cast<std::size_t>(game_box_index)].slots;
    return slot_index < static_cast<int>(slots.size()) &&
           slots[static_cast<std::size_t>(slot_index)].occupied() &&
           slots[static_cast<std::size_t>(slot_index)].held_item_id > 0;
}

int TransferSystemScreen::gameSaveSlotsPerBox() const {
    if (!transfer_selection_.pc_boxes.empty()) {
        for (const auto& box : transfer_selection_.pc_boxes) {
            if (box.native_slot_count > 0) {
                return std::clamp(box.native_slot_count, 1, 30);
            }
            if (!box.slots.empty()) {
                return static_cast<int>(std::min<std::size_t>(30, box.slots.size()));
            }
        }
    }
    if (!transfer_selection_.box1_slots.empty()) {
        return static_cast<int>(std::min<std::size_t>(30, transfer_selection_.box1_slots.size()));
    }
    return 30;
}

bool TransferSystemScreen::gameSaveSlotAccessible(int slot_index) const {
    return slot_index >= 0 && slot_index < gameSaveSlotsPerBox();
}

bool TransferSystemScreen::resortSlotHasHeldItem(int slot_index) const {
    if (slot_index < 0 || slot_index >= 30) {
        return false;
    }
    const int resort_box_index = resort_box_browser_.gameBoxIndex();
    if (resort_box_index < 0 || resort_box_index >= static_cast<int>(resort_pc_boxes_.size())) {
        return false;
    }
    const auto& slots = resort_pc_boxes_[static_cast<std::size_t>(resort_box_index)].slots;
    return slot_index < static_cast<int>(slots.size()) &&
           slots[static_cast<std::size_t>(slot_index)].occupied() &&
           slots[static_cast<std::size_t>(slot_index)].held_item_id > 0;
}

std::string TransferSystemScreen::gameSlotHeldItemName(int slot_index) const {
    if (!gameSlotHasHeldItem(slot_index)) {
        return {};
    }
    const int game_box_index = game_box_browser_.gameBoxIndex();
    const auto& slot = game_pc_boxes_[static_cast<std::size_t>(game_box_index)].slots[static_cast<std::size_t>(slot_index)];
    return !slot.held_item_name.empty() ? slot.held_item_name : ("Item " + std::to_string(slot.held_item_id));
}

int TransferSystemScreen::gameBoxSpaceMaxRowOffset() const {
    return game_box_browser_.gameBoxSpaceMaxRowOffset(static_cast<int>(game_pc_boxes_.size()));
}

int TransferSystemScreen::resortBoxSpaceMaxRowOffset() const {
    return resort_box_browser_.gameBoxSpaceMaxRowOffset(static_cast<int>(resort_pc_boxes_.size()));
}

BoxViewportModel TransferSystemScreen::resortBoxViewportModel() const {
    return resortBoxViewportModelAt(resort_box_browser_.gameBoxIndex());
}

std::optional<transfer_system::PokemonMoveController::SlotRef> TransferSystemScreen::slotRefForFocus(FocusNodeId focus_id) const {
    using Move = transfer_system::PokemonMoveController;
    if (focus_id >= 1000 && focus_id <= 1029 && !resort_box_browser_.gameBoxSpaceMode()) {
        return Move::SlotRef{Move::Panel::Resort, resort_box_browser_.gameBoxIndex(), focus_id - 1000};
    }
    if (focus_id >= 2000 && focus_id <= 2029 && !game_box_browser_.gameBoxSpaceMode()) {
        if (!gameSaveSlotAccessible(focus_id - 2000)) {
            return std::nullopt;
        }
        return Move::SlotRef{Move::Panel::Game, game_box_browser_.gameBoxIndex(), focus_id - 2000};
    }
    return std::nullopt;
}

std::optional<transfer_system::PokemonMoveController::SlotRef> TransferSystemScreen::slotRefAtPointer(int logical_x, int logical_y) const {
    const std::optional<FocusNodeId> focus_id = focusNodeAtPointer(logical_x, logical_y);
    return focus_id ? slotRefForFocus(*focus_id) : std::nullopt;
}

bool TransferSystemScreen::pointerOverExpandedGameDropdown(int logical_x, int logical_y) const {
    if (!box_name_dropdown_style_.enabled || !game_box_browser_.dropdownOpenTarget() ||
        game_box_browser_.dropdownExpandT() <= 0.08f || static_cast<int>(game_pc_boxes_.size()) < 2) {
        return false;
    }
    SDL_Rect outer{};
    int list_h = 0;
    int list_clip_y = 0;
    if (!computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
        return false;
    }
    return logical_x >= outer.x && logical_x < outer.x + outer.w && logical_y >= outer.y && logical_y < outer.y + outer.h;
}

bool TransferSystemScreen::pointerOverExpandedResortDropdown(int logical_x, int logical_y) const {
    if (!box_name_dropdown_style_.enabled || !resort_box_browser_.dropdownOpenTarget() ||
        resort_box_browser_.dropdownExpandT() <= 0.08f || static_cast<int>(resort_pc_boxes_.size()) < 2) {
        return false;
    }
    SDL_Rect outer{};
    int list_h = 0;
    int list_clip_y = 0;
    if (!computeResortBoxDropdownOuterRect(
            outer, static_cast<float>(resort_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
        return false;
    }
    return logical_x >= outer.x && logical_x < outer.x + outer.w && logical_y >= outer.y && logical_y < outer.y + outer.h;
}

std::optional<transfer_system::PokemonMoveController::SlotRef> TransferSystemScreen::multiPokemonAnchorSlotAtPointer(
    int logical_x,
    int logical_y) const {
    if (const auto ref = slotRefAtPointer(logical_x, logical_y)) {
        return ref;
    }
    return std::nullopt;
}

std::optional<transfer_system::PokemonMoveController::SlotRef> TransferSystemScreen::heldMultiPokemonAnchorSlot() const {
    if (!multi_pokemon_move_.active()) {
        return std::nullopt;
    }
    if (multi_pokemon_move_.inputMode() == transfer_system::MultiPokemonMoveController::InputMode::Pointer) {
        return multiPokemonAnchorSlotAtPointer(multi_pokemon_move_.pointer().x, multi_pokemon_move_.pointer().y);
    }
    return slotRefForFocus(focus_.current());
}

void TransferSystemScreen::refreshHeldMoveSpriteTexture() {
    if (!pokemon_move_.active() || !sprite_assets_ || !renderer_) {
        held_move_sprite_tex_ = {};
        return;
    }
    if (const auto* h = pokemon_move_.held()) {
        held_move_sprite_tex_ = sprite_assets_->loadPokemonTexture(renderer_, h->pokemon);
    }
}

} // namespace pr

