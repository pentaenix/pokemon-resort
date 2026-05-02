#include "core/app/screen/AppScreenCoordinator.hpp"

#include "ui/TitleScreen.hpp"
#include "ui/TransferFlowCoordinator.hpp"

namespace pr {

void AppScreenCoordinator::updateTransfer(double dt) {
    transfer_flow_.update(dt);
    if (transfer_flow_.consumeSuccessfulSaveReturnToTicketsRequest()) {
        startSuccessfulSaveQuickTransition();
    } else if (transfer_flow_.consumeReturnToTitleRequest()) {
        title_screen_.returnToMainMenuFromTransfer();
        active_screen_ = ActiveScreen::Title;
    }
}

void AppScreenCoordinator::collectTransferFrameRequests() {
    frame_requests_.requestButtonSfxIf(transfer_flow_.consumeButtonSfxRequest());
    frame_requests_.requestRipSfxIf(transfer_flow_.consumeRipSfxRequest());
    frame_requests_.requestUiMoveSfxIf(transfer_flow_.consumeUiMoveSfxRequest());
    frame_requests_.requestPickupSfxIf(transfer_flow_.consumePickupSfxRequest());
    frame_requests_.requestPutdownSfxIf(transfer_flow_.consumePutdownSfxRequest());
    frame_requests_.requestErrorSfxIf(transfer_flow_.consumeErrorSfxRequest());
}

} // namespace pr
