#pragma once

#include "ui/TransferSaveSelection.hpp"

#include <optional>
#include <string>
#include <unordered_map>

namespace pr::transfer_flow {

enum class ScreenKind {
    None,
    Loading,
    TicketList,
    TransferSystem
};

enum class LoadingPurpose {
    None,
    ScanTransferTickets,
    DeepProbeSelectedSave
};

struct TransferSystemEntryRequest {
    TransferSaveSelection selection;
    int initial_box_index = 0;
};

class TransferFlowController {
public:
    void beginTicketScan();
    void finishTicketScan();
    void beginDeepProbe(TransferSaveSelection selection);
    void finishDeepProbe(TransferSaveSelection selection);
    void returnToTitleFromTicketList();
    void returnToTicketListFromTransferSystem(const std::string& game_key, int current_game_box_index);

    ScreenKind activeScreenKind() const { return active_screen_; }
    LoadingPurpose loadingPurpose() const { return loading_purpose_; }
    bool isActive() const { return active_screen_ != ScreenKind::None; }
    const TransferSaveSelection& pendingTransferDetailSelection() const { return pending_transfer_detail_selection_; }

    bool consumeReturnToTitleRequest();
    std::optional<TransferSystemEntryRequest> consumeTransferSystemEntryRequest();

private:
    int lastGameBoxIndexFor(const std::string& game_key) const;

    ScreenKind active_screen_ = ScreenKind::None;
    LoadingPurpose loading_purpose_ = LoadingPurpose::None;
    TransferSaveSelection pending_transfer_detail_selection_{};
    std::unordered_map<std::string, int> last_game_box_index_by_game_key_;
    bool return_to_title_requested_ = false;
    std::optional<TransferSystemEntryRequest> transfer_system_entry_request_;
};

} // namespace pr::transfer_flow
