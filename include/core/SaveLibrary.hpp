#pragma once

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
    std::string game_id;
    std::string player_name;
    std::vector<std::string> party;
    std::string play_time;
    int pokedex_count = 0;
    int badges = 0;
    std::string status;
    std::string error;
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

class SaveLibrary {
public:
    SaveLibrary(std::string project_root, std::string cache_directory, const char* argv0);

    void refreshForTransferPage();
    void scanAndProbeProjectSaves();
    const std::vector<SaveFileRecord>& records() const;
    std::vector<SaveFileRecord> transferPageRecords() const;
    const SaveFileRecord* findRecordByPath(const std::string& path) const;
    const SaveFileRecord* findRecordByGameId(const std::string& game_id) const;

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
