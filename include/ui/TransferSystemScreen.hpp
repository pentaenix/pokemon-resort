#pragma once

#include "core/Assets.hpp"
#include "core/Types.hpp"
#include "ui/ScreenInput.hpp"
#include "ui/TransferSaveSelection.hpp"

#include <SDL.h>
#include <string>

namespace pr {

class TransferSystemScreen : public ScreenInput {
public:
    TransferSystemScreen(
        SDL_Renderer* renderer,
        const WindowConfig& window_config,
        const std::string& font_path,
        const std::string& project_root);

    void enter(const TransferSaveSelection& selection, SDL_Renderer* renderer);
    void update(double dt);
    void render(SDL_Renderer* renderer) const;

    void onAdvancePressed() override;
    void onBackPressed() override;
    bool handlePointerPressed(int logical_x, int logical_y) override;

    bool consumeButtonSfxRequest();
    bool consumeRestartGameRequest();

private:
    void loadTransferSystemConfig();
    void requestRestart();
    void drawBackground(SDL_Renderer* renderer) const;

    struct BackgroundAnimation {
        bool enabled = false;
        double scale = 1.0;
        double speed_x = 0.0;
        double speed_y = 0.0;
    };

    WindowConfig window_config_;
    std::string project_root_;
    TextureHandle background_;
    BackgroundAnimation background_animation_;
    double elapsed_seconds_ = 0.0;
    bool play_button_sfx_requested_ = false;
    bool restart_game_requested_ = false;
};

} // namespace pr
