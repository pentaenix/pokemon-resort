#include "core/app/screen/AppScreenCoordinator.hpp"

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
