#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pr::resort {

struct BoxLocation {
    std::string profile_id;
    int box_id = 0;
    int slot_index = 0;
};

enum class BoxPlacementPolicy : std::uint8_t {
    RejectIfOccupied,
    ReplaceOccupied
};

struct PokemonId {
    std::string pkrid;
    std::string origin_fingerprint;
};

struct PokemonHot {
    std::uint16_t species_id = 0;
    std::uint16_t form_id = 0;
    std::string nickname;
    bool is_nicknamed = false;

    std::uint8_t level = 1;
    std::uint32_t exp = 0;
    std::uint8_t gender = 0;
    bool shiny = false;

    std::optional<std::uint16_t> ability_id;
    std::optional<std::uint8_t> ability_slot;
    std::optional<std::uint16_t> held_item_id;

    std::array<std::optional<std::uint16_t>, 4> move_ids{};
    std::array<std::optional<std::uint8_t>, 4> move_pp{};
    std::array<std::optional<std::uint8_t>, 4> move_pp_ups{};

    std::uint16_t hp_current = 0;
    std::uint16_t hp_max = 0;
    std::uint32_t status_flags = 0;

    std::string ot_name;
    std::optional<std::uint16_t> tid16;
    std::optional<std::uint16_t> sid16;
    std::optional<std::uint32_t> tid32;

    std::uint16_t origin_game = 0;
    std::optional<std::uint8_t> language;
    std::optional<std::uint16_t> met_location_id;
    std::optional<std::uint8_t> met_level;
    std::optional<std::int64_t> met_date_unix;
    std::optional<std::uint16_t> ball_id;

    std::optional<std::uint32_t> pid;
    std::optional<std::uint32_t> encryption_constant;
    std::optional<std::string> home_tracker;
    std::optional<std::uint16_t> dv16;

    std::uint16_t lineage_root_species = 0;
    std::uint8_t identity_strength = 0;
};

struct PokemonWarm {
    std::string json = "{}";
};

struct PokemonCold {
    std::string suspended_json = "{}";
};

struct ResortPokemon {
    PokemonId id;
    PokemonHot hot;
    PokemonWarm warm;
    PokemonCold cold;
    std::uint64_t revision = 1;
    std::int64_t created_at_unix = 0;
    std::int64_t updated_at_unix = 0;
};

struct PokemonSlotView {
    std::string pkrid;
    std::uint16_t species_id = 0;
    std::uint16_t form_id = 0;
    std::string display_name;
    std::uint8_t level = 1;
    bool shiny = false;
    std::uint8_t gender = 0;
    std::optional<std::uint16_t> held_item_id;
    std::uint16_t hp_current = 0;
    std::uint16_t hp_max = 0;
    std::uint8_t status_icon = 0;
    int slot_index = 0;
};

enum class SnapshotKind : std::uint8_t {
    ImportedRaw = 0,
    ExportProjection = 1,
    ReturnRaw = 2,
    CanonicalCheckpoint = 3
};

struct PokemonSnapshot {
    std::string snapshot_id;
    std::string pkrid;
    SnapshotKind kind = SnapshotKind::ImportedRaw;
    std::string format_name;
    std::optional<std::uint16_t> game_id;
    std::int64_t captured_at_unix = 0;
    std::vector<std::uint8_t> raw_bytes;
    std::string raw_hash_sha256;
    std::string parsed_json = "{}";
    std::string notes_json = "{}";
};

enum class HistoryEventType : std::uint8_t {
    Created = 0,
    Imported = 1,
    Exported = 2,
    Merged = 3,
    MovedBox = 4,
    MirrorOpened = 5,
    MirrorReturned = 6,
    Archived = 7
};

struct PokemonHistoryEvent {
    std::string event_id;
    std::string pkrid;
    HistoryEventType event_type = HistoryEventType::Created;
    std::int64_t timestamp_unix = 0;
    std::optional<std::string> source_snapshot_id;
    std::optional<std::string> mirror_session_id;
    std::string diff_json = "{}";
};

enum class MirrorStatus : std::uint8_t {
    Active = 0,
    Returned = 1,
    Lost = 2,
    Abandoned = 3
};

struct MirrorSession {
    std::string mirror_session_id;
    std::string pkrid;
    std::uint16_t target_game = 0;
    MirrorStatus status = MirrorStatus::Active;
    std::int64_t created_at_unix = 0;
    std::optional<std::int64_t> returned_at_unix;
    std::optional<std::uint16_t> beacon_tid16;
    std::optional<std::string> beacon_ot_name;
    std::uint16_t sent_species_id = 0;
    std::uint16_t sent_form_id = 0;
    std::uint16_t sent_lineage_root = 0;
    std::uint8_t sent_level = 1;
    std::uint32_t sent_exp = 0;
    std::optional<std::string> original_ot_name;
    std::optional<std::uint16_t> original_tid16;
    std::optional<std::uint16_t> original_sid16;
    std::optional<std::uint16_t> original_game;
    std::optional<std::uint16_t> sent_dv16;
    std::string projection_json = "{}";
};

} // namespace pr::resort
