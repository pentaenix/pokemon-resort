#include "ui/TransferSystemScreen.hpp"

#include "resort/services/PokemonResortService.hpp"
#include "ui/transfer_system/markers/TransferMarkerResolve.hpp"
#include "ui/transfer_system/markers/TransferMarkerState.hpp"

#include <vector>

namespace pr {

void TransferSystemScreen::resetTransferMarkerSession() {
    if (transfer_marker_state_) {
        transfer_marker_state_->onSaveCommittedOrDiscard();
    }
}

void TransferSystemScreen::notifyTransferMarkerAfterCrossPanelDrop(
    bool marker_commit_ok,
    const PcSlotSpecies& moved_mon,
    const transfer_system::PokemonMoveController::SlotRef& from,
    const transfer_system::PokemonMoveController::SlotRef& to) {
    if (!marker_commit_ok || !transfer_marker_state_ || from.panel == to.panel) {
        return;
    }
    transfer_marker_state_->afterSuccessfulCrossPanelPokemonDrop(moved_mon, from, to, resort_service_);
}

void TransferSystemScreen::fillTransferSlotMarkers(BoxViewportModel& model, BoxViewportRole role, int box_index) const {
    if (!transfer_marker_state_) {
        return;
    }
    if (role == BoxViewportRole::ExternalGameSave && resort_service_) {
        transfer_marker_state_->ensureProfileOpenHomeMatchKeysLoaded(resort_service_);
    }
    const TransferMarkerPanel panel =
        role == BoxViewportRole::ExternalGameSave ? TransferMarkerPanel::GameColumn : TransferMarkerPanel::ResortColumn;

    std::vector<PcSlotSpecies> slots30;
    slots30.resize(30);
    if (role == BoxViewportRole::ExternalGameSave) {
        if (box_index >= 0 && static_cast<std::size_t>(box_index) < game_pc_boxes_.size()) {
            const auto& src = game_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
            for (int i = 0; i < 30 && i < static_cast<int>(src.size()); ++i) {
                slots30[static_cast<std::size_t>(i)] = src[static_cast<std::size_t>(i)];
            }
            transfer_marker_state_->ensureTier1OpenHomeHintsForGameBox(box_index, slots30, resort_service_);
        }
    } else {
        if (box_index >= 0 && static_cast<std::size_t>(box_index) < resort_pc_boxes_.size()) {
            const auto& src = resort_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
            for (int i = 0; i < 30 && i < static_cast<int>(src.size()); ++i) {
                slots30[static_cast<std::size_t>(i)] = src[static_cast<std::size_t>(i)];
            }
        }
    }

    for (int i = 0; i < 30; ++i) {
        const PcSlotSpecies& slot = slots30[static_cast<std::size_t>(i)];
        const TransferMarkerSessionView sv = transfer_marker_state_->sessionViewForSpecies(panel, slot);
        const bool tier1 =
            panel == TransferMarkerPanel::GameColumn && box_index >= 0 ? transfer_marker_state_->tier1OpenHomeHint(box_index, i) : false;
        const bool profile_hit =
            panel == TransferMarkerPanel::GameColumn && transfer_marker_state_->profileOpenHomeMatchForSlot(slot);
        model.slot_markers[static_cast<std::size_t>(i)] =
            resolveTransferSlotMarker(panel, slot, sv, tier1, profile_hit);
    }
}

void TransferSystemScreen::markGameBoxesDirty() {
    game_boxes_dirty_ = true;
    if (transfer_marker_state_) {
        transfer_marker_state_->invalidateAllGameGreenCaches();
    }
}

void TransferSystemScreen::markResortBoxesDirty() {
    resort_boxes_dirty_ = true;
    if (transfer_marker_state_) {
        transfer_marker_state_->markProfileOpenHomeMatchKeysStale();
    }
}

} // namespace pr
