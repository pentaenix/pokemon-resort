#pragma once

#include <cstdint>
#include <string>

namespace pr::resort {

enum class MatchConfidence : std::uint8_t {
    None = 0,
    Exact = 1,
    Strong = 2,
    BestEffort = 3
};

struct PokemonMatchResult {
    bool matched = false;
    std::string pkrid;
    std::string mirror_session_id;
    MatchConfidence confidence = MatchConfidence::None;
    std::string reason;
};

struct PokemonMergeResult {
    bool changed = false;
    std::string diff_json = "{}";
};

} // namespace pr::resort
