#pragma once

#include "core/domain/PcSlotSpecies.hpp"

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>

namespace pr {

/// Stable key for matching external-save `PcSlotSpecies` rows to Resort `pokemon` rows that carry OpenHome links.
/// Must stay in sync with `OpenHomeLinkRepository::loadPidEcOtOpenHomeProfileMatchKeys`.
inline std::optional<std::uint16_t> pcSlotOptionalTid16(const PcSlotSpecies& s) {
    if (s.tid16 < 0 || s.tid16 > 65535) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(s.tid16);
}

inline std::optional<std::uint16_t> pcSlotOptionalSid16(const PcSlotSpecies& s) {
    if (s.sid16 < 0 || s.sid16 > 65535) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(s.sid16);
}

inline std::string openHomeProfileMatchKey(
    std::uint32_t pid,
    std::uint32_t encryption_constant,
    std::optional<std::uint16_t> tid16,
    std::optional<std::uint16_t> sid16,
    const std::string& ot_name) {
    std::ostringstream oss;
    oss << "pid_ec:" << std::hex << pid << ':' << encryption_constant << std::dec;
    oss << '|' << (tid16 ? static_cast<int>(*tid16) : -1);
    oss << '|' << (sid16 ? static_cast<int>(*sid16) : -1);
    oss << "|ot:" << ot_name;
    return oss.str();
}

inline std::string openHomeProfileMatchKeyFromSlot(const PcSlotSpecies& s) {
    if (!s.pid.has_value() || !s.encryption_constant.has_value()) {
        return {};
    }
    return openHomeProfileMatchKey(
        *s.pid, *s.encryption_constant, pcSlotOptionalTid16(s), pcSlotOptionalSid16(s), s.ot_name);
}

} // namespace pr
