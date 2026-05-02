#pragma once

#include "core/Types.hpp"
#include "core/app/audio/AppAudioDirector.hpp"
#include "core/app/frame/AppFrameRequests.hpp"
#include "core/app/loading/AppLoadingCoordinator.hpp"
#include "core/app/transition/AppTransitionController.hpp"

#include <optional>

namespace pr {

class Screen;
class ScreenInput;
class TitleScreen;
class TransferFlowCoordinator;

class AppScreenCoordinator {
public:
    AppScreenCoordinator(
        TitleScreen& title_screen,
        AppLoadingCoordinator& loading,
        TransferFlowCoordinator& transfer_flow);

    Screen* activeScreen();
    ScreenInput* activeInput();
    void update(double dt);

    AppMusicRequest musicRequest() const;
    AppSfxRequests consumeSfxRequests();
    std::optional<UserSettings> consumeUserSettingsSaveRequest();
    float sfxVolume() const;
    double transitionOverlayAlpha() const;

private:
    enum class ActiveScreen {
        Title,
        ResortLoading,
        TransferFlow
    };

    enum class LoadingReturnTarget {
        ResortTitle,
        TradeTitle,
        TransferTickets
    };

    void updateTitle(double dt);
    void updateLoading(double dt);
    void updateTransfer(double dt);
    void updateTransition(double dt);
    void startSuccessfulSaveQuickTransition();
    void beginSuccessfulSaveLoadingScreen();
    void finishLoadingTransition();
    void collectTransferFrameRequests();

    TitleScreen& title_screen_;
    AppLoadingCoordinator& loading_;
    TransferFlowCoordinator& transfer_flow_;
    AppFrameRequests frame_requests_;
    AppTransitionController transition_controller_;
    ActiveScreen active_screen_ = ActiveScreen::Title;
    LoadingReturnTarget loading_return_target_ = LoadingReturnTarget::ResortTitle;
};

} // namespace pr
