#include "core/app/screen/AppScreenCoordinator.hpp"

#include "ui/AttendTestScreen.hpp"
#include "ui/Screen.hpp"
#include "ui/ScreenInput.hpp"
#include "ui/Overworld3DTestScreen.hpp"
#include "ui/TitleScreen.hpp"
#include "ui/TransferFlowCoordinator.hpp"

namespace pr {

AppScreenCoordinator::AppScreenCoordinator(
    TitleScreen& title_screen,
    AppLoadingCoordinator& loading,
    TransferFlowCoordinator& transfer_flow,
    Overworld3DTestScreen& overworld3d_test,
    AttendTestScreen& attend_test)
    : title_screen_(title_screen),
      loading_(loading),
      transfer_flow_(transfer_flow),
      overworld3d_test_(overworld3d_test),
      attend_test_(attend_test) {}

Screen* AppScreenCoordinator::activeScreen() {
    switch (active_screen_) {
        case ActiveScreen::Title:
            return &title_screen_;
        case ActiveScreen::ResortLoading:
            return loading_.activeScreen();
        case ActiveScreen::TransferFlow:
            return transfer_flow_.activeScreen();
        case ActiveScreen::Overworld3DTest:
            return &overworld3d_test_;
        case ActiveScreen::TestAttend:
            return &attend_test_;
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
        case ActiveScreen::Overworld3DTest:
            updateOverworld3D(dt);
            break;
        case ActiveScreen::TestAttend:
            updateTestAttend(dt);
            break;
    }

    updateTransition(dt);
}

AppMusicRequest AppScreenCoordinator::musicRequest() const {
    const bool transition_returns_to_transfer =
        transition_controller_.active() &&
        loading_return_target_ == LoadingReturnTarget::TransferTickets;
    const bool aquarium_context = active_screen_ == ActiveScreen::Overworld3DTest ||
        (active_screen_ == ActiveScreen::TestAttend &&
         attend_return_target_ == AttendReturnTarget::Overworld3D);
    AppMusicRequest request;
    request.menu_requested =
        active_screen_ == ActiveScreen::Title && title_screen_.wantsMenuMusic();
    request.transfer_requested =
        (active_screen_ == ActiveScreen::TransferFlow || transition_returns_to_transfer) &&
        transfer_flow_.hasTransferMusic();
    request.transfer_music_path = transfer_flow_.musicPath();
    request.transfer_silence_seconds = transfer_flow_.musicSilenceSeconds();
    request.transfer_fade_in_seconds = transfer_flow_.musicFadeInSeconds();
    request.aquarium_requested = aquarium_context && overworld3d_test_.wantsAquariumMusic();
    request.aquarium_music_path = overworld3d_test_.aquariumMusicPath();
    request.volume = title_screen_.musicVolume();
    return request;
}

AppSfxRequests AppScreenCoordinator::consumeSfxRequests() {
    collectTransferFrameRequests();
    collectOverworldFrameRequests();
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

std::string AppScreenCoordinator::screenshotNameContext() const {
    switch (active_screen_) {
        case ActiveScreen::Title:
            return "title_screen";
        case ActiveScreen::ResortLoading:
            return "resort_loading";
        case ActiveScreen::TransferFlow: {
            if (transfer_flow_.activeScreenKind() == TransferFlowCoordinator::ScreenKind::TransferSystem) {
                std::string game = transfer_flow_.activeGameKeyForScreenshot();
                const std::string prefix = "pokemon_";
                if (game.rfind(prefix, 0) == 0) {
                    game = game.substr(prefix.size());
                }
                return game.empty() ? "game_transfer" : ("game_transfer_" + game);
            }
            if (transfer_flow_.activeScreenKind() == TransferFlowCoordinator::ScreenKind::TicketList) {
                return "transfer_tickets";
            }
            return "transfer_loading";
        }
        case ActiveScreen::Overworld3DTest:
            return "overworld3d_test";
        case ActiveScreen::TestAttend:
            return "test_attend";
    }
    return "screen";
}

} // namespace pr
