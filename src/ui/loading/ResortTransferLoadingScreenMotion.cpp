#include "ui/loading/ResortTransferLoadingScreen.hpp"

#include <algorithm>

namespace pr {

namespace {

double foamTailClearance(const ResortTransferLoadingConfig& config, double approximate_boat_width) {
    return std::max(
        0.0,
        static_cast<double>(config.border.inset) +
            approximate_boat_width * (config.foam.total_width_percent_of_boat_width - 0.36) +
            config.foam.back_extension +
            config.foam.trailing_stretch_amount -
            approximate_boat_width * 0.5);
}

} // namespace

double ResortTransferLoadingScreen::boatCenterXForProgress(
    double enter_progress,
    double exit_progress,
    int viewport_w) const {
    const double target_x = static_cast<double>(viewport_w) * config_.boat.center_x_ratio;
    const double boat_width = 843.0 * config_.boat.scale;
    const double enter_x = -boat_width * 0.5 + (target_x + boat_width * 0.5) * enter_progress;
    const double exit_x = target_x +
        (static_cast<double>(viewport_w) + boat_width * 0.5 + foamTailClearance(config_, boat_width) - target_x) *
            exit_progress;
    return exit_progress > 0.0 ? exit_x : enter_x;
}

double ResortTransferLoadingScreen::quickPassBoatCenterX(double progress, int viewport_w) const {
    const double boat_width = 843.0 * config_.boat.scale;
    const double from = -boat_width * 0.5;
    const double to = static_cast<double>(viewport_w) + boat_width * 0.5 + foamTailClearance(config_, boat_width);
    return from + (to - from) * std::clamp(progress, 0.0, 1.0);
}

double ResortTransferLoadingScreen::quickPassExitProgress(
    double start_fraction,
    double duration_fraction) const {
    if (!loading_complete_requested_) {
        return 0.0;
    }

    const double duration = std::max(0.01, config_.quick_pass.duration_seconds);
    const double start_time = std::max(quick_pass_completion_time_, duration * start_fraction);
    const double stage_duration = std::max(0.01, duration * duration_fraction);
    return applyLoadingEase(LoadingEase::EaseInCubic, (state_time_ - start_time) / stage_duration);
}

double ResortTransferLoadingScreen::introDuration() const {
    return loadingSequenceDuration(config_.timing.intro_water_sun, config_.timing.intro_clouds, config_.timing.intro_boat);
}

double ResortTransferLoadingScreen::outroDuration() const {
    return loadingSequenceDuration(config_.timing.outro_boat, config_.timing.outro_water_sun, config_.timing.outro_clouds);
}

} // namespace pr
