#include "ui/transfer_system/TransferSystemUiStateController.hpp"

#include <algorithm>

namespace pr::transfer_system {

void TransferSystemUiStateController::configure(double fade_in_seconds, double fade_out_seconds) {
    fade_in_seconds_ = std::max(0.0, fade_in_seconds);
    fade_out_seconds_ = std::max(0.0, fade_out_seconds);
}

void TransferSystemUiStateController::enter() {
    selected_tool_index_ = 1;
    slider_t_ = 0.0;
    slider_target_ = 0.0;
    panels_reveal_ = 0.0;
    panels_target_ = 1.0;
    carousel_slide_offset_x_ = 0.0;
    carousel_slide_target_x_ = 0.0;
    ui_enter_ = 0.0;
    ui_enter_target_ = 1.0;
    bottom_banner_reveal_ = 0.0;
    bottom_banner_target_ = 1.0;
    elapsed_seconds_ = 0.0;
    exit_fade_seconds_ = 0.0;
    exit_in_progress_ = false;
    play_button_sfx_requested_ = false;
    play_ui_move_sfx_requested_ = false;
    return_to_ticket_list_requested_ = false;
}

void TransferSystemUiStateController::update(
    double dt,
    const GameTransferPillToggleStyle& pill_style,
    const GameTransferToolCarouselStyle& carousel_style) {
    elapsed_seconds_ += dt;

    approachExponential(slider_t_, slider_target_, dt, pill_style.toggle_smoothing);
    approachExponential(panels_reveal_, panels_target_, dt, pill_style.box_smoothing);

    constexpr double kEnterSmoothing = 14.0;
    constexpr double kBannerSmoothing = 12.0;
    approachExponential(ui_enter_, ui_enter_target_, dt, kEnterSmoothing);
    approachExponential(bottom_banner_reveal_, bottom_banner_target_, dt, kBannerSmoothing);

    if (std::fabs(carousel_slide_target_x_) >= 1e-9) {
        const double lambda = std::max(1.0, carousel_style.slide_smoothing);
        approachExponential(carousel_slide_offset_x_, carousel_slide_target_x_, dt, lambda);
        const double target = carousel_slide_target_x_;
        if (std::fabs(carousel_slide_offset_x_ - target) < 0.75) {
            if (target < 0.0) {
                selected_tool_index_ = (selected_tool_index_ + 1) % 4;
            } else {
                selected_tool_index_ = (selected_tool_index_ + 3) % 4;
            }
            carousel_slide_offset_x_ = 0.0;
            carousel_slide_target_x_ = 0.0;
        }
    } else if (std::fabs(carousel_slide_offset_x_) < 1e-4) {
        carousel_slide_offset_x_ = 0.0;
    }

    if (exit_in_progress_) {
        exit_fade_seconds_ += dt;
        const bool ui_gone = ui_enter_ < 0.02 && bottom_banner_reveal_ < 0.02 && panels_reveal_ < 0.02;
        const bool fade_done = (fade_out_seconds_ <= 1e-6) || (exit_fade_seconds_ >= fade_out_seconds_);
        if (ui_gone && fade_done && !return_to_ticket_list_requested_) {
            return_to_ticket_list_requested_ = true;
        }
    }
}

void TransferSystemUiStateController::togglePillTarget() {
    if (slider_target_ < 0.5) {
        slider_target_ = 1.0;
        panels_target_ = 0.0;
    } else {
        slider_target_ = 0.0;
        panels_target_ = 1.0;
    }
    requestButtonSfx();
}

void TransferSystemUiStateController::cycleToolCarousel(int dir, const GameTransferToolCarouselStyle& carousel_style) {
    if (dir == 0 || carouselSlideAnimating()) {
        return;
    }
    const int span = carouselSpanPixels(carousel_style, dir);
    if (span <= 0) {
        return;
    }
    carousel_slide_target_x_ = dir > 0 ? -static_cast<double>(span) : static_cast<double>(span);
    requestButtonSfx();
}

bool TransferSystemUiStateController::carouselSlideAnimating() const {
    return std::fabs(carousel_slide_target_x_) > 1e-4 || std::fabs(carousel_slide_offset_x_) > 1e-3;
}

void TransferSystemUiStateController::startExit() {
    exit_in_progress_ = true;
    ui_enter_target_ = 0.0;
    bottom_banner_target_ = 0.0;
    panels_target_ = 0.0;
}

bool TransferSystemUiStateController::consumeButtonSfxRequest() {
    const bool requested = play_button_sfx_requested_;
    play_button_sfx_requested_ = false;
    return requested;
}

bool TransferSystemUiStateController::consumeUiMoveSfxRequest() {
    const bool requested = play_ui_move_sfx_requested_;
    play_ui_move_sfx_requested_ = false;
    return requested;
}

bool TransferSystemUiStateController::consumeReturnToTicketListRequest() {
    const bool requested = return_to_ticket_list_requested_;
    return_to_ticket_list_requested_ = false;
    return requested;
}

void TransferSystemUiStateController::approachExponential(double& value, double target, double dt, double lambda) {
    const double weight = 1.0 - std::exp(-std::max(0.0, lambda) * std::max(0.0, dt));
    value += (target - value) * weight;
}

int TransferSystemUiStateController::carouselSpanPixels(
    const GameTransferToolCarouselStyle& carousel_style,
    int dir) {
    if (carousel_style.slide_span_pixels > 0) {
        return carousel_style.slide_span_pixels;
    }
    if (carousel_style.belt_spacing_pixels > 0) {
        return carousel_style.belt_spacing_pixels;
    }
    if (dir > 0) {
        return carousel_style.slot_center_right - carousel_style.slot_center_middle;
    }
    return carousel_style.slot_center_middle - carousel_style.slot_center_left;
}

} // namespace pr::transfer_system
