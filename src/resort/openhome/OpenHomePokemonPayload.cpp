#include "resort/openhome/OpenHomePokemonPayload.hpp"

namespace pr::resort::openhome {

bool OpenHomePokemonPayload::empty() const {
    return serialized_identity_or_ohpkm.empty() && openhome_id.empty();
}

bool OpenHomePokemonPayload::hasIdentityKey() const {
    return !openhome_id.empty();
}

} // namespace pr::resort::openhome
