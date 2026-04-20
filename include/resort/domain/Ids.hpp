#pragma once

#include <string>

namespace pr::resort {

std::string generateId(const char* prefix);
std::string fingerprintForFirstSeenPokemon(
    unsigned int source_game,
    const std::string& format_name,
    const std::string& trainer_name,
    unsigned int species_id,
    const std::string& raw_hash);
long long unixNow();

} // namespace pr::resort
