#pragma once

#include "core/domain/PcSlotSpecies.hpp"

#include <string>
#include <vector>

namespace pr {

struct TransferSaveSelection {
    struct PcBox {
        std::string name;
        /// Actual external save slots in this box before UI padding (20 for Gen 1/2, normally 30 otherwise).
        int native_slot_count = 0;
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
    std::string pokedex_seen;
    std::string pokedex_caught;
    std::string badges;
    /// Parsed party Pokemon summaries for the transfer ticket art strip.
    std::vector<PcSlotSpecies> party_slots;
    /// Box 1 slots parsed from the bridge probe.
    std::vector<PcSlotSpecies> box1_slots;
    /// Full PC box list (preferred over `box1_slots`).
    std::vector<PcBox> pc_boxes;
};

} // namespace pr
