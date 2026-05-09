#include "resort/domain/ResortPokemonRecord.hpp"

namespace pr::resort {

bool ResortPokemonRecord::visibleInNormalBoxes() const {
    return isVisibleInNormalResortBoxes(presence);
}

bool ResortPokemonRecord::hasOpenHomePayload() const {
    return !openhome_payload.empty();
}

} // namespace pr::resort
