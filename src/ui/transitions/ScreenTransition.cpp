#include "ui/transitions/ScreenTransition.hpp"

#include "core/config/Json.hpp"

#include <algorithm>
#include <filesystem>

namespace pr::transitions {

namespace {
double numberOr(const JsonValue* value, double fallback) {
    return value && value->isNumber() ? value->asNumber() : fallback;
}
std::string stringOr(const JsonValue* value, const std::string& fallback) {
    return value && value->isString() ? value->asString() : fallback;
}
TransitionStyle parseStyle(const JsonValue* value, TransitionStyle fallback) {
    if (!value || !value->isObject()) return fallback;
    fallback.type = stringOr(value->get("type"), fallback.type);
    fallback.duration_seconds = std::max(0.01, numberOr(value->get("durationSeconds"), fallback.duration_seconds));
    fallback.circle_segments = std::clamp(static_cast<int>(numberOr(value->get("circleSegments"), fallback.circle_segments)), 16, 192);
    fallback.max_radius_scale = std::max(1.0, numberOr(value->get("maxRadiusScale"), fallback.max_radius_scale));
    return fallback;
}
} // namespace

OverworldTransitionConfig loadOverworldTransitionConfig(const std::string& project_root) {
    OverworldTransitionConfig out{};
    try {
        const JsonValue root = parseJsonFile((std::filesystem::path(project_root) /
            "config/gameplay/world3d/transitions.json").string());
        out.attend = parseStyle(root.get("attend"), out.attend);
    } catch (...) {}
    return out;
}

void ScreenTransition::startClosing(const TransitionStyle& style) {
    if (phase_ != Phase::Idle) return;
    style_ = style;
    amount_ = 0.0;
    closed_event_ = false;
    phase_ = Phase::Closing;
}

void ScreenTransition::startOpening(const TransitionStyle& style) {
    style_ = style;
    amount_ = 1.0;
    closed_event_ = false;
    phase_ = Phase::Opening;
}

void ScreenTransition::update(double dt) {
    const double step = std::max(0.0, dt) / std::max(0.01, style_.duration_seconds);
    if (phase_ == Phase::Closing) {
        amount_ = std::min(1.0, amount_ + step);
        if (amount_ >= 1.0) { phase_ = Phase::Closed; closed_event_ = true; }
    } else if (phase_ == Phase::Opening) {
        amount_ = std::max(0.0, amount_ - step);
        if (amount_ <= 0.0) phase_ = Phase::Idle;
    }
}

bool ScreenTransition::consumeClosed() {
    const bool value = closed_event_;
    closed_event_ = false;
    return value;
}

} // namespace pr::transitions
