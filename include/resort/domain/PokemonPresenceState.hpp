#pragma once

#include <string>

namespace pr::resort {

enum class PokemonPresenceState {
    AvailableInResort,
    AwayInGame,
    PendingReturn,
    Unavailable
};

struct PokemonAwayLocation {
    std::string game_id;
    std::string save_id;
    std::string generation;
    std::string export_session_id;
    std::string projected_identity_hint;
};

inline bool isVisibleInNormalResortBoxes(PokemonPresenceState presence) {
    return presence == PokemonPresenceState::AvailableInResort;
}

inline PokemonPresenceState inferPresenceFromPlacement(bool has_resort_box_location, bool has_active_mirror) {
    if (has_resort_box_location) {
        return PokemonPresenceState::AvailableInResort;
    }
    if (has_active_mirror) {
        return PokemonPresenceState::AwayInGame;
    }
    return PokemonPresenceState::Unavailable;
}

} // namespace pr::resort
