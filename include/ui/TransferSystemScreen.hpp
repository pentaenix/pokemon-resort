#pragma once

#include "core/Assets.hpp"
#include "core/Types.hpp"
#include "core/Font.hpp"
#include "ui/BoxViewport.hpp"
#include "ui/ScreenInput.hpp"
#include "ui/TransferSaveSelection.hpp"

#include <SDL.h>
#include <array>
#include <memory>
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
    bool consumeReturnToTicketListRequest();

private:
    void requestReturnToTicketList();
    void drawBackground(SDL_Renderer* renderer) const;
    void updateAnimations(double dt);
    void updateEnterExit(double dt);
    void updateCarouselSlide(double dt);
    void drawPillToggle(SDL_Renderer* renderer) const;
    bool hitTestPillTrack(int logical_x, int logical_y) const;
    void togglePillTarget();
    void cachePillLabelTextures(SDL_Renderer* renderer);
    void syncBoxViewportPositions();
    void drawToolCarousel(SDL_Renderer* renderer) const;
    void drawBottomBanner(SDL_Renderer* renderer) const;
    bool hitTestToolCarousel(int logical_x, int logical_y) const;
    /// Cycles selection: `dir` −1 = previous tool, +1 = next (infinite wrap).
    void cycleToolCarousel(int dir);
    bool carouselSlideAnimating() const;
    Color carouselFrameColorForIndex(int tool_index) const;
    int carouselScreenY() const;

    struct BackgroundAnimation {
        bool enabled = false;
        double scale = 1.0;
        double speed_x = 0.0;
        double speed_y = 0.0;
    };

    WindowConfig window_config_;
    std::string project_root_;
    std::string font_path_;
    TextureHandle background_;
    BackgroundAnimation background_animation_;
    std::unique_ptr<BoxViewport> resort_box_viewport_;
    std::unique_ptr<BoxViewport> game_save_box_viewport_;
    GameTransferPillToggleStyle pill_style_;
    GameTransferToolCarouselStyle carousel_style_;
    std::array<TextureHandle, 4> tool_icons_{};
    /// 0 = multiple, 1 = basic, 2 = swap, 3 = items.
    int selected_tool_index_ = 1;
    FontHandle pill_font_;
    TextureHandle pill_label_pokemon_black_;
    TextureHandle pill_label_items_black_;
    TextureHandle pill_label_pokemon_white_;
    TextureHandle pill_label_items_white_;

    double elapsed_seconds_ = 0.0;
    double fade_in_seconds_ = 0.0;
    double fade_out_seconds_ = 0.12;
    double exit_fade_seconds_ = 0.0;
    bool play_button_sfx_requested_ = false;
    bool return_to_ticket_list_requested_ = false;

    /// 0 = Pokémon (left), 1 = Items (right).
    double slider_t_ = 0.0;
    double slider_target_ = 0.0;
    /// 0 = boxes off-screen, 1 = boxes at rest.
    double panels_reveal_ = 0.0;
    double panels_target_ = 1.0;
    /// Horizontal slide of tool strip (px); commits `selected_tool_index_` when motion finishes.
    double carousel_slide_offset_x_ = 0.0;
    double carousel_slide_target_x_ = 0.0;
    /// 0 = UI off-screen, 1 = at rest (first-enter animation, and used on exit).
    double ui_enter_ = 0.0;
    double ui_enter_target_ = 1.0;
    /// 0 = bottom banner hidden below screen, 1 = at rest.
    double bottom_banner_reveal_ = 0.0;
    double bottom_banner_target_ = 1.0;
    bool exit_in_progress_ = false;
};

} // namespace pr
