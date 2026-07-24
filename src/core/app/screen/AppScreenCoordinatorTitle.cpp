#include "core/app/screen/AppScreenCoordinator.hpp"

#include "ui/AttendTestScreen.hpp"
#include "ui/Overworld3DTestScreen.hpp"
#include "ui/TitleScreen.hpp"
#include "ui/TransferFlowCoordinator.hpp"

namespace pr {

void AppScreenCoordinator::updateTitle(double dt) {
    title_screen_.update(dt);
    for (TitleScreenEvent event : title_screen_.consumeEvents()) {
        switch (event) {
            case TitleScreenEvent::ButtonSfxRequested:
                frame_requests_.requestButtonSfx();
                break;
            case TitleScreenEvent::UserSettingsSaveRequested:
                frame_requests_.requestUserSettingsSave();
                break;
            case TitleScreenEvent::OpenResort3DTestRequested:
                overworld3d_test_.resetForNextLaunch();
                title_screen_.prepareForOverworld3D();
                active_screen_ = ActiveScreen::Overworld3DTest;
                break;
            case TitleScreenEvent::OpenTestAttendRequested:
                attend_return_target_ = AttendReturnTarget::TitleResortMenu;
                active_screen_ = ActiveScreen::TestAttend;
                break;
            case TitleScreenEvent::OpenResortLoadingRequested:
                loading_return_target_ = LoadingReturnTarget::ResortTitle;
                loading_.beginResortTransfer();
                active_screen_ = ActiveScreen::ResortLoading;
                break;
            case TitleScreenEvent::OpenTradeLoadingRequested:
                loading_return_target_ = LoadingReturnTarget::TradeTitle;
                loading_.beginTradeDemo();
                active_screen_ = ActiveScreen::ResortLoading;
                break;
            case TitleScreenEvent::OpenTransferRequested:
                transfer_flow_.beginTicketScan();
                active_screen_ = ActiveScreen::TransferFlow;
                break;
        }
    }
}

} // namespace pr
