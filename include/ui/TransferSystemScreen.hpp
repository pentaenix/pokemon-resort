#pragma once

#include "core/Assets.hpp"
#include "core/Types.hpp"
#include "ui/ScreenInput.hpp"
#include "ui/TransferSaveSelection.hpp"

#include <SDL.h>
#include <string>
#include <vector>

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
    void rebuildText(SDL_Renderer* renderer);
    void loadTransferSystemConfig();
    void requestRestart();
    void drawTextureTopLeft(SDL_Renderer* renderer, const TextureHandle& texture, int x, int y) const;
    void drawTextureCentered(SDL_Renderer* renderer, const TextureHandle& texture, int x, int y) const;
    bool pointInRect(int x, int y, const SDL_Rect& rect) const;
    SDL_Rect backButtonRect() const;

    double scaleX() const;
    double scaleY() const;
    int sx(int value) const;
    int sy(int value) const;

    WindowConfig window_config_;
    std::string font_path_;
    std::string project_root_;
    FontHandle title_font_;
    FontHandle body_font_;
    FontHandle button_font_;
    TransferSaveSelection selection_;
    TextureHandle title_text_;
    TextureHandle subtitle_text_;
    std::vector<TextureHandle> detail_textures_;
    TextureHandle back_text_;
    double fade_in_seconds_ = 0.12;
    double elapsed_seconds_ = 0.0;
    bool play_button_sfx_requested_ = false;
    bool restart_game_requested_ = false;
};

} // namespace pr
