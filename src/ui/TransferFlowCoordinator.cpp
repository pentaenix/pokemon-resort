#include "ui/TransferFlowCoordinator.hpp"

#include "ui/loading/LoadingScreenFactory.hpp"
#include "ui/transfer_flow/TransferFlowController.hpp"
#include "ui/transfer_flow/TransferSelectionBuilder.hpp"

#include <chrono>
#include <iostream>
#include <utility>

namespace pr {

TransferFlowCoordinator::TransferFlowCoordinator(
    SDL_Renderer* renderer,
    WindowConfig window_config,
    std::string font_path,
    std::string project_root,
    std::shared_ptr<PokeSpriteAssets> sprite_assets,
    SaveLibrary& save_library,
    const char* argv0,
    resort::PokemonResortService* resort_service)
    : renderer_(renderer),
      window_config_(std::move(window_config)),
      font_path_(std::move(font_path)),
      project_root_(std::move(project_root)),
      sprite_assets_(std::move(sprite_assets)),
      save_library_(save_library),
      argv0_(argv0),
      resort_service_(resort_service) {}

void TransferFlowCoordinator::beginTicketScan() {
    ensureLoadingScreen();
    ensureTicketScreen();
    loading_screen_->enter();
    flow_controller_.beginTicketScan();
    transfer_load_future_ = std::async(
        std::launch::async,
        [this]() {
            save_library_.refreshForTransferPage();
        });
}

void TransferFlowCoordinator::update(double dt) {
    switch (flow_controller_.activeScreenKind()) {
        case ScreenKind::Loading:
            if (loading_screen_) {
                loading_screen_->update(dt);
            }
            updateLoading();
            break;
        case ScreenKind::TicketList:
            updateTicketList(dt);
            break;
        case ScreenKind::TransferSystem:
            updateTransferSystem(dt);
            break;
        case ScreenKind::None:
            break;
    }
}

Screen* TransferFlowCoordinator::activeScreen() {
    switch (flow_controller_.activeScreenKind()) {
        case ScreenKind::Loading:
            return loading_screen_.get();
        case ScreenKind::TicketList:
            return transfer_ticket_.get();
        case ScreenKind::TransferSystem:
            return transfer_system_screen_.get();
        case ScreenKind::None:
            return nullptr;
    }
    return nullptr;
}

bool TransferFlowCoordinator::consumeReturnToTitleRequest() {
    return flow_controller_.consumeReturnToTitleRequest();
}

bool TransferFlowCoordinator::consumeSuccessfulSaveReturnToTicketsRequest() {
    if (!successful_save_return_to_tickets_requested_) {
        return false;
    }
    successful_save_return_to_tickets_requested_ = false;
    return true;
}

void TransferFlowCoordinator::completeSuccessfulSaveReturnToTickets() {
    if (!transfer_system_screen_) {
        return;
    }
    flow_controller_.returnToTicketListFromTransferSystem(
        transfer_system_screen_->currentGameKey(),
        transfer_system_screen_->currentGameBoxIndex());
    if (transfer_ticket_) {
        transfer_ticket_->prepareReturnFromGameTransferScreen();
    }
}

bool TransferFlowCoordinator::hasTransferMusic() const {
    return transfer_ticket_ && !transfer_ticket_->musicPath().empty();
}

const std::string& TransferFlowCoordinator::musicPath() const {
    static const std::string kEmpty;
    return transfer_ticket_ ? transfer_ticket_->musicPath() : kEmpty;
}

double TransferFlowCoordinator::musicSilenceSeconds() const {
    return transfer_ticket_ ? transfer_ticket_->musicSilenceSeconds() : 0.0;
}

double TransferFlowCoordinator::musicFadeInSeconds() const {
    return transfer_ticket_ ? transfer_ticket_->musicFadeInSeconds() : 0.0;
}

bool TransferFlowCoordinator::consumeButtonSfxRequest() {
    if (flow_controller_.activeScreenKind() == ScreenKind::TransferSystem) {
        return transfer_system_screen_ && transfer_system_screen_->consumeButtonSfxRequest();
    }
    const bool ticket_requested =
        transfer_ticket_ && transfer_ticket_->consumeButtonSfxRequest();
    const bool system_requested =
        transfer_system_screen_ && transfer_system_screen_->consumeButtonSfxRequest();
    return ticket_requested || system_requested;
}

bool TransferFlowCoordinator::consumeRipSfxRequest() {
    return transfer_ticket_ && transfer_ticket_->consumeRipSfxRequest();
}

bool TransferFlowCoordinator::consumeUiMoveSfxRequest() {
    return transfer_system_screen_ && transfer_system_screen_->consumeUiMoveSfxRequest();
}

bool TransferFlowCoordinator::consumePickupSfxRequest() {
    return transfer_system_screen_ && transfer_system_screen_->consumePickupSfxRequest();
}

bool TransferFlowCoordinator::consumePutdownSfxRequest() {
    return transfer_system_screen_ && transfer_system_screen_->consumePutdownSfxRequest();
}

bool TransferFlowCoordinator::consumeErrorSfxRequest() {
    return transfer_system_screen_ && transfer_system_screen_->consumeErrorSfxRequest();
}

void TransferFlowCoordinator::ensureLoadingScreen() {
    if (!loading_screen_) {
        loading_screen_ = createLoadingScreen(
            LoadingScreenType::Pokeball,
            renderer_,
            window_config_,
            font_path_,
            project_root_);
    }
}

void TransferFlowCoordinator::ensureTicketScreen() {
    if (!transfer_ticket_) {
        transfer_ticket_ = std::make_unique<TransferTicketScreen>(
            renderer_,
            window_config_,
            font_path_,
            project_root_,
            sprite_assets_);
    }
}

void TransferFlowCoordinator::ensureTransferSystemScreen() {
    if (!transfer_system_screen_) {
        transfer_system_screen_ = std::make_unique<TransferSystemScreen>(
            renderer_,
            window_config_,
            font_path_,
            project_root_,
            sprite_assets_,
            save_library_.cacheDirectory(),
            argv0_,
            resort_service_);
    }
}

void TransferFlowCoordinator::updateLoading() {
    if (flow_controller_.loadingPurpose() == transfer_flow::LoadingPurpose::ScanTransferTickets) {
        if (transfer_load_future_.valid() &&
            transfer_load_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                transfer_load_future_.get();
            } catch (const std::exception& ex) {
                std::cerr << "Warning: transfer save loading failed: " << ex.what() << '\n';
            }
            ensureTicketScreen();
            transfer_ticket_->setSaveSelections(
                renderer_,
                transfer_flow::selectionsFromRecords(save_library_.transferPageRecords()));
            transfer_ticket_->enter();
            flow_controller_.finishTicketScan();
        }
        return;
    }

