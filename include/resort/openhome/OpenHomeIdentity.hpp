#pragma once

#include <cctype>
#include <string>

namespace pr::resort::openhome {

using OpenHomeId = std::string;

inline bool isValidOpenHomeId(const OpenHomeId& id) {
    if (id.size() != 25 || id[4] != '-' || id[13] != '-' || id[22] != '-') {
        return false;
    }
    for (std::size_t i = 0; i < id.size(); ++i) {
        if (i == 4 || i == 13 || i == 22) {
            continue;
        }
        if (!std::isxdigit(static_cast<unsigned char>(id[i]))) {
            return false;
        }
    }
    return true;
}

} // namespace pr::resort::openhome
