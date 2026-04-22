#pragma once

#include <array>
#include <string>

namespace pr {

struct PcSlotMoveSummary {
    int slot_index = -1;
    int move_id = -1;
    std::string move_name;
    int current_pp = -1;
    int pp_ups = -1;
};

/// One parsed external-save PC slot payload from the PKHeX bridge.
/// This is the native transfer read model for box rendering, hover labels, future lower-bar details,
/// and a later summary screen. It is parsed once during probing and then passed around as plain data.
struct PcSlotSpecies {
    bool present = false;
    std::string area;
    int box_index = -1;
    int slot_index = -1;
    int global_index = -1;
    bool locked = false;
    bool overwrite_protected = false;
    std::string format;

    std::string slug;
    std::string species_name;
    int species_id = -1;
    std::string nickname;
    int form = -1;
    std::string form_key;
    int gender = -1;
    int level = -1;
    bool is_egg = false;
    bool is_shiny = false;

    std::string ot_name;
    int tid16 = -1;
    int sid16 = -1;

    int held_item_id = -1;
    std::string held_item_name;
    std::string nature;
    int ability_id = -1;

    std::array<PcSlotMoveSummary, 4> moves{};
    int move_count = 0;

    bool checksum_valid = false;

    bool occupied() const { return present && !slug.empty(); }
};

} // namespace pr
