#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pr::transfer::contracts {

struct TransferMonRef {
    std::string pkrid;
    std::string openhome_id;
};

struct TransferSource {
    std::string save_path;
    int box = 0;
    int slot = 0;
};

struct TransferDestination {
    std::string save_path;
    int box = 0;
    int slot = 0;
};

struct TransferOperationResult {
    bool success = false;
    std::string error;
    TransferMonRef mon;
};

struct PartyMonSnapshot {
    std::string pkrid;
    std::string species;
    std::int32_t level = 0;
    bool shiny = false;
};

struct PartySnapshot {
    std::vector<PartyMonSnapshot> mons;
};

class TransferService {
public:
    virtual ~TransferService() = default;

    virtual std::vector<TransferMonRef> listAvailablePokemon() const = 0;
    virtual TransferOperationResult importFromSave(const TransferSource& source) = 0;
    virtual TransferOperationResult exportToSave(const TransferMonRef& mon, const TransferDestination& destination) = 0;
    virtual TransferOperationResult commitTransferChanges() = 0;
};

class PartySnapshotProvider {
public:
    virtual ~PartySnapshotProvider() = default;

    virtual PartySnapshot getCurrentPartySnapshot() const = 0;
};

} // namespace pr::transfer::contracts
