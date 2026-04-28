#pragma once

#include "core/PcSlotSpecies.hpp"
#include "core/Types.hpp"

#include <string>

namespace pr::transfer_system {

struct TransferInfoBannerContext {
    std::string mode = "empty";
    const PcSlotSpecies* slot = nullptr;
    std::string source_game_key;
    std::string game_title;
    std::string trainer_name;
    std::string play_time;
    std::string pokedex_seen;
    std::string pokedex_caught;
    std::string badges;
    int selected_tool_index = 0;
    bool items_mode = false;
    GameTransferInfoBannerStyle tooltip_copy{};
    /// Occupied Pokémon slots across all Resort PC boxes (for resort footer icon tooltip).
    int resort_storage_occupied_slots = 0;
    /// Total Pokémon slot capacity across Resort PC boxes (usually boxes × 30).
    int resort_storage_total_slots = 0;
};

struct TransferInfoBannerFieldValue {
    bool visible = false;
    bool use_pokesprite_item = false;
    int pokesprite_item_id = -1;
    std::string text;
    std::string icon_key;
    std::string icon_group = "pokemon";
};

std::string sourceRegionKeyForGameId(const std::string& game_id);

TransferInfoBannerFieldValue resolveTransferInfoBannerField(
    const std::string& field_name,
    const TransferInfoBannerContext& context);

} // namespace pr::transfer_system
