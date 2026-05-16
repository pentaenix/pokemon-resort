#include "ui/transfer_system/markers/TransferMarkerState.hpp"

#include "core/domain/PcSlotOpenHomeProfileKey.hpp"
#include "core/domain/PcSlotSpecies.hpp"
#include "resort/openhome/OpenHomeIdentity.hpp"
#include "resort/services/PokemonResortService.hpp"
#include "ui/transfer_system/markers/TransferMarkerResolve.hpp"

namespace pr::transfer_system {

namespace {

bool slotNeedsTier1Hint(const pr::PcSlotSpecies& slot) {
    return slot.occupied() && slot.home_tracker.empty() && !slot.resort_pkrid.empty();
}

} // namespace

void TransferMarkerState::onSaveCommittedOrDiscard() {
    tier1_lru_.clear();
    tier1_by_box_.clear();
    yellow_on_game_.clear();
    blue_on_resort_.clear();
    green_carry_on_resort_.clear();
    profile_openhome_match_keys_.clear();
    profile_match_keys_stale_ = true;
}

void TransferMarkerState::invalidateAllGameGreenCaches() {
    tier1_lru_.clear();
    tier1_by_box_.clear();
}

void TransferMarkerState::invalidateGameBoxGreenCache(int box_index) {
    const auto it = tier1_by_box_.find(box_index);
    if (it == tier1_by_box_.end()) {
        return;
    }
    tier1_lru_.erase(it->second);
    tier1_by_box_.erase(it);
}

void TransferMarkerState::ensureProfileOpenHomeMatchKeysLoaded(resort::PokemonResortService* service) const {
    if (!service) {
        return;
    }
    if (!profile_match_keys_stale_) {
        return;
    }
    profile_openhome_match_keys_.clear();
    service->loadOpenHomeProfileMatchKeys(profile_openhome_match_keys_);
    profile_match_keys_stale_ = false;
}

void TransferMarkerState::markProfileOpenHomeMatchKeysStale() {
    profile_match_keys_stale_ = true;
}

bool TransferMarkerState::profileOpenHomeMatchForSlot(const pr::PcSlotSpecies& slot) const {
    const std::string k = pr::openHomeProfileMatchKeyFromSlot(slot);
    return !k.empty() && profile_openhome_match_keys_.count(k) != 0;
}

void TransferMarkerState::evictTier1IfNeeded() {
    while (static_cast<int>(tier1_by_box_.size()) > kTier1LruCap && !tier1_lru_.empty()) {
        const int victim = tier1_lru_.back().box_index;
        tier1_by_box_.erase(victim);
        tier1_lru_.pop_back();
    }
}

void TransferMarkerState::touchTier1Lru(int box_index, Tier1CacheEntry entry) {
    invalidateGameBoxGreenCache(box_index);
    tier1_lru_.push_front(std::move(entry));
    tier1_by_box_[box_index] = tier1_lru_.begin();
    evictTier1IfNeeded();
}

void TransferMarkerState::ensureTier1OpenHomeHintsForGameBox(
    int box_index,
    const std::vector<pr::PcSlotSpecies>& slots30,
    resort::PokemonResortService* service) {
    if (box_index < 0) {
        return;
    }
    if (const auto it = tier1_by_box_.find(box_index); it != tier1_by_box_.end()) {
        tier1_lru_.splice(tier1_lru_.begin(), tier1_lru_, it->second);
        return;
    }

    Tier1CacheEntry entry;
    entry.box_index = box_index;
    entry.hints.fill(0);

    std::vector<std::string> batch_pkrids;
    for (int i = 0; i < 30 && i < static_cast<int>(slots30.size()); ++i) {
        const pr::PcSlotSpecies& slot = slots30[static_cast<std::size_t>(i)];
        if (!slotNeedsTier1Hint(slot)) {
            entry.hints[static_cast<std::size_t>(i)] = 1; // false
            continue;
        }
        batch_pkrids.push_back(slot.resort_pkrid);
    }

    std::unordered_map<std::string, std::string> openhome_by_pkrid;
    if (service && !batch_pkrids.empty()) {
        openhome_by_pkrid = service->getOpenHomeIdsForPokemonBatch(batch_pkrids);
    }

    for (int i = 0; i < 30 && i < static_cast<int>(slots30.size()); ++i) {
        std::uint8_t& hint = entry.hints[static_cast<std::size_t>(i)];
        if (hint != 0) {
            continue;
        }
        const pr::PcSlotSpecies& slot = slots30[static_cast<std::size_t>(i)];
        if (!slotNeedsTier1Hint(slot)) {
            hint = 1;
            continue;
        }
        const auto itp = openhome_by_pkrid.find(slot.resort_pkrid);
        const bool ok = itp != openhome_by_pkrid.end() && resort::openhome::isValidOpenHomeId(itp->second);
        hint = static_cast<std::uint8_t>(ok ? 2 : 1);
    }

    touchTier1Lru(box_index, std::move(entry));
}

bool TransferMarkerState::tier1OpenHomeHint(int box_index, int slot_index) const {
    if (slot_index < 0 || slot_index >= 30) {
        return false;
    }
    const auto it = tier1_by_box_.find(box_index);
    if (it == tier1_by_box_.end()) {
        return false;
    }
    const std::uint8_t h = it->second->hints[static_cast<std::size_t>(slot_index)];
    return h == 2;
}

pr::TransferMarkerSessionView TransferMarkerState::sessionViewForSlot(
    pr::TransferMarkerPanel panel,
    const std::string& identity_key) const {
    pr::TransferMarkerSessionView out{};
    if (identity_key.empty()) {
        return out;
    }
    if (panel == pr::TransferMarkerPanel::GameColumn) {
        out.yellow_on_game = yellow_on_game_.count(identity_key) != 0;
    } else {
        out.blue_on_resort = blue_on_resort_.count(identity_key) != 0;
        out.green_carry_on_resort = green_carry_on_resort_.count(identity_key) != 0;
    }
    return out;
}

pr::TransferMarkerSessionView TransferMarkerState::sessionViewForSpecies(
    pr::TransferMarkerPanel panel,
    const pr::PcSlotSpecies& slot) const {
    pr::TransferMarkerSessionView merged{};
    for (const std::string& k : pr::transferMarkerSessionAliasKeys(slot)) {
        const pr::TransferMarkerSessionView part = sessionViewForSlot(panel, k);
        merged.yellow_on_game = merged.yellow_on_game || part.yellow_on_game;
        merged.blue_on_resort = merged.blue_on_resort || part.blue_on_resort;
        merged.green_carry_on_resort = merged.green_carry_on_resort || part.green_carry_on_resort;
    }
    return merged;
}

void TransferMarkerState::afterSuccessfulCrossPanelPokemonDrop(
    const pr::PcSlotSpecies& moved_mon_post,
    const PokemonMoveController::SlotRef& from,
    const PokemonMoveController::SlotRef& to,
    resort::PokemonResortService* service) {
    if (from.panel == to.panel) {
        return;
    }
    const std::vector<std::string> keys = pr::transferMarkerSessionAliasKeys(moved_mon_post);
    if (keys.empty()) {
        if (from.panel == PokemonMoveController::Panel::Game) {
            invalidateGameBoxGreenCache(from.box_index);
        }
        if (to.panel == PokemonMoveController::Panel::Game) {
            invalidateGameBoxGreenCache(to.box_index);
        }
        return;
    }

    if (from.panel == PokemonMoveController::Panel::Resort && to.panel == PokemonMoveController::Panel::Game) {
        for (const std::string& k : keys) {
            yellow_on_game_.insert(k);
        }
        invalidateGameBoxGreenCache(to.box_index);
        return;
    }
    if (from.panel == PokemonMoveController::Panel::Game && to.panel == PokemonMoveController::Panel::Resort) {
        for (const std::string& k : keys) {
            yellow_on_game_.erase(k);
        }
        ensureProfileOpenHomeMatchKeysLoaded(service);
        bool tier1_hint = false;
        if (moved_mon_post.occupied() && moved_mon_post.home_tracker.empty() && !moved_mon_post.resort_pkrid.empty() &&
            service != nullptr) {
            if (const std::optional<std::string> oh = service->getOpenHomeIdForPokemon(moved_mon_post.resort_pkrid)) {
                tier1_hint = resort::openhome::isValidOpenHomeId(*oh);
            }
        }
        const bool profile_hit = profileOpenHomeMatchForSlot(moved_mon_post);
        const bool baseline = baselineReturnVisitorOnGame(moved_mon_post, tier1_hint, profile_hit);
        if (baseline) {
            for (const std::string& k : keys) {
                green_carry_on_resort_.insert(k);
                blue_on_resort_.erase(k);
            }
        } else {
            for (const std::string& k : keys) {
                blue_on_resort_.insert(k);
                green_carry_on_resort_.erase(k);
            }
        }
        invalidateGameBoxGreenCache(from.box_index);
    }
}

} // namespace pr::transfer_system
