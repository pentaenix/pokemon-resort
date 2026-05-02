#include "core/app/screen/AppScreenCoordinator.hpp"

#include "ui/Screen.hpp"
#include "ui/ScreenInput.hpp"
#include "ui/TitleScreen.hpp"
#include "ui/TransferFlowCoordinator.hpp"

namespace pr {

AppScreenCoordinator::AppScreenCoordinator(
    TitleScreen& title_screen,
    AppLoadingCoordinator& loading,
    TransferFlowCoordinator& transfer_flow)
    : title_screen_(title_screen),
      loading_(loading),
      transfer_flow_(transfer_flow) {}

Screen* AppScreenCoordinator::activeScreen() {
    switch (active_screen_) {
        case ActiveScreen::Title:
            return &title_screen_;
        case ActiveScreen::ResortLoading:
            return loading_.activeScreen();
        case ActiveScreen::TransferFlow:
            return transfer_flow_.activeScreen();
    }
    return nullptr;
}

ScreenInput* AppScreenCoordinator::activeInput() {
    if (transition_controller_.blocksInput()) {
        return nullptr;
    }
    return activeScreen();
}

void AppScreenCoordinator::update(double dt) {
    switch (active_screen_) {
        case ActiveScreen::Title:
            updateTitle(dt);
            break;
        case ActiveScreen::ResortLoading:
            updateLoading(dt);
            break;
        case ActiveScreen::TransferFlow:
            updateTransfer(dt);
            break;
    }

    updateTransition(dt);
}

AppMusicRequest AppScreenCoordinator::musicRequest() const {
    const bool transition_returns_to_transfer =
        transition_controller_.active() &&
        loading_return_target_ == LoadingReturnTarget::TransferTickets;
    return AppMusicRequest{
        active_screen_ == ActiveScreen::Title && title_screen_.wantsMenuMusic(),
        (active_screen_ == ActiveScreen::TransferFlow || transition_returns_to_transfer) &&
            transfer_flow_.hasTransferMusic(),
        transfer_flow_.musicPath(),
        transfer_flow_.musicSilenceSeconds(),
        transfer_flow_.musicFadeInSeconds(),
        title_screen_.musicVolume()};
}

AppSfxRequests AppScreenCoordinator::consumeSfxRequests() {
    collectTransferFrameRequests();
    return frame_requests_.consumeSfxRequests();
}

std::optional<UserSettings> AppScreenCoordinator::consumeUserSettingsSaveRequest() {
    if (!frame_requests_.consumeUserSettingsSaveRequest()) {
        return std::nullopt;
    }
    return title_screen_.currentUserSettings();
}

float AppScreenCoordinator::sfxVolume() const {
    return title_screen_.sfxVolume();
}

double AppScreenCoordinator::transitionOverlayAlpha() const {
    return transition_controller_.overlayAlpha();
}

} // namespace pr
