#pragma once

#include "core/Types.hpp"
#include "core/app/audio/AppAudioDirector.hpp"
#include "core/app/frame/AppFrameRequests.hpp"
#include "core/app/loading/AppLoadingCoordinator.hpp"
#include "core/app/transition/AppTransitionController.hpp"

#include <future>
#include <optional>

namespace pr {

class Screen;
class ScreenInput;
class TitleScreen;
class TransferFlowCoordinator;
class Overworld3DTestScreen;

class AppScreenCoordinator {
public:
    AppScreenCoordinator(
        TitleScreen& title_screen,
        AppLoadingCoordinator& loading,
        TransferFlowCoordinator& transfer_flow,
        Overworld3DTestScreen& overworld3d_test);

    Screen* activeScreen();
    ScreenInput* activeInput();
    void update(double dt);

    AppMusicRequest musicRequest() const;
    AppSfxRequests consumeSfxRequests();
    std::optional<UserSettings> consumeUserSettingsSaveRequest();
    float sfxVolume() const;
    double transitionOverlayAlpha() const;
    std::string screenshotNameContext() const;

private:
    enum class ActiveScreen {
        Title,
        ResortLoading,
        TransferFlow,
        Overworld3DTest
    };

    enum class LoadingReturnTarget {
        ResortTitle,
        TradeTitle,
        TransferTickets
    };

    void updateTitle(double dt);
    void updateLoading(double dt);
    void updateTransfer(double dt);
    void updateOverworld3D(double dt);
    void updateTransition(double dt);
    void startSuccessfulSaveQuickTransition();
    void beginSuccessfulSaveLoadingScreen();
    void finishLoadingTransition();
    void collectTransferFrameRequests();

    TitleScreen& title_screen_;
    AppLoadingCoordinator& loading_;
    TransferFlowCoordinator& transfer_flow_;
    Overworld3DTestScreen& overworld3d_test_;
    AppFrameRequests frame_requests_;
    AppTransitionController transition_controller_;
    ActiveScreen active_screen_ = ActiveScreen::Title;
    LoadingReturnTarget loading_return_target_ = LoadingReturnTarget::ResortTitle;
    /// First `updateLoading` after `beginSuccessfulSaveLoadingScreen` runs deferred Save+Exit IO and unblocks the boat.
    bool pending_successful_save_quick_pass_work_ = false;
    int successful_save_deferred_io_frames_remaining_ = 0;
    bool deferred_successful_save_exit_failed_ = false;
    /// `runDeferredSaveForSuccessfulExit` on a worker; main thread polls so the boat keeps animating.
    std::optional<std::future<bool>> successful_save_async_io_;
};

} // namespace pr
