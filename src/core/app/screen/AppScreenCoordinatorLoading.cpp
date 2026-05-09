#include "core/app/screen/AppScreenCoordinator.hpp"

#include "ui/TitleScreen.hpp"
#include "ui/TransferFlowCoordinator.hpp"

#include <chrono>

namespace pr {

void AppScreenCoordinator::updateLoading(double dt) {
    loading_.update(dt);

    if (pending_successful_save_quick_pass_work_ &&
        loading_return_target_ == LoadingReturnTarget::TransferTickets) {
        if (successful_save_deferred_io_frames_remaining_ > 0) {
            --successful_save_deferred_io_frames_remaining_;
        } else if (!successful_save_async_io_.has_value()) {
            successful_save_async_io_ = transfer_flow_.launchDeferredSaveForSuccessfulExitAsync();
        } else if (successful_save_async_io_->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            const bool ok = successful_save_async_io_->get();
            successful_save_async_io_.reset();
            pending_successful_save_quick_pass_work_ = false;
            deferred_successful_save_exit_failed_ = !ok;
            if (deferred_successful_save_exit_failed_) {
                frame_requests_.requestErrorSfxIf(true);
            }
            loading_.markSuccessfulSaveQuickPassWorkComplete();
        }
    }
    if (!transition_controller_.active() && loading_.consumeReturnToMenuRequest()) {
        finishLoadingTransition();
        return;
    }

    if (!transition_controller_.active() &&
        loading_return_target_ == LoadingReturnTarget::TradeTitle &&
        loading_.isLoadingAnimationComplete()) {
        title_screen_.returnToMainMenuFromTradeLoading();
        active_screen_ = ActiveScreen::Title;
        return;
    }

    if (!transition_controller_.active() &&
        loading_return_target_ == LoadingReturnTarget::TransferTickets &&
        loading_.isLoadingAnimationComplete()) {
        active_screen_ = ActiveScreen::TransferFlow;
    }
}

void AppScreenCoordinator::updateTransition(double dt) {
    const AppTransitionController::StepResult transition_step =
        transition_controller_.update(dt, loading_.isLoadingAnimationComplete());
    if (transition_step.begin_loading) {
        beginSuccessfulSaveLoadingScreen();
    }
    if (transition_step.finish_destination) {
        finishLoadingTransition();
    }
}

void AppScreenCoordinator::startSuccessfulSaveQuickTransition() {
    deferred_successful_save_exit_failed_ = false;
    pending_successful_save_quick_pass_work_ = false;
    successful_save_deferred_io_frames_remaining_ = 0;
    successful_save_async_io_.reset();
    frame_requests_.requestSaveSfx();
    loading_return_target_ = LoadingReturnTarget::TransferTickets;
    transition_controller_.startSuccessfulSaveQuickTransition();
}

void AppScreenCoordinator::beginSuccessfulSaveLoadingScreen() {
    loading_.beginSuccessfulSaveQuickPass(transfer_flow_.takeSuccessfulSaveQuickPassMessageKey());
    active_screen_ = ActiveScreen::ResortLoading;
    pending_successful_save_quick_pass_work_ = true;
    successful_save_deferred_io_frames_remaining_ = 1;
}

void AppScreenCoordinator::finishLoadingTransition() {
    switch (loading_return_target_) {
        case LoadingReturnTarget::TransferTickets:
            if (deferred_successful_save_exit_failed_) {
                deferred_successful_save_exit_failed_ = false;
                transfer_flow_.notifyDeferredSuccessfulSaveLoadingAborted();
                active_screen_ = ActiveScreen::TransferFlow;
                break;
            }
            transfer_flow_.completeSuccessfulSaveReturnToTickets();
            active_screen_ = ActiveScreen::TransferFlow;
            break;
        case LoadingReturnTarget::TradeTitle:
            title_screen_.returnToMainMenuFromTradeLoading();
            active_screen_ = ActiveScreen::Title;
            break;
        case LoadingReturnTarget::ResortTitle:
            title_screen_.returnToMainMenuFromResort();
            active_screen_ = ActiveScreen::Title;
            break;
    }
}

} // namespace pr
