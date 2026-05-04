#include "ui/transfer_flow/TransferSelectionBuilder.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

pr::PcSlotSpecies makeOccupiedSlot(const std::string& species_slug) {
    pr::PcSlotSpecies slot;
    slot.present = true;
    slot.slug = species_slug;
    slot.species_name = species_slug;
    slot.species_id = 1;
    return slot;
}

pr::SaveFileRecord makeRecord() {
    pr::SaveFileRecord record;
    record.path = "/tmp/diamond.sav";
    record.filename = "diamond.sav";
    record.transfer_summary = pr::TransferSaveSummary{};
    record.transfer_summary->game_id = "pokemon_diamond";
    record.transfer_summary->player_name = "Carlyle";
    record.transfer_summary->play_time = "1:01";
    record.transfer_summary->pokedex_count = 5;
    record.transfer_summary->pokedex_seen_count = 7;
    record.transfer_summary->pokedex_caught_count = 5;
    record.transfer_summary->badges = 0;
    record.transfer_summary->party_slots = {makeOccupiedSlot("piplup")};
    record.transfer_summary->box_1_slots = {makeOccupiedSlot("bulbasaur")};
    pr::TransferSaveSummary::PcBox box;
    box.name = "BOX 1";
    box.slots = {makeOccupiedSlot("bulbasaur")};
    record.transfer_summary->pc_boxes.push_back(box);
    return record;
}

void testSelectionFromRecordMapsFields() {
    const pr::SaveFileRecord record = makeRecord();
    const pr::TransferSaveSelection selection = pr::transfer_flow::selectionFromRecord(record);

    expect(selection.source_path == record.path, "selection should keep source path");
    expect(selection.source_filename == record.filename, "selection should keep source filename");
    expect(selection.game_key == "pokemon_diamond", "selection should keep game key");
    expect(selection.game_title == "Pokemon Diamond", "selection should map game id to title");
    expect(selection.trainer_name == "Carlyle", "selection should keep trainer name");
    expect(selection.time == "1:01", "selection should keep play time");
    expect(selection.pokedex == "5", "selection should stringify pokedex count");
    expect(selection.pokedex_seen == "7", "selection should stringify pokedex seen count");
    expect(selection.pokedex_caught == "5", "selection should stringify pokedex caught count");
    expect(selection.badges == "0", "selection should stringify badge count");
    expect(selection.party_slots.size() == 1, "selection should keep party slots");
    expect(selection.box1_slots.size() == 1, "selection should keep box 1 slots");
    expect(selection.pc_boxes.size() == 1, "selection should keep pc boxes");
    expect(selection.pc_boxes[0].name == "BOX 1", "selection should keep box names");
}

void testGameTitleFromIdHandlesExplicitAliasGames() {
    expect(pr::transfer_flow::gameTitleFromId("pokemon_blue") == "Pokemon Blue",
           "builder should use explicit title for Pokemon Blue");
    expect(pr::transfer_flow::gameTitleFromId("pokemon_hgss") == "Pokemon Heart Gold / Soul Silver",
           "builder should use a readable title for ambiguous HGSS ids");
    expect(pr::transfer_flow::gameTitleFromId("pokemon_heartgold") == "Pokemon Heart Gold",
           "builder should use explicit title for Pokemon Heart Gold");
    expect(pr::transfer_flow::gameTitleFromId("pokemon_sword") == "Pokemon Sword",
           "builder should use explicit title for Pokemon Sword");
    expect(pr::transfer_flow::gameTitleFromId("pokemon_violet") == "Pokemon Violet",
           "builder should use explicit title for Pokemon Violet");
    expect(pr::transfer_flow::gameTitleFromId("pokemon_um") == "Pokemon Ultra Moon",
           "builder should normalize compact Ultra Moon ids before showing ticket titles");
    expect(pr::transfer_flow::gameTitleFromId("pokemon_us") == "Pokemon Ultra Sun",
           "builder should normalize compact Ultra Sun ids before showing ticket titles");
    expect(pr::transfer_flow::gameTitleFromId("pokemon_white_2") == "Pokemon White 2",
           "builder should keep numbered sequel titles readable");
}

void testSelectionsFromRecordsSkipsNonTransferRecords() {
    pr::SaveFileRecord with_summary = makeRecord();
    pr::SaveFileRecord without_summary;
    without_summary.path = "/tmp/empty.sav";
    without_summary.filename = "empty.sav";

    const auto selections =
        pr::transfer_flow::selectionsFromRecords({without_summary, with_summary});

    expect(selections.size() == 1, "builder should skip records without transfer summary");
    expect(selections[0].source_filename == "diamond.sav", "builder should preserve valid transfer record");
}

void testMergeFreshSummaryReplacesTransferFacingFields() {
    const pr::SaveFileRecord record = makeRecord();
    const pr::TransferSaveSelection base = pr::transfer_flow::selectionFromRecord(record);

    pr::TransferSaveSummary fresh;
    fresh.game_id = "pokemon_heartgold";
    fresh.play_time = "9:59";
    fresh.pokedex_count = 42;
    fresh.pokedex_seen_count = 60;
    fresh.pokedex_caught_count = 42;
    fresh.badges = 8;
    fresh.party_slots = {makeOccupiedSlot("empoleon")};
    fresh.box_1_slots = {makeOccupiedSlot("charizard")};
    pr::TransferSaveSummary::PcBox box;
    box.name = "BOX X";
    box.slots = {makeOccupiedSlot("blastoise")};
    fresh.pc_boxes.push_back(box);

    const pr::TransferSaveSelection merged = pr::transfer_flow::mergeFreshSummary(base, fresh);

    expect(merged.source_filename == base.source_filename, "merge should preserve source identity");
    expect(merged.game_key == "pokemon_heartgold", "merge should refresh game key from fresh summary");
    expect(merged.game_title == "Pokemon Heart Gold", "merge should refresh game title from fresh summary");
    expect(merged.time == "9:59", "merge should refresh play time");
    expect(merged.pokedex == "42", "merge should refresh pokedex count");
    expect(merged.pokedex_seen == "60", "merge should refresh pokedex seen count");
    expect(merged.pokedex_caught == "42", "merge should refresh pokedex caught count");
    expect(merged.badges == "8", "merge should refresh badge count");
    expect(merged.party_slots[0].slug == "empoleon", "merge should replace party slots");
    expect(merged.box1_slots[0].slug == "charizard", "merge should replace box 1 slots");
    expect(merged.pc_boxes.size() == 1 && merged.pc_boxes[0].name == "BOX X",
           "merge should replace pc box payload");
}

} // namespace

int main() {
    testSelectionFromRecordMapsFields();
    testGameTitleFromIdHandlesExplicitAliasGames();
    testSelectionsFromRecordsSkipsNonTransferRecords();
    testMergeFreshSummaryReplacesTransferFacingFields();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
