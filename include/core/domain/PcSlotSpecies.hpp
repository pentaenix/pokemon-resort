#pragma once

#include <array>
#include <cstdint>
#include <optional>
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
    int exp = -1;
    bool is_egg = false;
    bool is_shiny = false;
    int ball_id = -1;

    std::string ot_name;
    int tid16 = -1;
    int sid16 = -1;
    std::optional<std::uint32_t> tid32;
    std::string origin_game;
    int origin_game_id = -1;
    /// Numeric game version this slot was imported from. This is distinct from the
    /// Pokemon's own origin game and should travel with Resort slots across active saves.
    int source_game_id = -1;
    /// Concrete app game key for the save this slot came from (for example `pokemon_heartgold`).
    /// Prefer this over aggregate bridge version ids such as HGSS when present.
    std::string source_game_key;
    /// Save-level context captured when this Pokemon first enters Resort. This is intentionally lightweight
    /// provenance for future history features, while the concrete source game key drives current UI.
    std::string source_save_trainer_name;
    std::string source_save_play_time;
    std::string source_save_badges;
    int language = -1;
    int met_location_id = -1;
    std::string met_location_name;
    int met_level = -1;
    std::optional<std::int64_t> met_date_unix;

    int held_item_id = -1;
    std::string held_item_name;
    std::string nature;
    int ability_id = -1;
    int ability_slot = -1;
    std::string ability_name;
    std::string primary_type;
    std::string secondary_type;
    std::string tera_type;
    std::string mark_icon;
    std::string pokerus_status;
    bool is_alpha = false;
    bool is_gigantamax = false;
    int markings = 0;
    int hp_current = -1;
    int hp_max = -1;
    int status_flags = 0;

    std::array<PcSlotMoveSummary, 4> moves{};
    int move_count = 0;

    bool checksum_valid = false;

    // Stable identity evidence (Gen 3+): used to match returns to existing Resort canonicals.
    // Populated via bridge import merge when available.
    std::optional<std::uint32_t> pid;
    std::optional<std::uint32_t> encryption_constant;
    std::string home_tracker;
    std::optional<std::uint16_t> dv16;
    int lineage_root_species = -1;

    /// Canonical Resort SQLite key (`pokemon.pkrid`) when this slot was loaded from `profile.resort.db`.
    std::string resort_pkrid;

    /// Import-grade encrypted PC slot payload from `PKHeXBridge import` (`EncryptedBoxData`), base64-encoded.
    /// Required for safe write-back when moving Pokémon between PC slots; copied whenever the slot is moved in UI.
    std::string bridge_box_payload_base64;
    /// Lowercase hex SHA-256 of decoded `bridge_box_payload_base64` bytes (matches bridge `raw_hash_sha256`).
    std::string bridge_box_payload_hash_sha256;

    bool occupied() const { return present && (!slug.empty() || !resort_pkrid.empty()); }
};

} // namespace pr
