#pragma once

#include "core/bridge/SaveBridgeClient.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pr::resort {

class PokemonRepository;
class SnapshotRepository;

/// Input for the .NET `project` command (see `pkr-tools/pkhex_bridge/BridgeProject.cs`): converts snapshot
/// bytes to a target-generation encrypted PC payload using PKHeX `EntityConverter`.
struct MirrorBridgeProjectInput {
    std::string pkrid;
    std::uint16_t target_game = 0;
    std::string target_format_name;
    bool allow_lossy_projection = true;
};

struct MirrorBridgeProjectOutcome {
    pr::SaveBridgeProjectResult bridge;
    std::string error;
    std::string source_snapshot_id;
    std::string source_format_name;
};

/// Decoded + hash-verified PC payload after a successful bridge `project` run.
struct MirrorProjectDecodedResult {
    bool success = false;
    std::vector<unsigned char> raw_bytes;
    std::string raw_hash_sha256;
    std::string target_format_name;
    std::string source_snapshot_id;
    std::string source_format_name;
    /// From bridge `project` JSON (loss manifest / notes for diagnostics).
    bool bridge_lossy = false;
    std::vector<std::string> bridge_lost_categories;
    std::vector<std::string> bridge_loss_notes;
    /// PID inside the projected encrypted payload (may differ from canonical Resort PID).
    std::optional<std::uint32_t> bridge_target_pid;
    std::string error;
};

/// Outbound cross-generation projection: loads the latest non-export snapshot for `pkrid`, writes a
/// bridge `project` JSON request, and runs the helper process.
class MirrorProjectionService {
public:
    MirrorProjectionService(const PokemonRepository& pokemon, const SnapshotRepository& snapshots);

    /// Writes `request_json_path` (truncated) then invokes `projectPokemonWithBridge`.
    MirrorBridgeProjectOutcome projectLatestSnapshotToTarget(
        const MirrorBridgeProjectInput& input,
        const std::string& project_root,
        const char* argv0,
        const std::filesystem::path& request_json_path) const;

    /// Runs `projectLatestSnapshotToTarget`, decodes `target_raw_payload_base64`, and verifies SHA-256.
    MirrorProjectDecodedResult projectLatestSnapshotToTargetDecoded(
        const MirrorBridgeProjectInput& input,
        const std::string& project_root,
        const char* argv0,
        const std::filesystem::path& request_json_path) const;

private:
    const PokemonRepository& pokemon_;
    const SnapshotRepository& snapshots_;
};

} // namespace pr::resort
