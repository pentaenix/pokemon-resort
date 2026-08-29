#include "core/input/InputRouter.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

struct FakeScreen final : pr::ScreenInput {
    bool one_dimensional = false;
    bool two_dimensional = false;
    bool accepts_controller_axis = false;
    bool accepts_advance = true;
    int navigate_calls = 0;
    int navigate_sum = 0;
    int navigate2d_calls = 0;
    int navigate2d_sum_x = 0;
    int navigate2d_sum_y = 0;
    int navigate2d_release_calls = 0;
    int last_dx = 0;
    int last_dy = 0;
    int advance_calls = 0;
    int back_calls = 0;
    int aquarium_construction_calls = 0;
    int move_calls = 0;
    int press_calls = 0;
    int release_calls = 0;
    bool capture_advance = false;
    std::optional<double> advance_long_press_seconds{};
    int advance_long_press_calls = 0;
    int advance_charge_calls = 0;
    double last_advance_charge_elapsed = 0.0;
    int advance_end_calls = 0;
    bool last_advance_end_triggered = false;
    bool capture_nav2d_down = false;
    std::optional<double> nav2d_down_long_press_seconds{};
    int nav2d_long_press_calls = 0;
    int nav2d_charge_calls = 0;
    double last_nav2d_charge_elapsed = 0.0;
    int nav2d_end_calls = 0;
    bool last_nav2d_end_triggered = false;
    bool capture_right_mouse = false;
    bool capture_controller_extras = false;
    int unrouted_mouse_calls = 0;
    int unrouted_controller_calls = 0;

    bool canNavigate() const override { return one_dimensional; }
    void onNavigate(int delta) override {
        ++navigate_calls;
        navigate_sum += delta;
    }

    bool canNavigate2d() const override { return two_dimensional; }
    bool acceptsControllerAxisNavigation() const override { return accepts_controller_axis; }
    void onNavigate2d(int dx, int dy) override {
        ++navigate2d_calls;
        navigate2d_sum_x += dx;
        navigate2d_sum_y += dy;
        last_dx = dx;
        last_dy = dy;
    }
    void onNavigate2dReleased(int dx, int dy) override {
        ++navigate2d_release_calls;
        last_dx = dx;
        last_dy = dy;
    }

    bool acceptsAdvanceInput() const override { return accepts_advance; }
    void onAdvancePressed() override { ++advance_calls; }
    bool captureAdvanceForLongPress() const override { return capture_advance; }
    std::optional<double> advanceLongPressSeconds() const override { return advance_long_press_seconds; }
    void onAdvanceLongPress() override { ++advance_long_press_calls; }
    void onAdvanceLongPressCharge(double elapsed_seconds) override {
        ++advance_charge_calls;
        last_advance_charge_elapsed = elapsed_seconds;
    }
    void onAdvanceLongPressEnded(bool long_press_action_fired) override {
        ++advance_end_calls;
        last_advance_end_triggered = long_press_action_fired;
    }
    void onBackPressed() override { ++back_calls; }
    void onAquariumConstructionPressed(SDL_JoystickID) override { ++aquarium_construction_calls; }
    void handlePointerMoved(int, int) override { ++move_calls; }
    bool handlePointerPressed(int, int) override {
        ++press_calls;
        return true;
    }
    bool handlePointerReleased(int, int) override {
        ++release_calls;
        return true;
    }
    bool handleUnroutedSdlEvent(const SDL_Event& event) override {
        if (capture_right_mouse &&
            (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) &&
            event.button.button == SDL_BUTTON_RIGHT) {
            ++unrouted_mouse_calls;
            return true;
        }
        if (capture_controller_extras &&
            (event.type == SDL_CONTROLLERBUTTONDOWN ||
             event.type == SDL_CONTROLLERBUTTONUP) &&
            event.cbutton.button == SDL_CONTROLLER_BUTTON_X) {
            ++unrouted_controller_calls;
            return true;
        }
        return false;
    }

