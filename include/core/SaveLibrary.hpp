#pragma once

#include "core/PcSlotSpecies.hpp"
#include "core/SaveBridgeClient.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pr {

enum class SaveProbeStatus {
    NotProbed,
    ValidSave,
    InvalidSave,
    BridgeError
};

struct TransferSaveSummary {
    /// Present in full bridge JSON (`bridge_probe_schema`). 0 = missing/legacy stub binary.
    int bridge_probe_schema = 0;
    std::string game_id;
    std::string player_name;
    std::vector<std::string> party;
    /// Parsed party Pokemon derived from bridge `all_pokemon` when available, with `party` string fallback for legacy JSON.
    std::vector<PcSlotSpecies> party_slots;
    std::string play_time;
    int pokedex_count = 0;
    int pokedex_seen_count = 0;
    int pokedex_caught_count = 0;
    int badges = 0;
    std::string status;
    std::string error;
    /// PC box 1 slots parsed once from the bridge probe. Same order as bridge `boxes[0].slots` / `box_1`.
    std::vector<PcSlotSpecies> box_1_slots;
    struct PcBox {
        std::string name;
        /// Transfer-ready slot payloads in bridge order. Current transfer UI normalizes to 30 visible cells.
        std::vector<PcSlotSpecies> slots;
    };
    /// Full PC box list when available (preferred over `box_1_slots`).
    std::vector<PcBox> pc_boxes;
};

struct SaveFileRecord {
    std::string path;
    std::string filename;
    std::uintmax_t size = 0;
    std::filesystem::file_time_type last_write_time{};
    std::string file_hash;
    SaveProbeStatus probe_status = SaveProbeStatus::NotProbed;
    std::string raw_bridge_output;
    SaveBridgeProbeResult bridge_result;
    std::optional<TransferSaveSummary> transfer_summary;
    bool used_cache = false;
};

/// Full bridge parse for one file (always runs PKHeX; not backed by transfer_save_cache).
/// Use after the player picks a save when you need `box_1_slots` and other expanded fields.
std::optional<TransferSaveSummary> probeTransferSummaryFresh(
    const std::string& project_root,
    const char* argv0,
    const std::string& save_path);

class SaveLibrary {
public:
    SaveLibrary(std::string project_root, std::string cache_directory, const char* argv0);

    void refreshForTransferPage();
    void scanAndProbeProjectSaves();
    const std::vector<SaveFileRecord>& records() const;
    std::vector<SaveFileRecord> transferPageRecords() const;
    const SaveFileRecord* findRecordByPath(const std::string& path) const;
    const SaveFileRecord* findRecordByGameId(const std::string& game_id) const;
    const std::string& cacheDirectory() const { return cache_directory_; }

private:
    std::filesystem::path savesDirectory() const;
    std::filesystem::path cacheFilePath() const;
    void discoverFiles();
    void probeDiscoveredFiles();
    void loadCache();
    void saveCache() const;

    std::string project_root_;
    std::string cache_directory_;
    const char* argv0_ = nullptr;
    std::vector<SaveFileRecord> records_;
};

} // namespace pr
