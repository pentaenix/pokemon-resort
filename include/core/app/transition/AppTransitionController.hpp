#pragma once

namespace pr {

class AppTransitionController {
public:
    struct StepResult {
        bool begin_loading = false;
        bool finish_destination = false;
    };

    void startSuccessfulSaveQuickTransition();
    StepResult update(double dt, bool loading_complete);

    bool active() const;
    bool blocksInput() const;
    double overlayAlpha() const { return overlay_alpha_; }

private:
    enum class Phase {
        None,
        SaveSoundDelay,
        FadeOutToLoading,
        FadeInLoading,
        LoadingActive,
        FadeOutToDestination,
        FadeInDestination
    };

    Phase phase_ = Phase::None;
    double elapsed_seconds_ = 0.0;
    double overlay_alpha_ = 0.0;
};

} // namespace pr
