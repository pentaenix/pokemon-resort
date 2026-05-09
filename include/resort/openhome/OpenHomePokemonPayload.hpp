#pragma once

#include "resort/openhome/OpenHomeIdentity.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pr::resort::openhome {

struct OpenHomePokemonPayload {
    std::vector<std::uint8_t> serialized_identity_or_ohpkm;
    std::string openhome_format_version;
    OpenHomeId openhome_id;

    bool empty() const;
    bool hasIdentityKey() const;
};

} // namespace pr::resort::openhome
