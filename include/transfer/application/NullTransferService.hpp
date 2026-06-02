#pragma once

#include "transfer/contracts/TransferContracts.hpp"

namespace pr::transfer::application {

class NullTransferService final : public contracts::TransferService, public contracts::PartySnapshotProvider {
public:
    std::vector<contracts::TransferMonRef> listAvailablePokemon() const override;
    contracts::TransferOperationResult importFromSave(const contracts::TransferSource& source) override;
    contracts::TransferOperationResult exportToSave(
        const contracts::TransferMonRef& mon,
        const contracts::TransferDestination& destination) override;
    contracts::TransferOperationResult commitTransferChanges() override;
    contracts::PartySnapshot getCurrentPartySnapshot() const override;
};

} // namespace pr::transfer::application
