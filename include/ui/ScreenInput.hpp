#pragma once

#include <optional>

namespace pr {

class ScreenInput {
public:
    virtual ~ScreenInput() = default;

    virtual bool canNavigate() const { return false; }
    virtual void onNavigate(int delta) { (void)delta; }

    /// Optional 2D navigation (controllers / d-pad / left-right UI graphs).
    virtual bool canNavigate2d() const { return false; }
    virtual void onNavigate2d(int dx, int dy) {
        (void)dx;
        (void)dy;
    }

    virtual bool acceptsAdvanceInput() const { return true; }
    virtual void onAdvancePressed() {}
    virtual void onBackPressed() {}

    // --- Optional "long press" hooks (implemented by InputRouter) ---
    // If `captureAdvanceForLongPress()` is true, InputRouter will NOT call `onAdvancePressed()` on key-down.
    // Instead, it will either call `onAdvanceLongPress()` once the configured duration elapses, or call
    // `onAdvancePressed()` on key-up if the press was shorter than the threshold.
    virtual bool captureAdvanceForLongPress() const { return false; }
    virtual std::optional<double> advanceLongPressSeconds() const { return std::nullopt; }
    virtual void onAdvanceLongPress() {}

    // If `captureNavigate2dForLongPress(dx, dy)` is true, InputRouter will defer `onNavigate2d(dx, dy)` until key-up
    // (unless the long press triggers first).
    virtual bool captureNavigate2dForLongPress(int dx, int dy) const {
        (void)dx;
        (void)dy;
        return false;
    }
    virtual std::optional<double> navigate2dLongPressSeconds(int dx, int dy) const {
        (void)dx;
        (void)dy;
        return std::nullopt;
    }
    virtual void onNavigate2dLongPress(int dx, int dy) {
        (void)dx;
        (void)dy;
    }

    virtual void handlePointerMoved(int logical_x, int logical_y) {
        (void)logical_x;
        (void)logical_y;
    }

    virtual bool handlePointerPressed(int logical_x, int logical_y) {
        (void)logical_x;
        (void)logical_y;
        return false;
    }

    virtual bool handlePointerReleased(int logical_x, int logical_y) {
        (void)logical_x;
        (void)logical_y;
        return false;
    }
};

} // namespace pr
