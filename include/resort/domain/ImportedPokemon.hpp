#pragma once

#include "resort/domain/ResortTypes.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pr::resort {

struct IdentityEvidence {
    std::optional<std::uint32_t> pid;
    std::optional<std::uint32_t> encryption_constant;
    std::optional<std::string> home_tracker;
    std::optional<std::uint16_t> tid16;
    std::optional<std::uint16_t> sid16;
    std::string ot_name;
    std::optional<std::uint16_t> dv16;
    std::uint16_t lineage_root_species = 0;
};

struct ImportContext {
    std::string profile_id = "default";
    std::optional<BoxLocation> target_location;
    std::string source_label;
};

struct ImportedPokemon {
    std::uint16_t source_game = 0;
    std::string format_name;
    std::vector<std::uint8_t> raw_bytes;
    PokemonHot hot;
    std::string warm_json = "{}";
    std::string suspended_json = "{}";
    IdentityEvidence identity;
};

struct ImportResult {
    bool success = false;
    std::string pkrid;
    std::string snapshot_id;
    std::string error;
};

} // namespace pr::resort
