#pragma once

#include "core/save/SaveLibrary.hpp"
#include "ui/TransferSaveSelection.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pr::transfer_flow {

std::string gameTitleFromId(const std::string& game_id);

TransferSaveSelection selectionFromRecord(const SaveFileRecord& record);

std::vector<TransferSaveSelection> selectionsFromRecords(const std::vector<SaveFileRecord>& records);

TransferSaveSelection mergeFreshSummary(
    const TransferSaveSelection& base,
    const TransferSaveSummary& fresh_summary);

} // namespace pr::transfer_flow
