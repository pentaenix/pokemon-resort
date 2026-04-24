#include "ui/transfer_flow/TransferSelectionBuilder.hpp"

#include <utility>

namespace pr::transfer_flow {

namespace {

std::string asciiLowerCopy(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool containsToken(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

std::string gameTitleFromIdAndFilenameHint(const std::string& game_id, const std::string& filename_hint) {
    if (game_id == "pokemon_red") {
        return "Pokemon Red";
    }
    if (game_id == "pokemon_blue") {
        return "Pokemon Blue";
    }
    if (game_id == "pokemon_yellow") {
        return "Pokemon Yellow";
    }
    if (game_id == "pokemon_hgss") {
        // Prefer a single title when the bridge emits an ambiguous HGSS id.
        // Filename hints are reliable for common dump naming (heartgold/hg, soulsilver/ss).
        const std::string hint = asciiLowerCopy(filename_hint);
        if (containsToken(hint, "heartgold") || containsToken(hint, "heart_gold") || containsToken(hint, "hg")) {
            return "Pokemon Heart Gold";
        }
        if (containsToken(hint, "soulsilver") || containsToken(hint, "soul_silver") || containsToken(hint, "ss")) {
            return "Pokemon Soul Silver";
        }
        return "Pokemon Heart Gold / Soul Silver";
    }
    if (game_id == "pokemon_heartgold") {
        return "Pokemon Heart Gold";
    }
    if (game_id == "pokemon_soulsilver") {
        return "Pokemon Soul Silver";
    }
    if (game_id == "pokemon_firered") {
        return "Pokemon FireRed";
    }
    if (game_id == "pokemon_leafgreen") {
        return "Pokemon LeafGreen";
    }
    if (game_id == "pokemon_sword") {
        return "Pokemon Sword";
    }
    if (game_id == "pokemon_shield") {
        return "Pokemon Shield";
    }
    if (game_id == "pokemon_scarlet") {
        return "Pokemon Scarlet";
    }
    if (game_id == "pokemon_violet") {
        return "Pokemon Violet";
    }

    std::string title = game_id;
    for (char& c : title) {
        if (c == '_') {
            c = ' ';
        }
    }

    bool capitalize_next = true;
    for (char& c : title) {
        if (c == ' ') {
            capitalize_next = true;
            continue;
        }
        if (capitalize_next && c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
        capitalize_next = false;
    }
    return title;
}

} // namespace

std::string gameTitleFromId(const std::string& game_id) {
    // Public mapper stays deterministic; ambiguous ids use a readable combined title.
    if (game_id == "pokemon_hgss") {
        return "Pokemon Heart Gold / Soul Silver";
    }
    return gameTitleFromIdAndFilenameHint(game_id, "");
}

TransferSaveSelection selectionFromRecord(const SaveFileRecord& record) {
    const TransferSaveSummary& summary = *record.transfer_summary;
    TransferSaveSelection selection;
    selection.source_path = record.path;
    selection.source_filename = record.filename;
    selection.game_key = summary.game_id;
    selection.game_title = gameTitleFromIdAndFilenameHint(summary.game_id, record.filename);
    selection.trainer_name = summary.player_name;
    selection.time = summary.play_time;
    selection.pokedex = std::to_string(summary.pokedex_count);
    selection.badges = std::to_string(summary.badges);
    selection.party_slots = summary.party_slots;
    selection.box1_slots = summary.box_1_slots;
    selection.pc_boxes.reserve(summary.pc_boxes.size());
    for (const auto& box : summary.pc_boxes) {
        TransferSaveSelection::PcBox out_box;
        out_box.name = box.name;
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
        merged.game_title = gameTitleFromIdAndFilenameHint(fresh_summary.game_id, merged.source_filename);
    }
    merged.box1_slots = fresh_summary.box_1_slots;
    merged.party_slots = fresh_summary.party_slots;
    merged.time = fresh_summary.play_time;
    merged.pokedex = std::to_string(fresh_summary.pokedex_count);
    merged.badges = std::to_string(fresh_summary.badges);
    merged.pc_boxes.clear();
    merged.pc_boxes.reserve(fresh_summary.pc_boxes.size());
    for (const auto& box : fresh_summary.pc_boxes) {
        TransferSaveSelection::PcBox out_box;
        out_box.name = box.name;
        out_box.slots = box.slots;
        merged.pc_boxes.push_back(std::move(out_box));
    }
    return merged;
}

} // namespace pr::transfer_flow
