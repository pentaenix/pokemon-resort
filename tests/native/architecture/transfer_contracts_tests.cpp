#include "transfer/application/NullTransferService.hpp"

#include <cassert>

int main() {
    pr::transfer::application::NullTransferService service;

    const auto listed = service.listAvailablePokemon();
    assert(listed.empty());

    const auto commit = service.commitTransferChanges();
    assert(commit.success);

    const auto snapshot = service.getCurrentPartySnapshot();
    assert(snapshot.mons.empty());

    return 0;
}
