#pragma once

#include <string>

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

/// Human-readable summary when `success` is false: spawn/pipe errors from `error_message`, otherwise
/// parsed JSON fields from stdout (`error`, `details`, `status`) plus stderr fallback.
std::string formatBridgeRunFailureMessage(const SaveBridgeProbeResult& result);

} // namespace pr
