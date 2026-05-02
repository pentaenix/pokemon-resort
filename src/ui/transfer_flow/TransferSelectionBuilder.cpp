#include "ui/transfer_flow/TransferSelectionBuilder.hpp"

#include "core/domain/PokemonOrigin.hpp"

#include <utility>

namespace pr::transfer_flow {

std::string gameTitleFromId(const std::string& game_id) {
    return pokemonGameTitle(game_id);
}

TransferSaveSelection selectionFromRecord(const SaveFileRecord& record) {
    const TransferSaveSummary& summary = *record.transfer_summary;
    TransferSaveSelection selection;
    selection.source_path = record.path;
    selection.source_filename = record.filename;
    selection.game_key = summary.game_id;
    selection.game_title = pokemonGameTitle(summary.game_id, record.filename);
    selection.trainer_name = summary.player_name;
    selection.time = summary.play_time;
    selection.pokedex = std::to_string(summary.pokedex_count);
    selection.pokedex_seen = std::to_string(summary.pokedex_seen_count);
    selection.pokedex_caught = std::to_string(summary.pokedex_caught_count);
    selection.badges = std::to_string(summary.badges);
    selection.party_slots = summary.party_slots;
    selection.box1_slots = summary.box_1_slots;
    selection.pc_boxes.reserve(summary.pc_boxes.size());
    for (const auto& box : summary.pc_boxes) {
        TransferSaveSelection::PcBox out_box;
        out_box.name = box.name;
        out_box.native_slot_count = box.native_slot_count;
        out_box.slots = box.slots;
        selection.pc_boxes.push_back(std::move(out_box));
    }
    return selection;
}

std::vector<TransferSaveSelection> selectionsFromRecords(const std::vector<SaveFileRecord>& records) {
    std::vector<TransferSaveSelection> selections;
    selections.reserve(records.size());
    for (const SaveFileRecord& record : records) {
        if (!record.transfer_summary) {
            continue;
        }
        selections.push_back(selectionFromRecord(record));
    }
    return selections;
}

TransferSaveSelection mergeFreshSummary(
    const TransferSaveSelection& base,
    const TransferSaveSummary& fresh_summary) {
    TransferSaveSelection merged = base;
    if (!fresh_summary.game_id.empty()) {
        merged.game_key = fresh_summary.game_id;
        merged.game_title = pokemonGameTitle(fresh_summary.game_id, merged.source_filename);
    }
    merged.box1_slots = fresh_summary.box_1_slots;
    merged.party_slots = fresh_summary.party_slots;
    merged.time = fresh_summary.play_time;
    merged.pokedex = std::to_string(fresh_summary.pokedex_count);
    merged.pokedex_seen = std::to_string(fresh_summary.pokedex_seen_count);
    merged.pokedex_caught = std::to_string(fresh_summary.pokedex_caught_count);
    merged.badges = std::to_string(fresh_summary.badges);
    merged.pc_boxes.clear();
    merged.pc_boxes.reserve(fresh_summary.pc_boxes.size());
    for (const auto& box : fresh_summary.pc_boxes) {
        TransferSaveSelection::PcBox out_box;
        out_box.name = box.name;
        out_box.native_slot_count = box.native_slot_count;
        out_box.slots = box.slots;
        merged.pc_boxes.push_back(std::move(out_box));
    }
    return merged;
}

} // namespace pr::transfer_flow
