#include "ui/TransferSystemScreen.hpp"

#include "core/domain/PcSlotSpecies.hpp"
#include "core/bridge/SaveBridgeClient.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace pr {

bool TransferSystemScreen::syncGamePcSlotHeldItemPayload(
    PcSlotSpecies& slot, int new_held_item_id, const std::string& new_held_item_name) {
    if (!slot.occupied()) {
        return false;
    }
    if (slot.bridge_box_payload_base64.empty()) {
        slot.held_item_id = new_held_item_id;
        slot.held_item_name = new_held_item_name;
        return true;
    }

    const int bridge_item_id = new_held_item_id > 0 ? new_held_item_id : 0;

    fs::path dir = save_directory_.empty() ? fs::temp_directory_path() : fs::path(save_directory_);
    std::error_code mkdir_ec;
    fs::create_directories(dir, mkdir_ec);

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path req_path = dir / ("held_item_patch_req_" + std::to_string(stamp) + ".json");

    {
        std::ofstream out(req_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out << "{\"raw_payload_base64\":\"" << slot.bridge_box_payload_base64 << "\",\"held_item_id\":" << bridge_item_id
            << "}\n";
    }

    const SaveBridgeHeldItemPatchResult patch =
        patchHeldItemPayloadWithBridge(project_root_, bridge_argv0_, req_path.string());
    std::error_code rm_ec;
    fs::remove(req_path, rm_ec);

    if (!patch.success || patch.raw_payload_base64.empty() || patch.raw_hash_sha256.empty()) {
        return false;
    }

    slot.bridge_box_payload_base64 = patch.raw_payload_base64;
    slot.bridge_box_payload_hash_sha256 = patch.raw_hash_sha256;
    slot.held_item_id = new_held_item_id;
    slot.held_item_name = new_held_item_name;
    return true;
}

} // namespace pr
