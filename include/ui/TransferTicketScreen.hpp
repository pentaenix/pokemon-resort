#pragma once

#include "core/assets/Assets.hpp"
#include "core/assets/PokeSpriteAssets.hpp"
#include "core/Types.hpp"
#include "ui/Screen.hpp"
#include "ui/TransferSaveSelection.hpp"
#include "ui/transfer_ticket/TransferTicketListController.hpp"

#include <SDL.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pr {

class TransferTicketScreen : public Screen {
public:
    TransferTicketScreen(
        SDL_Renderer* renderer,
        const WindowConfig& window_config,
        const std::string& font_path,
        const std::string& project_root,
        std::shared_ptr<PokeSpriteAssets> sprite_assets);

    void enter();
    void setSaveSelections(SDL_Renderer* renderer, const std::vector<TransferSaveSelection>& selections);
    void update(double dt) override;
    void render(SDL_Renderer* renderer) override;

    bool canNavigate() const override;
    void onNavigate(int delta) override;
    void onAdvancePressed() override;
    void onBackPressed() override;
    void handlePointerMoved(int logical_x, int logical_y) override;
    bool handlePointerPressed(int logical_x, int logical_y) override;
    bool handlePointerReleased(int logical_x, int logical_y) override;

    bool consumeButtonSfxRequest();
    bool consumeRipSfxRequest();
    bool consumeReturnToMainMenuRequest();
    bool consumeOpenTransferSystemRequest(TransferSaveSelection& out_selection);
    /// Call when returning from the game transfer screen so the ticket list restores rip state while keeping selection.
    void prepareReturnFromGameTransferScreen();
    const std::string& musicPath() const;
    double musicSilenceSeconds() const;
    double musicFadeInSeconds() const;

#ifdef PR_ENABLE_TEST_HOOKS
    bool debugTicketGameTitleTextureReady(int index) const {
        return index >= 0 &&
            index < static_cast<int>(tickets_.size()) &&
            tickets_[static_cast<std::size_t>(index)].text.game_title.texture != nullptr;
    }
    bool debugTicketTrainerTextureReady(int index) const {
        return index >= 0 &&
            index < static_cast<int>(tickets_.size()) &&
            tickets_[static_cast<std::size_t>(index)].text.trainer_name.texture != nullptr;
    }
#endif

private:
    struct TicketAssets {
        TextureHandle background;
        TextureHandle banner;
        TextureHandle backdrop;
        TextureHandle stamp;
        TextureHandle main_left;
        TextureHandle main_right;
        TextureHandle color_left;
        TextureHandle color_right;
        TextureHandle game_icon_back;
        TextureHandle game_icon_front;
        TextureHandle icon_boat;
    };

    struct TicketFonts {
        FontHandle banner_title;
        FontHandle banner_subtitle;
        FontHandle title;
        FontHandle trainer;
        FontHandle data_label;
        FontHandle data_value;
        FontHandle boarding_pass;
    };

    struct TicketText {
        TextureHandle game_title;
        TextureHandle trainer_name;
        TextureHandle time_label;
        TextureHandle time_value;
        TextureHandle badges_label;
        TextureHandle badges_value;
        TextureHandle pokedex_label;
        TextureHandle pokedex_value;
        TextureHandle boarding_pass;
    };

    struct ScreenText {
        TextureHandle title;
        TextureHandle subtitle;
    };

    struct TicketEntry {
        TransferSaveSelection data;
        Color game_color{179, 58, 50, 255};
        TicketText text;
        std::vector<TextureHandle> party_sprites;
    };

    struct TicketFontSizes {
        int title = 34;
        int trainer = 22;
        int data_label = 21;
        int data_value = 22;
        int boarding_pass = 17;
    };

    struct TicketLayout {
        Point boarding_pass{6, 108};
        double boarding_pass_angle = -90.0;
        Point game_title{111, 12};
        Point trainer_name{113, 54};
        Point party_start{92, 96};
        int party_count = 6;
        int party_spacing = 42;
        double party_scale = 2.0;
        Point stats_origin{252, 0};
        int stats_label_x = 0;
        int stats_value_x = 2;
        int stats_label_y = 9;
        int stats_label_value_gap = 19;
        int stats_row_gap = 41;
        TicketFontSizes font_sizes;
    };

