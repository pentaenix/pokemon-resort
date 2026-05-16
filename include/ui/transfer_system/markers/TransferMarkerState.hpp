#pragma once

#include "ui/transfer_system/PokemonMoveController.hpp"
#include "ui/transfer_system/markers/TransferSlotMarkerTypes.hpp"

#include <array>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pr {
struct PcSlotSpecies;
}

namespace pr::resort {
class PokemonResortService;
}

namespace pr::transfer_system {

/// Session staging + Tier-1 OpenHome hint LRU for transfer slot markers.
class TransferMarkerState {
public:
    static constexpr int kTier1LruCap = 16;

    void onSaveCommittedOrDiscard();

    void invalidateAllGameGreenCaches();
    void invalidateGameBoxGreenCache(int box_index);

    void ensureTier1OpenHomeHintsForGameBox(
        int box_index,
        const std::vector<pr::PcSlotSpecies>& slots30,
        resort::PokemonResortService* service);

    bool tier1OpenHomeHint(int box_index, int slot_index) const;

    pr::TransferMarkerSessionView sessionViewForSlot(pr::TransferMarkerPanel panel, const std::string& identity_key) const;
    pr::TransferMarkerSessionView sessionViewForSpecies(pr::TransferMarkerPanel panel, const pr::PcSlotSpecies& slot) const;

    void afterSuccessfulCrossPanelPokemonDrop(
        const pr::PcSlotSpecies& moved_mon_post,
        const PokemonMoveController::SlotRef& from,
        const PokemonMoveController::SlotRef& to,
        resort::PokemonResortService* service);

    void ensureProfileOpenHomeMatchKeysLoaded(resort::PokemonResortService* service) const;
    void markProfileOpenHomeMatchKeysStale();
    bool profileOpenHomeMatchForSlot(const pr::PcSlotSpecies& slot) const;

private:
    struct Tier1CacheEntry {
        int box_index = -1;
        std::array<std::uint8_t, 30> hints{}; // 0 unknown, 1 false, 2 true
    };

    void touchTier1Lru(int box_index, Tier1CacheEntry entry);
    void evictTier1IfNeeded();

    std::list<Tier1CacheEntry> tier1_lru_{};
    std::unordered_map<int, std::list<Tier1CacheEntry>::iterator> tier1_by_box_{};

    mutable std::unordered_set<std::string> profile_openhome_match_keys_{};
    mutable bool profile_match_keys_stale_ = true;

    std::unordered_set<std::string> yellow_on_game_{};
    std::unordered_set<std::string> blue_on_resort_{};
    std::unordered_set<std::string> green_carry_on_resort_{};
};

} // namespace pr::transfer_system
