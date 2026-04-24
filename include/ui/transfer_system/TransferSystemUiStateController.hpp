#pragma once

#include "core/Types.hpp"

#include <cmath>

namespace pr::transfer_system {

class TransferSystemUiStateController {
public:
    void configure(double fade_in_seconds, double fade_out_seconds);
    void enter();
    void update(double dt, const GameTransferPillToggleStyle& pill_style, const GameTransferToolCarouselStyle& carousel_style);

    void togglePillTarget();
    void cycleToolCarousel(int dir, const GameTransferToolCarouselStyle& carousel_style);
    bool carouselSlideAnimating() const;
    void startExit();

    void requestButtonSfx() { play_button_sfx_requested_ = true; }
    void requestUiMoveSfx() { play_ui_move_sfx_requested_ = true; }

    bool consumeButtonSfxRequest();
    bool consumeUiMoveSfxRequest();
    bool consumeReturnToTicketListRequest();

    int selectedToolIndex() const { return selected_tool_index_; }
    double sliderT() const { return slider_t_; }
    double panelsReveal() const { return panels_reveal_; }
    double carouselSlideOffsetX() const { return carousel_slide_offset_x_; }
    double carouselSlideTargetX() const { return carousel_slide_target_x_; }
    double uiEnter() const { return ui_enter_; }
    double bottomBannerReveal() const { return bottom_banner_reveal_; }
    bool exitInProgress() const { return exit_in_progress_; }
    double fadeInSeconds() const { return fade_in_seconds_; }
    double fadeOutSeconds() const { return fade_out_seconds_; }
    double elapsedSeconds() const { return elapsed_seconds_; }
    double exitFadeSeconds() const { return exit_fade_seconds_; }

private:
    static void approachExponential(double& value, double target, double dt, double lambda);
    static int carouselSpanPixels(const GameTransferToolCarouselStyle& carousel_style, int dir);

    int selected_tool_index_ = 1;
    double slider_t_ = 0.0;
    double slider_target_ = 0.0;
    double panels_reveal_ = 0.0;
    double panels_target_ = 1.0;
    double carousel_slide_offset_x_ = 0.0;
    double carousel_slide_target_x_ = 0.0;
    double ui_enter_ = 0.0;
    double ui_enter_target_ = 1.0;
    double bottom_banner_reveal_ = 0.0;
    double bottom_banner_target_ = 1.0;
    double elapsed_seconds_ = 0.0;
    double fade_in_seconds_ = 0.0;
    double fade_out_seconds_ = 0.12;
    double exit_fade_seconds_ = 0.0;
    bool exit_in_progress_ = false;
    bool play_button_sfx_requested_ = false;
    bool play_ui_move_sfx_requested_ = false;
    bool return_to_ticket_list_requested_ = false;
};

} // namespace pr::transfer_system