    bool captureNavigate2dForLongPress(int dx, int dy) const override {
        return capture_nav2d_down && dx == 0 && dy == 1;
    }
    std::optional<double> navigate2dLongPressSeconds(int dx, int dy) const override {
        if (dx == 0 && dy == 1) {
            return nav2d_down_long_press_seconds;
        }
        return std::nullopt;
    }
    void onNavigate2dLongPress(int, int) override { ++nav2d_long_press_calls; }
    void onNavigationLongPressCharge(double elapsed_seconds, int dx, int dy) override {
        ++nav2d_charge_calls;
        last_nav2d_charge_elapsed = elapsed_seconds;
        (void)dx;
        (void)dy;
    }
    void onNavigationLongPressEnded(bool long_press_action_fired) override {
        ++nav2d_end_calls;
        last_nav2d_end_triggered = long_press_action_fired;
    }
};

SDL_Event keyDown(SDL_Keycode key, bool repeat = false) {
    SDL_Event event{};
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = key;
    event.key.repeat = repeat ? 1 : 0;
    return event;
}

SDL_Event keyUp(SDL_Keycode key) {
    SDL_Event event{};
    event.type = SDL_KEYUP;
    event.key.keysym.sym = key;
    return event;
}

SDL_Event controllerDown(Uint8 button) {
    SDL_Event event{};
    event.type = SDL_CONTROLLERBUTTONDOWN;
    event.cbutton.button = button;
    return event;
}

SDL_Event controllerButtonUp(Uint8 button) {
    SDL_Event event{};
    event.type = SDL_CONTROLLERBUTTONUP;
    event.cbutton.button = button;
    return event;
}

SDL_Event controllerAxis(Uint8 axis, Sint16 value) {
    SDL_Event event{};
    event.type = SDL_CONTROLLERAXISMOTION;
    event.caxis.axis = axis;
    event.caxis.value = value;
    return event;
}

SDL_Event mouseEvent(Uint32 type, Uint8 button = SDL_BUTTON_LEFT) {
    SDL_Event event{};
    event.type = type;
    event.motion.x = 12;
    event.motion.y = 34;
    event.button.x = 12;
    event.button.y = 34;
    event.button.button = button;
    return event;
}

} // namespace

