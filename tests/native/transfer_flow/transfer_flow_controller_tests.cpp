#include "ui/transfer_flow/TransferFlowController.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

pr::TransferSaveSelection makeSelection(const std::string& game_key, const std::string& filename) {
    pr::TransferSaveSelection selection;
    selection.game_key = game_key;
    selection.source_filename = filename;
    selection.source_path = "/tmp/" + filename;
    return selection;
}

void testTicketScanFlowMovesIntoTicketList() {
    pr::transfer_flow::TransferFlowController controller;
    controller.beginTicketScan();

    expect(controller.activeScreenKind() == pr::transfer_flow::ScreenKind::Loading,
           "beginTicketScan should enter loading screen");
    expect(controller.loadingPurpose() == pr::transfer_flow::LoadingPurpose::ScanTransferTickets,
           "beginTicketScan should set scan loading purpose");

    controller.finishTicketScan();
    expect(controller.activeScreenKind() == pr::transfer_flow::ScreenKind::TicketList,
           "finishTicketScan should enter ticket list");
    expect(controller.loadingPurpose() == pr::transfer_flow::LoadingPurpose::None,
           "finishTicketScan should clear loading purpose");
}

void testDeepProbeProducesTransferSystemEntry() {
    pr::transfer_flow::TransferFlowController controller;
    controller.beginDeepProbe(makeSelection("pokemon_diamond", "diamond.sav"));

    expect(controller.activeScreenKind() == pr::transfer_flow::ScreenKind::Loading,
           "beginDeepProbe should enter loading screen");
    expect(controller.loadingPurpose() == pr::transfer_flow::LoadingPurpose::DeepProbeSelectedSave,
           "beginDeepProbe should set deep-probe purpose");
    expect(controller.pendingTransferDetailSelection().source_filename == "diamond.sav",
           "beginDeepProbe should store pending selection");

    controller.finishDeepProbe(makeSelection("pokemon_diamond", "diamond.sav"));
    expect(controller.activeScreenKind() == pr::transfer_flow::ScreenKind::TransferSystem,
           "finishDeepProbe should enter transfer system");

    auto request = controller.consumeTransferSystemEntryRequest();
    expect(request.has_value(), "finishDeepProbe should emit transfer-system entry request");
    expect(request->selection.source_filename == "diamond.sav",
           "entry request should carry selected save");
    expect(request->initial_box_index == 0,
           "entry request should default to box index 0 before any remembered state");
    expect(!controller.consumeTransferSystemEntryRequest().has_value(),
           "entry request should be one-shot");
}

void testRememberedBoxIndexIsRestoredPerGame() {
    pr::transfer_flow::TransferFlowController controller;
    controller.returnToTicketListFromTransferSystem("pokemon_diamond", 7);

    controller.beginDeepProbe(makeSelection("pokemon_diamond", "diamond.sav"));
    controller.finishDeepProbe(makeSelection("pokemon_diamond", "diamond.sav"));

    auto request = controller.consumeTransferSystemEntryRequest();
    expect(request.has_value(), "deep probe after return should emit entry request");
    expect(request->initial_box_index == 7, "controller should restore remembered box index per game");
}

void testReturnToTitleClearsSessionState() {
    pr::transfer_flow::TransferFlowController controller;
    controller.returnToTicketListFromTransferSystem("pokemon_diamond", 5);
    controller.returnToTitleFromTicketList();

    expect(controller.activeScreenKind() == pr::transfer_flow::ScreenKind::None,
           "returnToTitleFromTicketList should deactivate the flow");
    expect(controller.consumeReturnToTitleRequest(),
           "returnToTitleFromTicketList should emit one-shot title return request");
    expect(!controller.consumeReturnToTitleRequest(),
           "title return request should be one-shot");

    controller.beginDeepProbe(makeSelection("pokemon_diamond", "diamond.sav"));
    controller.finishDeepProbe(makeSelection("pokemon_diamond", "diamond.sav"));
    auto request = controller.consumeTransferSystemEntryRequest();
    expect(request.has_value(), "controller should still emit entry request after reset");
    expect(request->initial_box_index == 0, "return to title should clear remembered box indices");
}

} // namespace

int main() {
    testTicketScanFlowMovesIntoTicketList();
    testDeepProbeProducesTransferSystemEntry();
    testRememberedBoxIndexIsRestoredPerGame();
    testReturnToTitleClearsSessionState();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
