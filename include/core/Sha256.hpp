#pragma once

#include <string>
#include <vector>

namespace pr {

/// Lowercase hex SHA-256 of `data` (matches PKHeX bridge `raw_hash_sha256` style).
std::string sha256HexLowercase(const std::vector<unsigned char>& data);

} // namespace pr
