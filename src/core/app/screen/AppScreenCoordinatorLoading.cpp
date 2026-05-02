#include "core/app/screen/AppScreenCoordinator.hpp"

#include "ui/TitleScreen.hpp"
#include "ui/TransferFlowCoordinator.hpp"

namespace pr {

void AppScreenCoordinator::updateLoading(double dt) {
    loading_.update(dt);
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
    frame_requests_.requestSaveSfx();
    loading_return_target_ = LoadingReturnTarget::TransferTickets;
    transition_controller_.startSuccessfulSaveQuickTransition();
}

void AppScreenCoordinator::beginSuccessfulSaveLoadingScreen() {
    loading_.beginSuccessfulSaveQuickPass(transfer_flow_.takeSuccessfulSaveQuickPassMessageKey());
    active_screen_ = ActiveScreen::ResortLoading;
}

void AppScreenCoordinator::finishLoadingTransition() {
    switch (loading_return_target_) {
        case LoadingReturnTarget::TransferTickets:
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
