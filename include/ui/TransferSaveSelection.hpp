#pragma once

#include <string>
#include <vector>

namespace pr {

struct TransferSaveSelection {
    std::string source_path;
    std::string source_filename;
    std::string game_key;
    std::string game_title;
    std::string trainer_name;
    std::string time;
    std::string pokedex;
    std::string badges;
    std::vector<std::string> party_sprites;
    /// Box 1 slot species slugs; size matches save box slot count; empty string = vacant slot.
    std::vector<std::string> box1_slots;
};

} // namespace pr
