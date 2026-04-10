#pragma once

#include "core/Assets.hpp"
#include "core/Types.hpp"

#include <SDL.h>
#include <map>
#include <memory>
#include <string>

namespace pr {

class TransferTicketSandboxScreen {
public:
    TransferTicketSandboxScreen(
        SDL_Renderer* renderer,
        const WindowConfig& window_config,
        const std::string& font_path,
        const std::string& project_root);

    void enter();
    void update(double dt);
    void render(SDL_Renderer* renderer) const;

    void onNavigate(int delta);
    void onAdvancePressed();
    void onBackPressed();
    void handlePointerMoved(int logical_x, int logical_y);
    bool handlePointerPressed(int logical_x, int logical_y);
    bool handlePointerReleased(int logical_x, int logical_y);

    bool consumeButtonSfxRequest();
    bool consumeReturnToMainMenuRequest();

private:
    struct TicketAssets {
        TextureHandle background;
        TextureHandle banner;
        TextureHandle backdrop;
        TextureHandle main_left;
        TextureHandle sub_right;
        TextureHandle color_left;
        TextureHandle color_right;
        TextureHandle game_icon_back;
        TextureHandle game_icon_front;
        TextureHandle icon_boat;
        TextureHandle party_sprite;
    };

    struct TicketFonts {
        FontHandle banner_title;
        FontHandle banner_subtitle;
        FontHandle title;
        FontHandle trainer;
        FontHandle data_label;
        FontHandle data_value;
        FontHandle boarding_pass;
        FontHandle footer_hint;
    };

    struct TicketText {
        TextureHandle banner_title;
        TextureHandle banner_subtitle;
        TextureHandle game_title;
        TextureHandle trainer_name;
        TextureHandle time_label;
        TextureHandle time_value;
        TextureHandle badges_label;
        TextureHandle badges_value;
        TextureHandle pokedex_label;
        TextureHandle pokedex_value;
        TextureHandle boarding_pass;
        TextureHandle footer_hint;
    };

    void requestButtonSfx();
    void loadPaletteConfig();
    void buildTextTextures(SDL_Renderer* renderer);
    Color colorForGame(const std::string& key, const Color& fallback) const;

    void drawTextureTopLeft(SDL_Renderer* renderer, const TextureHandle& texture, int x, int y) const;
    void drawTextureTopLeftTinted(SDL_Renderer* renderer, const TextureHandle& texture, int x, int y, const Color& tint) const;
    void drawTextureCentered(SDL_Renderer* renderer, const TextureHandle& texture, int x, int y) const;
    void drawTextureCenteredTinted(SDL_Renderer* renderer, const TextureHandle& texture, int x, int y, const Color& tint) const;
    void drawTextureCenteredScaled(
        SDL_Renderer* renderer,
        const TextureHandle& texture,
        int x,
        int y,
        double scale) const;
    void drawTextureTopLeftRotated(
        SDL_Renderer* renderer,
        const TextureHandle& texture,
        int x,
        int y,
        double angle_degrees) const;
    void renderTicket(SDL_Renderer* renderer) const;

    double scaleX() const;
    double scaleY() const;
    int sx(int value) const;
    int sy(int value) const;

    WindowConfig window_config_;
    std::string font_path_;
    std::string project_root_;
    TicketAssets assets_;
    TicketFonts fonts_;
    TicketText text_;
    std::map<std::string, Color> game_palette_;
    Color active_game_color_{179, 58, 50, 255};
    int right_stub_offset_x_ = 0;
    bool play_button_sfx_requested_ = false;
    bool return_to_main_menu_requested_ = false;
};

} // namespace pr
