#pragma once

#include "core/Types.hpp"
#include "ui/loading/LoadingScreenBase.hpp"
#include "ui/loading/ResortTransferLoadingRenderer.hpp"

#include <SDL.h>

#include <memory>
#include <string>

namespace pr {

enum class ResortTransferLoadingState {
    WhiteIdle,
    Intro,
    LoadingLoop,
    Outro,
    QuickPass
};

class ResortTransferLoadingScreen : public LoadingScreenBase {
public:
    ResortTransferLoadingScreen(
        SDL_Renderer* renderer,
        WindowConfig window_config,
        std::string project_root,
        LoadingScreenType loading_screen_type = LoadingScreenType::ResortTransfer);

    LoadingScreenType loadingScreenType() const override { return loading_screen_type_; }
    void setLoadingMessageKey(const std::string& message_key) override;
    void setMinimumLoopSeconds(double minimum_loop_seconds) override;
    void enter() override;
    void beginLoadingWithMessageKey(const std::string& message_key, double minimum_loop_seconds = -1.0) override;
    void beginQuickPass(bool wait_for_completion = false) override;
    void update(double dt) override;
    void render(SDL_Renderer* renderer) override;

    void onAdvancePressed() override;
    void onBackPressed() override;
    bool handlePointerPressed(int logical_x, int logical_y) override;
    bool acceptsAdvanceInput() const override;
    void markLoadingComplete() override;
    bool isLoadingAnimationComplete() const override;
    bool consumeReturnToMenuRequest() override;

#ifdef PR_ENABLE_TEST_HOOKS
    ResortTransferLoadingState debugState() const;
#endif

private:
    void rebuildRenderer();
    void refreshMessageTexture();
    void changeState(ResortTransferLoadingState next);
    ResortTransferLoadingFrame buildFrame(SDL_Renderer* renderer) const;
    double boatCenterXForProgress(double enter_progress, double exit_progress, int viewport_w) const;
    double quickPassBoatCenterX(double progress, int viewport_w) const;
    double quickPassExitProgress(double start_fraction, double duration_fraction) const;
    double introDuration() const;
    double outroDuration() const;

    WindowConfig window_config_;
    SDL_Renderer* sdl_renderer_ = nullptr;
    std::string project_root_;
    LoadingScreenType loading_screen_type_ = LoadingScreenType::ResortTransfer;
    ResortTransferLoadingConfig config_;
    ResortTransferLoadingTextures textures_;
    std::unique_ptr<ResortTransferLoadingRenderer> renderer_;
    std::string message_key_;
    ResortTransferLoadingState state_ = ResortTransferLoadingState::WhiteIdle;
    double state_time_ = 0.0;
    double loop_elapsed_seconds_ = 0.0;
    double minimum_loop_seconds_ = 0.0;
    double loop_time_ = 0.0;
    double cloud_drift_ = 0.0;
    double foam_phase_ = 0.0;
    double speed_crest_phase_ = 0.0;
    double previous_boat_x_ = 0.0;
    double boat_velocity_x_ = 0.0;
    double quick_pass_completion_time_ = 0.0;
    bool has_previous_boat_x_ = false;
    bool return_to_menu_requested_ = false;
    bool auto_flow_ = false;
    bool loading_complete_requested_ = false;
    bool loading_animation_complete_ = false;
    bool suppress_message_ = false;
};

} // namespace pr