    struct TicketSelection {
        bool enabled = true;
        Color border_color{244, 205, 72, 255};
        int border_alpha = 230;
        int border_thickness = 2;
        int border_padding = 2;
        int border_radius = 10;
        double beat_speed = 2.2;
        double beat_magnitude = 1.5;
    };

    struct TicketListLayout {
        Point start{45, 167};
        int separation_y = 308;
        SDL_Rect viewport{0, 156, 1280, 615};
        double scroll_speed = 14.0;
    };

    struct ScreenHeader {
        std::string title = "TRANSFER";
        std::string subtitle = "Select a save ticket to begin.";
        Point title_center{640, 42};
        Point subtitle_center{640, 94};
        int title_font_size = 52;
        int subtitle_font_size = 25;
        Color title_color{31, 31, 31, 255};
        Color subtitle_color{94, 94, 94, 255};
    };

    struct TicketRipAnimation {
        bool enabled = true;
        int distance = 28;
        int pre_tug_distance = 3;
        double pre_tug_duration_seconds = 0.08;
        double duration_seconds = 0.35;
        double rotation_degrees = 1.2;
        Point rotation_pivot{360, 12};
    };

    struct SelectionTransition {
        double fade_to_black_seconds = 0.12;
        int fade_to_black_max_alpha = 255;
    };

    struct TransferMusic {
        std::string path = "assets/music/transfer_lobby.mp3";
        double silence_seconds = 1.0;
        double fade_in_seconds = 1.0;
    };

    struct BackgroundAnimation {
        bool enabled = false;
        double scale = 1.0;
        double speed_x = 0.0;
        double speed_y = 0.0;
    };

    void requestButtonSfx();
    void loadTransferConfig();
    void buildTextTextures(SDL_Renderer* renderer, const std::vector<TransferSaveSelection>& selections);
    void buildScreenTextTextures(SDL_Renderer* renderer);
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
    void drawTextureTopLeftRotatedAround(
        SDL_Renderer* renderer,
        const TextureHandle& texture,
        int x,
        int y,
        double angle_degrees,
        int pivot_x,
        int pivot_y) const;
    void drawTextureTopLeftTintedRotatedAround(
        SDL_Renderer* renderer,
        const TextureHandle& texture,
        int x,
        int y,
        const Color& tint,
        double angle_degrees,
        int pivot_x,
        int pivot_y) const;
    void drawBackground(SDL_Renderer* renderer) const;
    void drawSelectionOutline(SDL_Renderer* renderer, int x, int y, int width, int height) const;
    void drawRoundedSelectionRect(SDL_Renderer* renderer, int x, int y, int width, int height, int radius) const;
    void renderTicket(SDL_Renderer* renderer, int index, int x, int y, bool selected) const;
    bool pointInTicket(int logical_x, int logical_y, int index) const;
    int ticketCount() const;

    WindowConfig window_config_;
    std::string font_path_;
    std::string project_root_;
    std::shared_ptr<PokeSpriteAssets> sprite_assets_;
    double fade_in_seconds_ = 0.3;
    double fade_in_elapsed_seconds_ = 0.0;
    TicketAssets assets_;
    TicketFonts fonts_;
    TicketLayout layout_;
    TicketSelection selection_;
    TicketListLayout list_layout_;
    ScreenHeader screen_header_;
    TicketRipAnimation rip_animation_;
    SelectionTransition selection_transition_;
    TransferMusic music_;
    BackgroundAnimation background_animation_;
    ScreenText screen_text_;
    std::vector<TicketEntry> tickets_;
    std::map<std::string, Color> game_palette_;
    double elapsed_seconds_ = 0.0;
    bool play_button_sfx_requested_ = false;
    bool play_rip_sfx_requested_ = false;
    transfer_ticket::TransferTicketListController list_controller_;
};

} // namespace pr
