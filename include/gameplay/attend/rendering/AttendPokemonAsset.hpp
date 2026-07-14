#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pr::gameplay::attend::rendering {

// Load a raw GLB or a PRGLBZ01-wrapped GLB into identical in-memory GLB bytes.
bool loadAttendPokemonAssetBytes(
    const std::string& path,
    std::vector<std::uint8_t>& glb_bytes,
    std::string* error = nullptr);

} // namespace pr::gameplay::attend::rendering
