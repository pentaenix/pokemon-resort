#pragma once

#include "core/PokeSpriteAssets.hpp"
#include "core/SaveLibrary.hpp"
#include "core/Types.hpp"
#include "ui/LoadingScreen.hpp"
#include "ui/Screen.hpp"
#include "ui/TransferSaveSelection.hpp"
#include "ui/TransferSystemScreen.hpp"
#include "ui/TransferTicketScreen.hpp"
#include "ui/transfer_flow/TransferFlowController.hpp"

#include <SDL.h>
#include <future>
#include <memory>
#include <optional>
#include <string>

namespace pr {

class TransferFlowCoordinator {
public:
    using ScreenKind = transfer_flow::ScreenKind;

    TransferFlowCoordinator(
        SDL_Renderer* renderer,
        WindowConfig window_config,
        std::string font_path,
        std::string project_root,
        std::shared_ptr<PokeSpriteAssets> sprite_assets,
        SaveLibrary& save_library,
        const char* argv0);

    void beginTicketScan();
    void update(double dt);

    Screen* activeScreen();
    ScreenKind activeScreenKind() const { return flow_controller_.activeScreenKind(); }
    bool isActive() const { return flow_controller_.isActive(); }

    bool consumeReturnToTitleRequest();

    bool hasTransferMusic() const;
    const std::string& musicPath() const;
    double musicSilenceSeconds() const;
    double musicFadeInSeconds() const;

    bool consumeButtonSfxRequest();
    bool consumeRipSfxRequest();
    bool consumeUiMoveSfxRequest();

private:
    void ensureLoadingScreen();
    void ensureTicketScreen();
    void ensureTransferSystemScreen();
    void updateLoading();
    void updateTicketList(double dt);
    void updateTransferSystem(double dt);
    void enterTransferSystem(TransferSaveSelection selection, int initial_box_index);

    SDL_Renderer* renderer_ = nullptr;
    WindowConfig window_config_;
    std::string font_path_;
    std::string project_root_;
    std::shared_ptr<PokeSpriteAssets> sprite_assets_;
    SaveLibrary& save_library_;
    const char* argv0_ = nullptr;

    transfer_flow::TransferFlowController flow_controller_;
    std::future<void> transfer_load_future_;
    std::future<std::optional<TransferSaveSummary>> transfer_detail_future_;

    std::unique_ptr<LoadingScreen> loading_screen_;
    std::unique_ptr<TransferTicketScreen> transfer_ticket_;
    std::unique_ptr<TransferSystemScreen> transfer_system_screen_;
};

} // namespace pr
