#pragma once

#include "resort/domain/PokemonPresenceState.hpp"
#include "resort/domain/ResortTypes.hpp"
#include "resort/openhome/OpenHomePokemonPayload.hpp"

#include <string>
#include <vector>

namespace pr::resort {

struct ResortPokemonRecord {
    openhome::OpenHomeId openhome_id;
    openhome::OpenHomePokemonPayload openhome_payload;

    std::string memories_json = "{}";
    std::vector<std::string> visited_games;
    std::string party_data_json = "{}";
    std::string relationships_json = "{}";

    PokemonPresenceState presence = PokemonPresenceState::AvailableInResort;
    PokemonAwayLocation away_location;

    // Legacy canonical row retained only while PKHeX-backed import/export is still being migrated.
    // New OpenHome-backed storage should use `openhome_id` as the Pokemon identity.
    ResortPokemon legacy_canonical;

    bool visibleInNormalBoxes() const;
    bool hasOpenHomePayload() const;
};

} // namespace pr::resort
