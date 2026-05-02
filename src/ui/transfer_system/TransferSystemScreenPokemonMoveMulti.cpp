#include "ui/TransferSystemScreen.hpp"

#include <SDL.h>

#include <algorithm>
#include <iostream>
#include <vector>

namespace pr {

bool TransferSystemScreen::beginMultiPokemonMoveFromSlots(
    const std::vector<transfer_system::PokemonMoveController::SlotRef>& refs,
    transfer_system::MultiPokemonMoveController::InputMode input_mode,
    SDL_Point pointer) {
    using Move = transfer_system::PokemonMoveController;
    using Multi = transfer_system::MultiPokemonMoveController;
    if (refs.empty() || pokemon_move_.active() || multi_pokemon_move_.active() || held_move_.heldItem() || held_move_.heldBox()) {
        return false;
    }

    std::vector<Move::SlotRef> unique_refs;
    unique_refs.reserve(refs.size());
    for (const Move::SlotRef& ref : refs) {
        if (std::find(unique_refs.begin(), unique_refs.end(), ref) == unique_refs.end()) {
            unique_refs.push_back(ref);
        }
    }
    if (unique_refs.empty()) {
        return false;
    }

    int min_row = 99;
    int min_col = 99;
    constexpr int kCols = 6;
    for (const Move::SlotRef& ref : unique_refs) {
        const PcSlotSpecies* slot = pokemonAt(ref);
        if (!slot || !slot->occupied()) {
            return false;
        }
        min_row = std::min(min_row, ref.slot_index / kCols);
        min_col = std::min(min_col, ref.slot_index % kCols);
    }

    std::sort(unique_refs.begin(), unique_refs.end(), [](const Move::SlotRef& a, const Move::SlotRef& b) {
        if (a.panel != b.panel) return static_cast<int>(a.panel) < static_cast<int>(b.panel);
        if (a.box_index != b.box_index) return a.box_index < b.box_index;
        return a.slot_index < b.slot_index;
    });

    std::vector<Multi::Entry> entries;
    entries.reserve(unique_refs.size());
    for (const Move::SlotRef& ref : unique_refs) {
        const PcSlotSpecies* slot = pokemonAt(ref);
        if (!slot || !slot->occupied()) {
            return false;
        }
        Multi::Entry entry;
        entry.pokemon = *slot;
        entry.return_slot = ref;
        entry.row_offset = (ref.slot_index / kCols) - min_row;
        entry.col_offset = (ref.slot_index % kCols) - min_col;
        entries.push_back(std::move(entry));
    }

    for (const Move::SlotRef& ref : unique_refs) {
        clearPokemonAt(ref);
    }
    multi_pokemon_move_.pickUp(std::move(entries), input_mode, pointer);
    refreshResortBoxViewportModel();
    refreshGameBoxViewportModel();
    requestPickupSfx();
    return true;
}

bool TransferSystemScreen::dropHeldMultiPokemonAt(const transfer_system::PokemonMoveController::SlotRef& target) {
    if (!multi_pokemon_move_.active()) {
        return false;
    }
    const auto slots = multi_pokemon_move_.targetSlotsFor(target);
    if (!slots || slots->size() != multi_pokemon_move_.entries().size()) {
        return false;
    }

    for (const auto& ref : *slots) {
        const PcSlotSpecies* dst = pokemonAt(ref);
        if (!dst || dst->occupied()) {
            return false;
        }
    }

    const auto entries = multi_pokemon_move_.entries();
    using Move = transfer_system::PokemonMoveController;
    const Move::Panel from_panel = entries.empty() ? Move::Panel::Game : entries.front().return_slot.panel;
    const Move::Panel to_panel = target.panel;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        setPokemonAt((*slots)[i], entries[i].pokemon);
    }
    if (from_panel == Move::Panel::Game && to_panel == Move::Panel::Resort) {
        noteCrossPanelGameToResortMoves(static_cast<int>(entries.size()));
    } else if (from_panel == Move::Panel::Resort && to_panel == Move::Panel::Game) {
        noteCrossPanelResortToGameMoves(static_cast<int>(entries.size()));
    }
    std::cerr << "[TEMP_TRANSFER_LOG_DELETE] UI multi Pokemon drop count=" << entries.size()
              << " source_panel="
              << (entries.empty() || entries.front().return_slot.panel == transfer_system::PokemonMoveController::Panel::Game
                      ? "game"
                      : "resort")
              << " target_panel="
              << (target.panel == transfer_system::PokemonMoveController::Panel::Game ? "game" : "resort")
              << " target_box=" << target.box_index
              << " target_slot=" << target.slot_index
              << " commit pending Save+Exit\n";
    multi_pokemon_move_.clear();
    refreshResortBoxViewportModel();
    refreshGameBoxViewportModel();
    requestPutdownSfx();

    if (!game_box_browser_.gameBoxSpaceMode()) {
        const auto& first = slots->front();
        focus_.setCurrent((first.panel == transfer_system::PokemonMoveController::Panel::Game ? 2000 : 1000) + first.slot_index);
        selection_cursor_hidden_after_mouse_ = false;
    }
    return true;
}

