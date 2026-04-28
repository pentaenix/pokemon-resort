#pragma once

#include <string>

namespace pr::transfer_system::detail {

bool utf8_pop_back_last(std::string& s);

std::string trimAsciiWhitespaceCopy(std::string s);

std::string asciiLowerCopy(std::string s);

std::string replaceCharsCopy(std::string s, const std::string& from_any, char to);

std::string removeCharsCopy(std::string s, const std::string& remove_any);

} // namespace pr::transfer_system::detail

