#pragma once

#include "core/PcSlotSpecies.hpp"

#include <string>
#include <vector>

namespace pr {

struct TransferSaveSelection {
    struct PcBox {
        std::string name;
        /// 30 slots (empty slug = empty).
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
    std::vector<std::string> party_sprites;
    /// Box 1 slots; size matches save box slot count; empty slug = vacant slot.
    std::vector<PcSlotSpecies> box1_slots;
    /// Full PC box list (preferred over `box1_slots`).
    std::vector<PcBox> pc_boxes;
};

} // namespace pr