bool TransferSystemScreen::cancelHeldMultiPokemonMove() {
    if (!multi_pokemon_move_.active()) {
        return false;
    }
    for (const auto& entry : multi_pokemon_move_.entries()) {
        const PcSlotSpecies* dst = pokemonAt(entry.return_slot);
        if (!dst || dst->occupied()) {
            return false;
        }
    }
    const auto entries = multi_pokemon_move_.entries();
    for (const auto& entry : entries) {
        setPokemonAt(entry.return_slot, entry.pokemon);
    }
    multi_pokemon_move_.clear();
    refreshResortBoxViewportModel();
    refreshGameBoxViewportModel();
    requestPutdownSfx();
    return true;
}

bool TransferSystemScreen::gameBoxHasEmptySlots(int box_index, int required_count) const {
    if (required_count <= 0) {
        return true;
    }
    if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
        return false;
    }
    const auto& slots = game_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    int empty_count = 0;
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        if (gameSaveSlotAccessible(i) &&
            !slots[static_cast<std::size_t>(i)].occupied() &&
            ++empty_count >= required_count) {
            return true;
        }
    }
    return false;
}

bool TransferSystemScreen::resortBoxHasEmptySlots(int box_index, int required_count) const {
    if (required_count <= 0) {
        return true;
    }
    if (box_index < 0 || box_index >= static_cast<int>(resort_pc_boxes_.size())) {
        return false;
    }
    const auto& slots = resort_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    int empty_count = 0;
    for (const PcSlotSpecies& slot : slots) {
        if (!slot.occupied() && ++empty_count >= required_count) {
            return true;
        }
    }
    return false;
}

bool TransferSystemScreen::dropHeldMultiPokemonIntoFirstEmptyResortBox(int box_index) {
    if (!multi_pokemon_move_.active() || !resortBoxHasEmptySlots(box_index, multi_pokemon_move_.count())) {
        return false;
    }
    std::vector<transfer_system::PokemonMoveController::SlotRef> targets;
    targets.reserve(static_cast<std::size_t>(multi_pokemon_move_.count()));
    const auto& slots = resort_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    for (int i = 0; i < static_cast<int>(slots.size()) && static_cast<int>(targets.size()) < multi_pokemon_move_.count(); ++i) {
        if (!slots[static_cast<std::size_t>(i)].occupied()) {
            targets.push_back(transfer_system::PokemonMoveController::SlotRef{
                transfer_system::PokemonMoveController::Panel::Resort,
                box_index,
                i});
        }
    }
    if (static_cast<int>(targets.size()) != multi_pokemon_move_.count()) {
        return false;
    }
    const auto entries = multi_pokemon_move_.entries();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        setPokemonAt(targets[i], entries[i].pokemon);
    }
    if (!entries.empty() && entries.front().return_slot.panel == transfer_system::PokemonMoveController::Panel::Game) {
        noteCrossPanelGameToResortMoves(static_cast<int>(entries.size()));
    }
    std::cerr << "[TEMP_TRANSFER_LOG_DELETE] UI multi Pokemon quick-drop to Resort count=" << entries.size()
              << " target_box=" << box_index
              << " commit pending Save+Exit\n";
    multi_pokemon_move_.clear();
    refreshGameBoxViewportModel();
    refreshResortBoxViewportModel();
    requestPutdownSfx();
    return true;
}

bool TransferSystemScreen::dropHeldMultiPokemonIntoFirstEmptySlotsInBox(int box_index) {
    if (!multi_pokemon_move_.active() || !gameBoxHasEmptySlots(box_index, multi_pokemon_move_.count())) {
        return false;
    }
    std::vector<transfer_system::PokemonMoveController::SlotRef> targets;
    targets.reserve(static_cast<std::size_t>(multi_pokemon_move_.count()));
    const auto& slots = game_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    for (int i = 0; i < static_cast<int>(slots.size()) && static_cast<int>(targets.size()) < multi_pokemon_move_.count(); ++i) {
        if (gameSaveSlotAccessible(i) && !slots[static_cast<std::size_t>(i)].occupied()) {
            targets.push_back(transfer_system::PokemonMoveController::SlotRef{
                transfer_system::PokemonMoveController::Panel::Game,
                box_index,
                i});
        }
    }
    if (static_cast<int>(targets.size()) != multi_pokemon_move_.count()) {
        return false;
    }
    const auto entries = multi_pokemon_move_.entries();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        setPokemonAt(targets[i], entries[i].pokemon);
    }
    if (!entries.empty() && entries.front().return_slot.panel == transfer_system::PokemonMoveController::Panel::Resort) {
        noteCrossPanelResortToGameMoves(static_cast<int>(entries.size()));
    }
    std::cerr << "[TEMP_TRANSFER_LOG_DELETE] UI multi Pokemon quick-drop to Game count=" << entries.size()
              << " target_box=" << box_index
              << " commit pending Save+Exit\n";
    multi_pokemon_move_.clear();
    refreshGameBoxViewportModel();
    refreshResortBoxViewportModel();
    requestPutdownSfx();
    return true;
}

} // namespace pr

