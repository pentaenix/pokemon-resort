#pragma once

#include <cctype>
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

} // namespace pr::resort
