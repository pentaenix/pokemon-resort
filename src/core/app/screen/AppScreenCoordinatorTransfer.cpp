#include "core/app/screen/AppScreenCoordinator.hpp"

#include "ui/AttendTestScreen.hpp"
#include "ui/Overworld3DTestScreen.hpp"
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

void AppScreenCoordinator::collectOverworldFrameRequests() {
    frame_requests_.requestOverworldBlockedSfxIf(
        active_screen_ == ActiveScreen::Overworld3DTest &&
        overworld3d_test_.consumeBlockedMovementSfxRequested());
    if (active_screen_ == ActiveScreen::Overworld3DTest) {
        frame_requests_.requestErrorSfxIf(
            overworld3d_test_.consumeAquariumConstructionErrorSfxRequested());
        frame_requests_.requestUiMoveSfxIf(
            overworld3d_test_.consumeAquariumConstructionMoveSfxRequested());
        if (overworld3d_test_.consumeAquariumConstructionSaveSfxRequested()) {
            frame_requests_.requestSaveSfx();
        }
    }
    if (active_screen_ == ActiveScreen::TestAttend) {
        for (const std::string& request : attend_test_.consumeOneShotSfxRequests()) {
            frame_requests_.requestOneShotSfx(request);
        }
    }
}

} // namespace pr