    if (flow_controller_.loadingPurpose() == transfer_flow::LoadingPurpose::DeepProbeSelectedSave) {
        if (transfer_detail_future_.valid() &&
            transfer_detail_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            std::optional<TransferSaveSummary> fresh_summary;
            try {
                fresh_summary = transfer_detail_future_.get();
            } catch (const std::exception& ex) {
                std::cerr << "Warning: transfer detail probe failed: " << ex.what() << '\n';
            }

            TransferSaveSelection merged = flow_controller_.pendingTransferDetailSelection();
            if (fresh_summary) {
                merged = transfer_flow::mergeFreshSummary(merged, *fresh_summary);
                std::size_t filled_slots = 0;
                for (const auto& slot : merged.box1_slots) {
                    if (slot.occupied()) {
                        ++filled_slots;
                    }
                }
                std::cerr << "[App] game transfer probe ok file=" << merged.source_filename
                          << " box1_slots=" << merged.box1_slots.size()
                          << " non_empty=" << filled_slots << '\n';
                if (merged.box1_slots.empty()) {
                    std::cerr << "[App] hint: fresh PKHeX probe returned no PC box 1 slots (transfer_save_cache is "
                                 "not used here). Check bridge/PKHeX output for this save format.\n";
                }
            } else {
                std::cerr << "Warning: fresh PKHeX probe failed — no box sprites (check .NET bridge / "
                             "PKHEX_BRIDGE_EXECUTABLE). Run: pkr clear\n";
                merged.box1_slots.clear();
            }

            flow_controller_.finishDeepProbe(std::move(merged));
            if (auto request = flow_controller_.consumeTransferSystemEntryRequest()) {
                enterTransferSystem(std::move(request->selection), request->initial_box_index);
            }
        }
    }
}

void TransferFlowCoordinator::updateTicketList(double dt) {
    if (!transfer_ticket_) {
        return;
    }

    transfer_ticket_->update(dt);
    TransferSaveSelection selected_transfer_save;
    if (transfer_ticket_->consumeOpenTransferSystemRequest(selected_transfer_save)) {
        flow_controller_.beginDeepProbe(selected_transfer_save);
        ensureLoadingScreen();
        loading_screen_->enter();
        const std::string detail_path = flow_controller_.pendingTransferDetailSelection().source_path;
        transfer_detail_future_ = std::async(
            std::launch::async,
            [root = project_root_, argv0 = argv0_, detail_path]() -> std::optional<TransferSaveSummary> {
                return probeTransferSummaryFresh(root, argv0, detail_path);
            });
    }

    if (transfer_ticket_->consumeReturnToMainMenuRequest()) {
        flow_controller_.returnToTitleFromTicketList();
    }
}

void TransferFlowCoordinator::updateTransferSystem(double dt) {
    if (!transfer_system_screen_) {
        return;
    }

    transfer_system_screen_->update(dt);
    if (transfer_system_screen_->consumeSuccessfulSaveExitRequest()) {
        successful_save_return_to_tickets_requested_ = true;
        return;
    }
    if (transfer_system_screen_->consumeReturnToTicketListRequest()) {
        flow_controller_.returnToTicketListFromTransferSystem(
            transfer_system_screen_->currentGameKey(),
            transfer_system_screen_->currentGameBoxIndex());
        if (transfer_ticket_) {
            transfer_ticket_->prepareReturnFromGameTransferScreen();
        }
    }
}

void TransferFlowCoordinator::enterTransferSystem(TransferSaveSelection selection, int initial_box_index) {
    ensureTransferSystemScreen();
    transfer_system_screen_->enter(selection, renderer_, initial_box_index);
}

} // namespace pr
