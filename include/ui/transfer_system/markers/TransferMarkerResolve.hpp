#pragma once

#include "core/domain/PcSlotOpenHomeProfileKey.hpp"
#include "core/domain/PcSlotSpecies.hpp"
#include "ui/transfer_system/markers/TransferSlotMarkerTypes.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace pr {

inline std::string transferSlotMarkerIdentityKey(const PcSlotSpecies& s) {
    if (!s.resort_pkrid.empty()) {
        return std::string("pkrid:") + s.resort_pkrid;
    }
    if (!s.home_tracker.empty()) {
        return std::string("hot:") + s.home_tracker;
    }
    if (s.pid.has_value() && s.encryption_constant.has_value()) {
        std::ostringstream oss;
        oss << "pid_ec:" << std::hex << *s.pid << ':' << *s.encryption_constant;
        return oss.str();
    }
    return {};
}

inline void transferMarkerAppendUniqueKey(std::vector<std::string>& keys, const std::string& s) {
    if (s.empty()) {
        return;
    }
    for (const auto& existing : keys) {
        if (existing == s) {
            return;
        }
    }
    keys.push_back(s);
}

/// Keys for session sets (yellow / blue / green). Multiple aliases cover cases where identity prefers `pkrid:`
/// while the visible slot later matches only PID+EC+OT (profile key) or vice versa.
inline std::vector<std::string> transferMarkerSessionAliasKeys(const PcSlotSpecies& s) {
    std::vector<std::string> keys;
    transferMarkerAppendUniqueKey(keys, transferSlotMarkerIdentityKey(s));
    transferMarkerAppendUniqueKey(keys, openHomeProfileMatchKeyFromSlot(s));
    return keys;
}

inline bool tier0ReturnVisitorCart(const PcSlotSpecies& slot) {
    return slot.occupied() && !slot.home_tracker.empty();
}

inline bool baselineReturnVisitorOnGame(
    const PcSlotSpecies& slot,
    bool tier1_open_home_hint,
    bool profile_pid_ec_openhome_match) {
    if (tier0ReturnVisitorCart(slot)) {
        return true;
    }
    if (slot.occupied() && !slot.resort_pkrid.empty() && tier1_open_home_hint) {
        return true;
    }
    return slot.occupied() && profile_pid_ec_openhome_match;
}

inline TransferSlotMarkerKind resolveTransferSlotMarker(
    TransferMarkerPanel panel,
    const PcSlotSpecies& slot,
    const TransferMarkerSessionView& session,
    bool tier1_open_home_hint,
    bool profile_pid_ec_openhome_match) {
    if (!slot.occupied()) {
        return TransferSlotMarkerKind::None;
    }
    if (panel == TransferMarkerPanel::GameColumn) {
        if (session.yellow_on_game) {
            return TransferSlotMarkerKind::StagingFromResort;
        }
        if (baselineReturnVisitorOnGame(slot, tier1_open_home_hint, profile_pid_ec_openhome_match)) {
            return TransferSlotMarkerKind::ReturnVisitor;
        }
        return TransferSlotMarkerKind::None;
    }
    if (session.green_carry_on_resort) {
        return TransferSlotMarkerKind::ReturnVisitorOnResortUntilSave;
    }
    if (session.blue_on_resort) {
        return TransferSlotMarkerKind::FirstVisitStaging;
    }
    return TransferSlotMarkerKind::None;
}

} // namespace pr
