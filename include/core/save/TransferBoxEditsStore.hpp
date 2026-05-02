#pragma once

#include "ui/TransferSaveSelection.hpp"

#include <optional>
#include <string>

namespace pr {

struct TransferBoxEditsOverlay {
    /// Bump when overlay JSON gains fields required for safe save write-back (v2 adds PKM payload blobs).
    int version = 2;
    std::string source_path;
    std::string game_key;
    std::vector<TransferSaveSelection::PcBox> pc_boxes;
};

std::optional<TransferBoxEditsOverlay> loadTransferBoxEditsOverlay(
    const std::string& save_directory,
    const std::string& source_path,
    const std::string& game_key,
    std::string* error_message = nullptr);

bool saveTransferBoxEditsOverlayAtomic(
    const std::string& save_directory,
    const TransferBoxEditsOverlay& overlay,
    std::string* error_message = nullptr);

} // namespace pr