int main() {
    pr::InputConfig config;
    pr::InputRouter router;

    FakeScreen menu;
    menu.one_dimensional = true;
    expect(router.handleEvent(keyDown(SDLK_DOWN), config, &menu), "keyboard down is handled");
    expect(menu.navigate_calls == 1 && menu.navigate_sum == 1, "keyboard down navigates once");
    router.update(0.41, &menu);
    expect(menu.navigate_calls == 1, "hold does not repeat before delay");
    router.update(0.02, &menu);
    expect(menu.navigate_calls == 2, "hold repeats at delay boundary");
    router.update(0.36, &menu);
    expect(menu.navigate_calls == 4, "hold repeats at interval cadence");
    router.handleEvent(keyUp(SDLK_DOWN), config, &menu);
    router.update(1.0, &menu);
    expect(menu.navigate_calls == 4, "key release stops repeats");

    FakeScreen grid;
    grid.two_dimensional = true;
    expect(router.handleEvent(controllerDown(SDL_CONTROLLER_BUTTON_DPAD_LEFT), config, &grid), "controller left is handled");
    expect(grid.navigate2d_calls == 1 && grid.last_dx == -1 && grid.last_dy == 0, "controller left dispatches 2D navigation");
    expect(router.handleEvent(
        controllerButtonUp(SDL_CONTROLLER_BUTTON_DPAD_LEFT), config, &grid),
        "controller left release is handled");
    expect(grid.navigate2d_release_calls == 1 && grid.last_dx == -1 && grid.last_dy == 0,
        "controller release lets a 2D screen clear movement latches");
    expect(router.handleEvent(keyDown(SDLK_RIGHT), config, &grid),
        "keyboard right is handled by a 2D screen");
    expect(router.handleEvent(keyUp(SDLK_RIGHT), config, &grid),
        "keyboard right release is handled");
    expect(grid.navigate2d_release_calls == 2 && grid.last_dx == 1 && grid.last_dy == 0,
        "keyboard release uses the same 2D release contract");

    FakeScreen keyboard_grid_hold;
    keyboard_grid_hold.two_dimensional = true;
    pr::InputRouter keyboard_grid_router;
    expect(keyboard_grid_router.handleEvent(keyDown(SDLK_RIGHT), config, &keyboard_grid_hold),
        "construction-style keyboard grid hold begins");
    keyboard_grid_router.update(0.43, &keyboard_grid_hold);
    keyboard_grid_router.update(1.45, &keyboard_grid_hold);
    expect(keyboard_grid_hold.navigate2d_calls == 10 &&
           keyboard_grid_hold.navigate2d_sum_x == 10 &&
           keyboard_grid_hold.navigate2d_sum_y == 0,
        "held keyboard movement produces ten whole-cell semantic steps");
    keyboard_grid_router.handleEvent(keyUp(SDLK_RIGHT), config, &keyboard_grid_hold);

    FakeScreen dpad_grid_hold;
    dpad_grid_hold.two_dimensional = true;
    pr::InputRouter dpad_grid_router;
    expect(dpad_grid_router.handleEvent(
        controllerDown(SDL_CONTROLLER_BUTTON_DPAD_DOWN), config, &dpad_grid_hold),
        "construction-style D-pad grid hold begins");
    dpad_grid_router.update(0.43, &dpad_grid_hold);
    dpad_grid_router.update(1.45, &dpad_grid_hold);
    expect(dpad_grid_hold.navigate2d_calls == 10 &&
           dpad_grid_hold.navigate2d_sum_x == 0 &&
           dpad_grid_hold.navigate2d_sum_y == 10,
        "held D-pad movement produces ten whole-cell semantic steps");
    dpad_grid_router.handleEvent(
        controllerButtonUp(SDL_CONTROLLER_BUTTON_DPAD_DOWN), config, &dpad_grid_hold);

    FakeScreen actions;
    expect(router.handleEvent(keyDown(SDLK_RETURN), config, &actions), "keyboard forward is handled");
    expect(actions.advance_calls == 1, "keyboard forward advances");
    expect(router.handleEvent(controllerDown(SDL_CONTROLLER_BUTTON_B), config, &actions), "controller back is handled");
    expect(actions.back_calls == 1, "controller B backs out");
    expect(router.handleEvent(keyDown(SDLK_z), config, &actions),
        "configured aquarium construction key is handled");
    expect(router.handleEvent(controllerDown(SDL_CONTROLLER_BUTTON_Y), config, &actions),
        "controller Y aquarium construction action is handled");
    expect(actions.aquarium_construction_calls == 2,
        "keyboard Z and controller Y share one semantic construction action");

    FakeScreen stick;
    stick.two_dimensional = true;
    expect(!router.handleEvent(controllerAxis(SDL_CONTROLLER_AXIS_LEFTX, 20000), config, &stick),
        "left stick does not leak into screens that do not explicitly own axis navigation");
    expect(stick.navigate2d_calls == 0,
        "unowned left stick input does not navigate the screen");
    stick.accepts_controller_axis = true;
    expect(router.handleEvent(controllerAxis(SDL_CONTROLLER_AXIS_LEFTX, 20000), config, &stick),
        "left stick navigation is handled outside the dead zone");
    expect(stick.navigate2d_calls == 1 && stick.last_dx == 1 && stick.last_dy == 0,
        "left stick right dispatches semantic 2D navigation");
    expect(router.handleEvent(controllerAxis(SDL_CONTROLLER_AXIS_LEFTX, 0), config, &stick),
        "left stick return to dead zone is handled");
    expect(stick.navigate2d_release_calls == 1,
        "left stick return releases semantic navigation");

    FakeScreen stick_grid_hold;
    stick_grid_hold.two_dimensional = true;
    stick_grid_hold.accepts_controller_axis = true;
    pr::InputRouter stick_grid_router;
    expect(stick_grid_router.handleEvent(
        controllerAxis(SDL_CONTROLLER_AXIS_LEFTX, -20000), config, &stick_grid_hold),
        "construction-style left-stick grid hold begins outside the dead zone");
    stick_grid_router.update(0.43, &stick_grid_hold);
    stick_grid_router.update(1.45, &stick_grid_hold);
    expect(stick_grid_hold.navigate2d_calls == 10 &&
           stick_grid_hold.navigate2d_sum_x == -10 &&
           stick_grid_hold.navigate2d_sum_y == 0,
        "held left-stick movement produces ten whole-cell semantic steps");
    stick_grid_router.handleEvent(
        controllerAxis(SDL_CONTROLLER_AXIS_LEFTX, 0), config, &stick_grid_hold);

    actions.accepts_advance = false;
    expect(router.handleEvent(controllerDown(SDL_CONTROLLER_BUTTON_A), config, &actions), "controller A remains handled when advance blocked");
    expect(actions.advance_calls == 1, "blocked advance does not call screen");

    FakeScreen long_press;
    long_press.capture_advance = true;
    long_press.advance_long_press_seconds = 0.5;
    expect(router.handleEvent(keyDown(SDLK_RETURN), config, &long_press), "captured forward keydown is handled");
    expect(long_press.advance_calls == 0, "captured forward does not advance on keydown");
    router.update(0.49, &long_press);
    expect(long_press.advance_charge_calls >= 1 && long_press.last_advance_charge_elapsed >= 0.49,
        "advance long press reports charge progress before threshold");
    expect(long_press.advance_long_press_calls == 0, "advance long press does not trigger before threshold");
    router.update(0.02, &long_press);
    expect(long_press.advance_charge_calls >= 2, "advance charge continues through threshold crossing frame");
    expect(long_press.advance_long_press_calls == 1, "advance long press triggers after threshold");
    expect(router.handleEvent(keyUp(SDLK_RETURN), config, &long_press), "forward keyup is handled when captured");
    expect(long_press.advance_calls == 0, "after long press, keyup does not call advance");
    expect(long_press.advance_end_calls == 1 && long_press.last_advance_end_triggered,
        "advance long press end reports fired action");

    FakeScreen short_press;
    short_press.capture_advance = true;
    short_press.advance_long_press_seconds = 1.0;
    expect(router.handleEvent(keyDown(SDLK_RETURN), config, &short_press), "captured forward short keydown is handled");
    router.update(0.2, &short_press);
    expect(short_press.advance_charge_calls >= 1, "short captured forward receives charge ticks");
    expect(router.handleEvent(keyUp(SDLK_RETURN), config, &short_press), "captured forward short keyup is handled");
    expect(short_press.advance_calls == 1, "short captured forward triggers advance on keyup");
    expect(short_press.advance_end_calls == 1 && !short_press.last_advance_end_triggered,
        "advance long press end reports cancel when threshold not reached");

    FakeScreen nav_long;
    nav_long.two_dimensional = true;
    nav_long.capture_nav2d_down = true;
    nav_long.nav2d_down_long_press_seconds = 0.4;
    expect(router.handleEvent(keyDown(SDLK_DOWN), config, &nav_long), "captured down keydown is handled");
    expect(nav_long.navigate2d_calls == 0, "captured down does not navigate on keydown");
    router.update(0.2, &nav_long);
    expect(nav_long.nav2d_charge_calls == 1 && nav_long.last_nav2d_charge_elapsed >= 0.199,
        "navigation long press reports charge progress before threshold");
    router.update(0.21, &nav_long);
    expect(nav_long.nav2d_long_press_calls == 1, "down long press triggers after threshold");
    expect(nav_long.nav2d_charge_calls == 2, "charge callback includes the frame that crosses the threshold");
    expect(router.handleEvent(keyUp(SDLK_DOWN), config, &nav_long), "captured down keyup is handled");
    expect(nav_long.navigate2d_calls == 0, "after down long press, keyup does not navigate");
    expect(nav_long.nav2d_end_calls == 1 && nav_long.last_nav2d_end_triggered,
        "navigation long press end reports that the action already fired");

    FakeScreen nav_short_cancel;
    nav_short_cancel.two_dimensional = true;
    nav_short_cancel.capture_nav2d_down = true;
    nav_short_cancel.nav2d_down_long_press_seconds = 1.0;
    expect(router.handleEvent(keyDown(SDLK_DOWN), config, &nav_short_cancel), "short captured down keydown handled");
    router.update(0.15, &nav_short_cancel);
    expect(nav_short_cancel.nav2d_charge_calls >= 1, "short hold still receives charge ticks");
    expect(router.handleEvent(keyUp(SDLK_DOWN), config, &nav_short_cancel), "short captured down keyup handled");
    expect(nav_short_cancel.nav2d_long_press_calls == 0, "short hold does not fire long press");
    expect(nav_short_cancel.nav2d_end_calls == 1 && !nav_short_cancel.last_nav2d_end_triggered,
        "navigation long press end reports cancel when threshold not reached");

    FakeScreen nav_long_pad;
    nav_long_pad.two_dimensional = true;
    nav_long_pad.capture_nav2d_down = true;
    nav_long_pad.nav2d_down_long_press_seconds = 0.4;
    expect(router.handleEvent(controllerDown(SDL_CONTROLLER_BUTTON_DPAD_DOWN), config, &nav_long_pad),
        "controller D-pad captured down is handled");
    expect(nav_long_pad.navigate2d_calls == 0, "controller captured down does not navigate on press");
    router.update(0.41, &nav_long_pad);
    expect(nav_long_pad.nav2d_long_press_calls == 1, "controller D-pad down long press triggers after threshold");
    expect(router.handleEvent(controllerButtonUp(SDL_CONTROLLER_BUTTON_DPAD_DOWN), config, &nav_long_pad),
        "controller captured down release is handled");
    expect(nav_long_pad.navigate2d_calls == 0, "after controller down long press, release does not navigate");
    expect(nav_long_pad.nav2d_end_calls == 1 && nav_long_pad.last_nav2d_end_triggered,
        "controller navigation long press end reports fired action");

    FakeScreen pad_short_cancel;
    pad_short_cancel.two_dimensional = true;
    pad_short_cancel.capture_nav2d_down = true;
    pad_short_cancel.nav2d_down_long_press_seconds = 1.0;
    expect(router.handleEvent(controllerDown(SDL_CONTROLLER_BUTTON_DPAD_DOWN), config, &pad_short_cancel),
        "controller short captured down handled");
    router.update(0.15, &pad_short_cancel);
    expect(router.handleEvent(controllerButtonUp(SDL_CONTROLLER_BUTTON_DPAD_DOWN), config, &pad_short_cancel),
        "controller short captured down release handled");
    expect(pad_short_cancel.navigate2d_calls == 1 && pad_short_cancel.nav2d_long_press_calls == 0,
        "controller short captured down dispatches navigate on release");
    expect(pad_short_cancel.nav2d_end_calls == 1 && !pad_short_cancel.last_nav2d_end_triggered,
        "controller short navigation long press end reports cancel");

    FakeScreen pointer;
    expect(router.handleEvent(mouseEvent(SDL_MOUSEMOTION), config, &pointer), "mouse motion is handled when enabled");
    expect(router.handleEvent(mouseEvent(SDL_MOUSEBUTTONDOWN), config, &pointer), "mouse press is handled when enabled");
    expect(router.handleEvent(mouseEvent(SDL_MOUSEBUTTONUP), config, &pointer), "mouse release is handled when enabled");
    expect(pointer.move_calls == 1 && pointer.press_calls == 1 && pointer.release_calls == 1, "mouse events dispatch to screen");

    FakeScreen context_pointer;
    context_pointer.capture_right_mouse = true;
    expect(router.handleEvent(
        mouseEvent(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_RIGHT), config, &context_pointer),
        "right mouse press can be captured as a semantic back action");
    expect(router.handleEvent(
        mouseEvent(SDL_MOUSEBUTTONUP, SDL_BUTTON_RIGHT), config, &context_pointer),
        "right mouse release can be captured without leaking to pointer placement");
    expect(context_pointer.unrouted_mouse_calls == 2 &&
           context_pointer.press_calls == 0 && context_pointer.release_calls == 0,
        "captured right mouse input bypasses left-button placement callbacks");

    FakeScreen construction_controller;
    construction_controller.capture_controller_extras = true;
    expect(router.handleEvent(
        controllerDown(SDL_CONTROLLER_BUTTON_X), config, &construction_controller),
        "construction context can capture controller edit actions before global routing");
    expect(router.handleEvent(
        controllerButtonUp(SDL_CONTROLLER_BUTTON_X), config, &construction_controller),
        "construction context can capture controller edit-action release");
    expect(construction_controller.unrouted_controller_calls == 2 &&
           construction_controller.advance_calls == 0 &&
           construction_controller.back_calls == 0,
        "captured controller edit action does not leak into gameplay actions");

    config.accept_mouse = false;
    expect(!router.handleEvent(mouseEvent(SDL_MOUSEMOTION), config, &pointer), "mouse motion ignored when disabled");
    expect(pointer.move_calls == 1, "disabled mouse does not dispatch");

    config.accept_controller = false;
    expect(!router.handleEvent(controllerDown(SDL_CONTROLLER_BUTTON_DPAD_RIGHT), config, &grid), "controller input ignored when disabled");
    expect(grid.navigate2d_calls == 2, "disabled controller does not dispatch");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
