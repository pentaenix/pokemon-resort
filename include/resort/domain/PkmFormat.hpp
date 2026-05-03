#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace pr::resort {

/// Case-insensitive comparison of PKHeX-style format labels (`pk3`, `PK4`, `PB8`, ...).
inline bool pkmFormatNamesEqual(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

inline std::string pkmStorageFormatNameForGameId(std::uint16_t game_id) {
    switch (game_id) {
        case 1:  // Ruby/Sapphire
        case 2:
        case 3:  // Emerald
        case 4:  // FireRed
        case 5:  // LeafGreen
            return "pk3";
        case 7:  // HeartGold
        case 8:  // SoulSilver
        case 10: // Diamond
        case 11: // Pearl
        case 12: // Platinum
            return "pk4";
        case 20: // White
        case 21: // Black
        case 22: // White 2
        case 23: // Black 2
            return "pk5";
        case 24: // X
        case 25: // Y
        case 26: // Alpha Sapphire
        case 27: // Omega Ruby
            return "pk6";
        case 30: // Sun
        case 31: // Moon
        case 32: // Ultra Sun
        case 33: // Ultra Moon
            return "pk7";
        case 44: // Sword
        case 45: // Shield
            return "pk8";
        case 47: // Legends: Arceus
            return "pa8";
        case 48: // Brilliant Diamond
        case 49: // Shining Pearl
            return "pb8";
        case 50: // Scarlet
        case 51: // Violet
            return "pk9";
        default:
            return {};
    }
}

/// PKM storage-format generation used for PID / personality constraints (not a specific game title).
/// Returns 0 when unknown (e.g. empty or non-standard format labels).
inline int constraintGenerationFromStorageFormat(std::string_view fmt) {
    if (fmt.size() < 3) {
        return 0;
    }
    const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(fmt[0])));
    const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(fmt[1])));
    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(fmt[2])));
    if (a == 'p' && b == 'k' && c >= '1' && c <= '9') {
        return c - '0';
    }
    if (a == 'p' && b == 'b' && c == '8') {
        return 8;
    }
    if (a == 'p' && b == 'a' && c == '8') {
        return 8;
    }
    return 0;
}

} // namespace pr::resort
