#include "transfer/application/NullTransferService.hpp"

namespace pr::transfer::application {

std::vector<contracts::TransferMonRef> NullTransferService::listAvailablePokemon() const {
    return {};
}

contracts::TransferOperationResult NullTransferService::importFromSave(const contracts::TransferSource&) {
    return {.success = false, .error = "NullTransferService: importFromSave not wired"};
}

contracts::TransferOperationResult NullTransferService::exportToSave(
    const contracts::TransferMonRef& mon,
    const contracts::TransferDestination&) {
    return {.success = false, .error = "NullTransferService: exportToSave not wired", .mon = mon};
}

contracts::TransferOperationResult NullTransferService::commitTransferChanges() {
    return {.success = true};
}

contracts::PartySnapshot NullTransferService::getCurrentPartySnapshot() const {
    return {};
}

} // namespace pr::transfer::application
