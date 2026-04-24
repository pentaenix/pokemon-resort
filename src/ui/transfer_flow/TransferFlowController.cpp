#include "ui/transfer_flow/TransferFlowController.hpp"

#include <utility>

namespace pr::transfer_flow {

void TransferFlowController::beginTicketScan() {
    loading_purpose_ = LoadingPurpose::ScanTransferTickets;
    active_screen_ = ScreenKind::Loading;
    return_to_title_requested_ = false;
    transfer_system_entry_request_.reset();
}

void TransferFlowController::finishTicketScan() {
    loading_purpose_ = LoadingPurpose::None;
    active_screen_ = ScreenKind::TicketList;
}

void TransferFlowController::beginDeepProbe(TransferSaveSelection selection) {
    pending_transfer_detail_selection_ = std::move(selection);
    loading_purpose_ = LoadingPurpose::DeepProbeSelectedSave;
    active_screen_ = ScreenKind::Loading;
    transfer_system_entry_request_.reset();
}

void TransferFlowController::finishDeepProbe(TransferSaveSelection selection) {
    loading_purpose_ = LoadingPurpose::None;
    active_screen_ = ScreenKind::TransferSystem;
    const int initial_box_index = lastGameBoxIndexFor(selection.game_key);
    transfer_system_entry_request_ = TransferSystemEntryRequest{
        std::move(selection),
        initial_box_index};
}

void TransferFlowController::returnToTitleFromTicketList() {
    last_game_box_index_by_game_key_.clear();
    loading_purpose_ = LoadingPurpose::None;
    active_screen_ = ScreenKind::None;
    return_to_title_requested_ = true;
    transfer_system_entry_request_.reset();
}

void TransferFlowController::returnToTicketListFromTransferSystem(
    const std::string& game_key,
    int current_game_box_index) {
    if (!game_key.empty()) {
        last_game_box_index_by_game_key_[game_key] = current_game_box_index;
    }
    active_screen_ = ScreenKind::TicketList;
    loading_purpose_ = LoadingPurpose::None;
}

bool TransferFlowController::consumeReturnToTitleRequest() {
    const bool requested = return_to_title_requested_;
    return_to_title_requested_ = false;
    return requested;
}

std::optional<TransferSystemEntryRequest> TransferFlowController::consumeTransferSystemEntryRequest() {
    std::optional<TransferSystemEntryRequest> request;
    request.swap(transfer_system_entry_request_);
    return request;
}

int TransferFlowController::lastGameBoxIndexFor(const std::string& game_key) const {
    if (game_key.empty()) {
        return 0;
    }
    auto it = last_game_box_index_by_game_key_.find(game_key);
    return it == last_game_box_index_by_game_key_.end() ? 0 : it->second;
}

} // namespace pr::transfer_flow
