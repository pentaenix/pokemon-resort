#pragma once

#include "resort/openhome/OpenHomePokemonPayload.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace pr::resort::openhome {

struct OpenHomeSaveSlot {
    std::filesystem::path save_path;
    std::string save_type;
    int box = 0;
    int slot = 0;
};

struct OpenHomeHomeSlot {
    int bank = 0;
    int box = 0;
    int slot = 0;
};

struct OpenHomeMovementError {
    std::string code;
    std::string message;
};

struct OpenHomeSyncSaveRequest {
    std::filesystem::path save_path;
    std::string save_type;
    bool write_synced_ohpkm_store = true;
};

struct OpenHomeSyncSaveResult {
    bool success = false;
    std::vector<OpenHomeId> matched_openhome_ids;
    std::vector<OpenHomePokemonPayload> updated_payloads;
    std::optional<OpenHomeMovementError> error;
};

struct OpenHomePullToHomeRequest {
    OpenHomeSaveSlot source;
    OpenHomeHomeSlot destination;
    bool write_source_save = true;
    bool write_home_banks = true;
    bool write_ohpkm_store = true;
};

struct OpenHomePullToHomeResult {
    bool success = false;
    OpenHomeId openhome_id;
    OpenHomePokemonPayload payload;
    std::optional<OpenHomeId> displaced_home_openhome_id;
    bool source_save_written = false;
    bool home_banks_written = false;
    bool ohpkm_store_written = false;
    std::optional<OpenHomeMovementError> error;
};

struct OpenHomePushToGameRequest {
    OpenHomeId openhome_id;
    OpenHomeSaveSlot destination;
    bool write_target_save = true;
    bool write_ohpkm_store = true;
};

struct OpenHomePushToGameResult {
    bool success = false;
    OpenHomeId openhome_id;
    std::optional<OpenHomePokemonPayload> updated_payload;
    bool target_save_written = false;
    bool ohpkm_store_written = false;
    std::optional<OpenHomeMovementError> error;
};

struct OpenHomeMoveBetweenGamesRequest {
    OpenHomeSaveSlot source;
    OpenHomeSaveSlot destination;
    bool write_source_save = true;
    bool write_target_save = true;
    bool write_ohpkm_store = true;
};

struct OpenHomeMoveBetweenGamesResult {
    bool success = false;
    OpenHomeId openhome_id;
    OpenHomePokemonPayload payload;
    bool source_save_written = false;
    bool target_save_written = false;
    bool ohpkm_store_written = false;
    std::optional<OpenHomeMovementError> error;
};

struct OpenHomePokemonSupportQuery {
    int dex_number = -1;
    int form_number = 0;
};

struct OpenHomePokemonSupportResult {
    bool success = false;
    std::unordered_map<std::string, bool> supported_by_key;
    std::optional<OpenHomeMovementError> error;
};

enum class OpenHomeMovementStep {
    LoadSourceSave,
    LoadTargetSave,
    SyncTrackedPokemonWithSaveData,
    LoadTrackedPokemonOrStartTracking,
    ConvertOhpkmForTargetSave,
    ClearSourceSaveSlot,
    WriteTargetSaveSlot,
    UpsertOhpkmStore,
    PlaceOpenHomeIdInHomeBank,
    WriteOpenHomeBanks,
    PrepareSaveWriter,
    WriteSaveFile
};

class IOpenHomeMovementBridge {
public:
    virtual ~IOpenHomeMovementBridge() = default;

    virtual OpenHomeSyncSaveResult syncOpenSave(const OpenHomeSyncSaveRequest& request) = 0;
    virtual OpenHomePullToHomeResult pullPokemonToHome(const OpenHomePullToHomeRequest& request) = 0;
    virtual OpenHomePushToGameResult pushPokemonToGame(const OpenHomePushToGameRequest& request) = 0;
    virtual OpenHomeMoveBetweenGamesResult movePokemonBetweenGames(
        const OpenHomeMoveBetweenGamesRequest& request) = 0;
    virtual OpenHomePokemonSupportResult queryPokemonSupport(
        const OpenHomeSaveSlot& save,
        const std::vector<OpenHomePokemonSupportQuery>& queries) = 0;
};

class OpenHomeCliMovementBridge final : public IOpenHomeMovementBridge {
public:
    OpenHomeCliMovementBridge(
        std::filesystem::path project_root,
        std::filesystem::path storage_root);

    OpenHomeSyncSaveResult syncOpenSave(const OpenHomeSyncSaveRequest& request) override;
    OpenHomePullToHomeResult pullPokemonToHome(const OpenHomePullToHomeRequest& request) override;
    OpenHomePushToGameResult pushPokemonToGame(const OpenHomePushToGameRequest& request) override;
    OpenHomeMoveBetweenGamesResult movePokemonBetweenGames(
        const OpenHomeMoveBetweenGamesRequest& request) override;
    OpenHomePokemonSupportResult queryPokemonSupport(
        const OpenHomeSaveSlot& save,
        const std::vector<OpenHomePokemonSupportQuery>& queries) override;

    std::filesystem::path bridgeScriptPath() const;

private:
    std::filesystem::path project_root_;
    std::filesystem::path storage_root_;
};

bool isValidOpenHomeSaveSlot(const OpenHomeSaveSlot& slot);
bool isValidOpenHomeHomeSlot(const OpenHomeHomeSlot& slot);

std::vector<OpenHomeMovementStep> planOpenHomePullToHome(const OpenHomePullToHomeRequest& request);
std::vector<OpenHomeMovementStep> planOpenHomePushToGame(const OpenHomePushToGameRequest& request);
std::vector<OpenHomeMovementStep> planOpenHomeMoveBetweenGames(const OpenHomeMoveBetweenGamesRequest& request);

const char* openHomeMovementStepName(OpenHomeMovementStep step);

} // namespace pr::resort::openhome
