#pragma once

#include <SDL.h>

#include <optional>

namespace pr {

class ScreenInput {
public:
    virtual ~ScreenInput() = default;

    /// Text input / IME (`SDL_TEXTINPUT`, `SDL_TEXTEDITING`) and keyboard entry while a text field is focused.
    /// Return true to consume the event (InputRouter will not process navigation on the same frame).
    virtual bool handleUnroutedSdlEvent(const SDL_Event& event) {
        (void)event;
        return false;
    }

    /// When true, `SDL_KEYDOWN` for typing keys is offered to `handleUnroutedSdlEvent` before navigation bindings.
    virtual bool capturesUnroutedKeyboardFocus() const { return false; }

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

    /// Called each frame while advance is held and `captureAdvanceForLongPress()` is active.
    virtual void onAdvanceLongPressCharge(double elapsed_seconds) { (void)elapsed_seconds; }

    /// Called when advance is released after a captured hold. `long_press_action_fired` is true if
    /// `onAdvanceLongPress()` already ran.
    virtual void onAdvanceLongPressEnded(bool long_press_action_fired) {
        (void)long_press_action_fired;
    }

    // If `captureNavigate2dForLongPress(dx, dy)` is true, InputRouter will defer `onNavigate2d(dx, dy)` until the
    // matching key or D-pad button is released (unless the long press triggers first).
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

    /// Called each frame while a captured navigation key is held before the long-press action fires.
    virtual void onNavigationLongPressCharge(double elapsed_seconds, int dx, int dy) {
        (void)elapsed_seconds;
        (void)dx;
        (void)dy;
    }

    /// Called when the captured navigation key is released. `long_press_action_fired` is true if
    /// `onNavigate2dLongPress` already ran for this hold.
    virtual void onNavigationLongPressEnded(bool long_press_action_fired) {
        (void)long_press_action_fired;
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
