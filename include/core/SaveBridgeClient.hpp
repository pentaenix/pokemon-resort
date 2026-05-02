#pragma once

#include <string>
#include <vector>

namespace pr {

struct SaveBridgeProbeResult {
    bool launched = false;
    bool success = false;
    int exit_code = -1;
    std::string bridge_path;
    std::string command;
    std::string save_path;
    std::string stdout_text;
    std::string stderr_text;
    std::string error_message;
};

SaveBridgeProbeResult probeSaveWithBridge(
    const std::string& project_root,
    const char* argv0,
    const std::string& save_path);

SaveBridgeProbeResult importSaveWithBridge(
    const std::string& project_root,
    const char* argv0,
    const std::string& save_path);

SaveBridgeProbeResult writeProjectionWithBridge(
    const std::string& project_root,
    const char* argv0,
    const std::string& save_path,
    const std::string& projection_json_path);

struct SaveBridgeHeldItemPatchResult {
    bool launched = false;
    bool success = false;
    int exit_code = -1;
    std::string raw_payload_base64;
    std::string raw_hash_sha256;
    std::string stdout_text;
    std::string stderr_text;
    std::string error_message;
};

/// Runs `pkm-patch-held-item` with a JSON request file (`raw_payload_base64`, `held_item_id`).
SaveBridgeHeldItemPatchResult patchHeldItemPayloadWithBridge(
    const std::string& project_root,
    const char* argv0,
    const std::string& request_json_path);

struct SaveBridgeProjectResult {
    bool launched = false;
    bool success = false;
    int exit_code = -1;
    std::string target_format_name;
    std::string target_raw_payload_base64;
    std::string target_raw_hash_sha256;
    bool legality_valid = false;
    bool loss_manifest_lossy = false;
    std::vector<std::string> legality_warnings;
    std::vector<std::string> lost_categories;
    std::vector<std::string> projected_categories;
    std::vector<std::string> loss_notes;
    std::string stdout_text;
    std::string stderr_text;
    std::string error_message;
};

/// Parses bridge `project` stdout. Exposed for contract tests; production code should use
/// `projectPokemonWithBridge` so process errors and exit-code failures are preserved.
SaveBridgeProjectResult parseBridgeProjectResultJson(const std::string& stdout_text);

/// Runs `project` with a JSON request file. This is the native seam for Resort mirror projection.
SaveBridgeProjectResult projectPokemonWithBridge(
    const std::string& project_root,
    const char* argv0,
    const std::string& request_json_path);

/// Human-readable summary when `success` is false: spawn/pipe errors from `error_message`, otherwise
/// parsed JSON fields from stdout (`error`, `details`, `status`) plus stderr fallback.
std::string formatBridgeRunFailureMessage(const SaveBridgeProbeResult& result);

} // namespace pr
