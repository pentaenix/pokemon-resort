#pragma once

#include "resort/domain/ResortTypes.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pr::resort {

struct ExportContext {
    std::uint16_t target_game = 0;
    std::string target_format_name = "projection-json";
    bool managed_mirror = true;
    bool use_gen12_beacon = false;
};

struct ExportResult {
    bool success = false;
    std::string pkrid;
    std::string snapshot_id;
    std::string mirror_session_id;
    std::string format_name;
    std::vector<unsigned char> raw_payload;
    std::string raw_hash;
    std::string error;
};

} // namespace pr::resort
