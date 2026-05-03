#pragma once

#include "resort/domain/ImportedPokemon.hpp"
#include "resort/domain/MatchMerge.hpp"
#include "resort/persistence/MirrorSessionRepository.hpp"
#include "resort/persistence/PidTransportRegistryRepository.hpp"
#include "resort/persistence/PokemonRepository.hpp"

namespace pr::resort {

class PokemonMatcher {
public:
    PokemonMatcher(
        PokemonRepository& pokemon,
        MirrorSessionRepository& mirrors,
        PidTransportRegistryRepository& pid_transport);

    PokemonMatchResult findBestMatch(const ImportedPokemon& imported) const;

private:
    PokemonRepository& pokemon_;
    MirrorSessionRepository& mirrors_;
    PidTransportRegistryRepository& pid_transport_;
};

} // namespace pr::resort
