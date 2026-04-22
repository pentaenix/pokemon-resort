#pragma once

#include "core/PcSlotSpecies.hpp"

#include <string>
#include <vector>

namespace pr {

struct TransferSaveSelection {
    struct PcBox {
        std::string name;
        /// Parsed slot summaries for this external save box. Rendering and future details should read this data,
        /// not raw bridge JSON.
        std::vector<PcSlotSpecies> slots;
    };
    std::string source_path;
    std::string source_filename;
    std::string game_key;
    std::string game_title;
    std::string trainer_name;
    std::string time;
    std::string pokedex;
    std::string badges;
    /// Parsed party Pokemon summaries for the transfer ticket art strip.
    std::vector<PcSlotSpecies> party_slots;
    /// Box 1 slots parsed from the bridge probe.
    std::vector<PcSlotSpecies> box1_slots;
    /// Full PC box list (preferred over `box1_slots`).
    std::vector<PcBox> pc_boxes;
};

} // namespace pr
