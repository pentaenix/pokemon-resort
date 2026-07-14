#pragma once

#include <string>

namespace pr::transitions {

struct TransitionStyle {
    std::string type = "black_iris";
    double duration_seconds = 0.28;
    int circle_segments = 64;
    double max_radius_scale = 1.15;
};

struct OverworldTransitionConfig {
    TransitionStyle attend{};
};

OverworldTransitionConfig loadOverworldTransitionConfig(const std::string& project_root);

class ScreenTransition {
public:
    enum class Phase { Idle, Closing, Closed, Opening };

    void startClosing(const TransitionStyle& style);
    void startOpening(const TransitionStyle& style);
    void update(double dt);
    bool consumeClosed();
    bool active() const { return phase_ != Phase::Idle; }
    bool blocksInput() const { return phase_ == Phase::Closing || phase_ == Phase::Closed; }
    Phase phase() const { return phase_; }
    double closedAmount() const { return amount_; }
    const TransitionStyle& style() const { return style_; }

private:
    TransitionStyle style_{};
    Phase phase_ = Phase::Idle;
    double amount_ = 0.0;
    bool closed_event_ = false;
};

} // namespace pr::transitions
