#pragma once

#include "core/Types.hpp"
#include "ui/loading/LoadingScreenBase.hpp"
#include "ui/loading/ResortTransferLoadingConfig.hpp"

#include <memory>
#include <string>

struct SDL_Renderer;

namespace pr {

class AppLoadingCoordinator {
public:
    AppLoadingCoordinator(
        SDL_Renderer* renderer,
        WindowConfig window_config,
        std::string font_path,
        std::string project_root);

    LoadingScreenBase* activeScreen();
    void beginResortTransfer();
    void beginTradeDemo();
    void beginSuccessfulSaveQuickPass(const std::string& message_key = {});
    void update(double dt);

    bool consumeReturnToMenuRequest();
    bool isLoadingAnimationComplete() const;

private:
    SDL_Renderer* renderer_ = nullptr;
    WindowConfig window_config_;
    std::string font_path_;
    std::string project_root_;
    ResortTransferLoadingConfig config_;

    std::unique_ptr<LoadingScreenBase> resort_transfer_screen_;
    std::unique_ptr<LoadingScreenBase> pokeball_screen_;
    std::unique_ptr<LoadingScreenBase> quick_boat_pass_screen_;
    LoadingScreenBase* active_screen_ = nullptr;

    TemporalLoadingDemoType temporal_loading_type_ = TemporalLoadingDemoType::QuickBoatPass;
    double temporal_simulated_load_duration_seconds_ = 0.0;
    double temporal_loading_elapsed_seconds_ = 0.0;
    bool temporal_loading_completion_sent_ = false;
};

} // namespace pr
