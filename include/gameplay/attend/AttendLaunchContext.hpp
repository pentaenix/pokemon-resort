#pragma once

#include <optional>
#include <string>
#include <vector>

namespace pr::gameplay::attend {

struct AttendLaunchContext {
    std::string actor_id;
    std::optional<int> resort_box_id;
    std::optional<int> resort_slot_index;
    std::string species_slug;
    std::string form_id = "default";
    bool shiny = false;
    std::string display_name;
    int tile_x = 0;
    int tile_y = 0;
    std::string surface = "ground";
    std::vector<std::string> tile_tags;
    std::optional<std::string> event_id;
    std::optional<std::string> time_of_day;
    std::optional<std::string> weather;
};

} // namespace pr::gameplay::attend
