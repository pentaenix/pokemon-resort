#include "core/app/transition/AppTransitionController.hpp"

#include <algorithm>

namespace pr {
namespace {

constexpr double kSaveSoundPreTransitionDelaySeconds = 0.5;
constexpr double kQuickTransitionFadeSeconds = 0.25;

double clamp01(double value) {
    return std::max(0.0, std::min(1.0, value));
}

double phaseProgress(double elapsed_seconds, double duration_seconds) {
    if (duration_seconds <= 1e-6) {
        return 1.0;
    }
    return clamp01(elapsed_seconds / duration_seconds);
}

} // namespace

void AppTransitionController::startSuccessfulSaveQuickTransition() {
    phase_ = Phase::SaveSoundDelay;
    elapsed_seconds_ = 0.0;
    overlay_alpha_ = 0.0;
}

AppTransitionController::StepResult AppTransitionController::update(
    double dt,
    bool loading_complete) {
    StepResult result;
    if (phase_ == Phase::None) {
        return result;
    }

    elapsed_seconds_ += dt;
    switch (phase_) {
        case Phase::SaveSoundDelay:
            overlay_alpha_ = 0.0;
            if (elapsed_seconds_ >= kSaveSoundPreTransitionDelaySeconds) {
                phase_ = Phase::FadeOutToLoading;
                elapsed_seconds_ = 0.0;
            }
            break;
        case Phase::FadeOutToLoading: {
            const double t = phaseProgress(elapsed_seconds_, kQuickTransitionFadeSeconds);
            overlay_alpha_ = t;
            if (t >= 1.0) {
                result.begin_loading = true;
                phase_ = Phase::FadeInLoading;
                elapsed_seconds_ = 0.0;
                overlay_alpha_ = 1.0;
            }
            break;
        }
        case Phase::FadeInLoading: {
            const double t = phaseProgress(elapsed_seconds_, kQuickTransitionFadeSeconds);
            overlay_alpha_ = 1.0 - t;
            if (t >= 1.0) {
                phase_ = Phase::LoadingActive;
                elapsed_seconds_ = 0.0;
                overlay_alpha_ = 0.0;
            }
            break;
        }
        case Phase::LoadingActive:
            overlay_alpha_ = 0.0;
            if (loading_complete) {
                phase_ = Phase::FadeOutToDestination;
                elapsed_seconds_ = 0.0;
            }
            break;
        case Phase::FadeOutToDestination: {
            const double t = phaseProgress(elapsed_seconds_, kQuickTransitionFadeSeconds);
            overlay_alpha_ = t;
            if (t >= 1.0) {
                result.finish_destination = true;
                phase_ = Phase::FadeInDestination;
                elapsed_seconds_ = 0.0;
                overlay_alpha_ = 1.0;
            }
            break;
        }
        case Phase::FadeInDestination: {
            const double t = phaseProgress(elapsed_seconds_, kQuickTransitionFadeSeconds);
            overlay_alpha_ = 1.0 - t;
            if (t >= 1.0) {
                phase_ = Phase::None;
                elapsed_seconds_ = 0.0;
                overlay_alpha_ = 0.0;
            }
            break;
        }
        case Phase::None:
            break;
    }

    return result;
}

bool AppTransitionController::active() const {
    return phase_ != Phase::None;
}

bool AppTransitionController::blocksInput() const {
    return active();
}

} // namespace pr
