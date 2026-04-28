#include "ui/transfer_system/detail/StringUtil.hpp"

#include <cctype>

namespace pr::transfer_system::detail {

bool utf8_pop_back_last(std::string& s) {
    if (s.empty()) {
        return false;
    }
    std::size_t i = s.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) {
        --i;
    }
    s.erase(i);
    return true;
}

std::string trimAsciiWhitespaceCopy(std::string s) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && is_space(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && is_space(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}

std::string asciiLowerCopy(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

std::string replaceCharsCopy(std::string s, const std::string& from_any, char to) {
    for (char& ch : s) {
        if (from_any.find(ch) != std::string::npos) {
            ch = to;
        }
    }
    return s;
}

std::string removeCharsCopy(std::string s, const std::string& remove_any) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
        if (remove_any.find(ch) == std::string::npos) {
            out.push_back(ch);
        }
    }
    return out;
}

} // namespace pr::transfer_system::detail

