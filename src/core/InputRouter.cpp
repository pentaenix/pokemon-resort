#include "core/InputRouter.hpp"

#include "core/InputBindings.hpp"

namespace pr {

bool InputRouter::handleEvent(
    const SDL_Event& event,
    const InputConfig& config,
    ScreenInput* input) {
    if (event.type == SDL_TEXTINPUT || event.type == SDL_TEXTEDITING) {
        if (input && input->handleUnroutedSdlEvent(event)) {
            return true;
        }
        return false;
    }
    if (event.type == SDL_KEYDOWN && !event.key.repeat) {
        if (input && input->capturesUnroutedKeyboardFocus() && input->handleUnroutedSdlEvent(event)) {
            return true;
        }
        const SDL_Keycode key = event.key.keysym.sym;
        const NavigationHold nav = navigationDeltaForKey(key, config);
        if (nav.dx != 0 || nav.dy != 0) {
            if (input && input->captureNavigate2dForLongPress(nav.dx, nav.dy)) {
                navigation_long_press_hold_.active = true;
                navigation_long_press_hold_.triggered = false;
                navigation_long_press_hold_.dx = nav.dx;
                navigation_long_press_hold_.dy = nav.dy;
                navigation_long_press_hold_.elapsed_seconds = 0.0;
                return true;
            }
            if (dispatchNavigation(nav, input)) {
                startHold(nav);
                return true;
            }
        }
        if (matchesBinding(key, config.back_keys)) {
            if (input) {
                input->onBackPressed();
            }
            return true;
        }
        if (matchesBinding(key, config.forward_keys)) {
            if (input && input->acceptsAdvanceInput()) {
                if (input->captureAdvanceForLongPress()) {
                    advance_hold_.active = true;
                    advance_hold_.triggered = false;
                    advance_hold_.elapsed_seconds = 0.0;
                } else {
                    input->onAdvancePressed();
                }
            }
            return true;
        }
        return false;
    }

    if (event.type == SDL_KEYUP) {
        if (!event.key.repeat) {
            const SDL_Keycode key = event.key.keysym.sym;
            if (matchesBinding(key, config.forward_keys) && input && advance_hold_.active) {
                const bool triggered = advance_hold_.triggered;
                if (input) {
                    input->onAdvanceLongPressEnded(triggered);
                }
                resetAdvanceHold();
                if (!triggered && input->acceptsAdvanceInput()) {
                    input->onAdvancePressed();
                }
                return true;
            }
            const NavigationHold nav = navigationDeltaForKey(key, config);
            if (navigation_long_press_hold_.active &&
                nav.dx == navigation_long_press_hold_.dx &&
                nav.dy == navigation_long_press_hold_.dy) {
                const bool triggered = navigation_long_press_hold_.triggered;
                const int dx = navigation_long_press_hold_.dx;
                const int dy = navigation_long_press_hold_.dy;
                if (input) {
                    input->onNavigationLongPressEnded(triggered);
                }
                resetNavigationLongPressHold();
                if (!triggered) {
                    (void)dispatchNavigation(NavigationHold{dx, dy, 0.0, 0.0}, input);
                }
                return true;
            }
        }
        releaseNavigationHold(navigationDeltaForKey(event.key.keysym.sym, config));
        return false;
    }

    if (event.type == SDL_MOUSEMOTION && config.accept_mouse) {
        if (input) {
            input->handlePointerMoved(event.motion.x, event.motion.y);
        }
        return true;
    }

    if (event.type == SDL_MOUSEBUTTONDOWN && config.accept_mouse) {
        if (input) {
            input->handlePointerPressed(event.button.x, event.button.y);
        }
        return true;
    }

    if (event.type == SDL_MOUSEBUTTONUP && config.accept_mouse) {
        if (input) {
            input->handlePointerReleased(event.button.x, event.button.y);
        }
        return true;
    }

    if (event.type == SDL_CONTROLLERBUTTONDOWN && config.accept_controller) {
        const NavigationHold nav = navigationDeltaForControllerButton(event.cbutton.button);
        if (nav.dx != 0 || nav.dy != 0) {
            if (input && input->captureNavigate2dForLongPress(nav.dx, nav.dy)) {
                navigation_long_press_hold_.active = true;
                navigation_long_press_hold_.triggered = false;
                navigation_long_press_hold_.dx = nav.dx;
                navigation_long_press_hold_.dy = nav.dy;
                navigation_long_press_hold_.elapsed_seconds = 0.0;
                return true;
            }
            if (dispatchNavigation(nav, input)) {
                startHold(nav);
                return true;
            }
        }

        switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_A:
                if (input && input->acceptsAdvanceInput()) {
                    if (input->captureAdvanceForLongPress()) {
                        advance_hold_.active = true;
                        advance_hold_.triggered = false;
                        advance_hold_.elapsed_seconds = 0.0;
                    } else {
                        input->onAdvancePressed();
                    }
                }
                return true;
            case SDL_CONTROLLER_BUTTON_B:
                if (input) {
                    input->onBackPressed();
                }
                return true;
            default:
                return false;
        }
    }

    if (event.type == SDL_CONTROLLERBUTTONUP && config.accept_controller) {
        const NavigationHold released_nav = navigationDeltaForControllerButton(event.cbutton.button);
        if (navigation_long_press_hold_.active &&
            released_nav.dx == navigation_long_press_hold_.dx &&
            released_nav.dy == navigation_long_press_hold_.dy &&
            (released_nav.dx != 0 || released_nav.dy != 0)) {
            const bool triggered = navigation_long_press_hold_.triggered;
            const int dx = navigation_long_press_hold_.dx;
            const int dy = navigation_long_press_hold_.dy;
            if (input) {
                input->onNavigationLongPressEnded(triggered);
            }
            resetNavigationLongPressHold();
            if (!triggered) {
                (void)dispatchNavigation(NavigationHold{dx, dy, 0.0, 0.0}, input);
            }
            return true;
        }
        if (isControllerNavigationButton(event.cbutton.button)) {
            releaseNavigationHold(navigationDeltaForControllerButton(event.cbutton.button));
        }
        if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A && input && advance_hold_.active) {
            const bool triggered = advance_hold_.triggered;
            input->onAdvanceLongPressEnded(triggered);
            resetAdvanceHold();
            if (!triggered && input->acceptsAdvanceInput()) {
                input->onAdvancePressed();
            }
        }
        return false;
    }

    return false;
}

void InputRouter::update(double dt, ScreenInput* input) {
    if (advance_hold_.active && input && !advance_hold_.triggered) {
        advance_hold_.elapsed_seconds += dt;
        input->onAdvanceLongPressCharge(advance_hold_.elapsed_seconds);
        if (const auto threshold = input->advanceLongPressSeconds()) {
            if (advance_hold_.elapsed_seconds >= *threshold) {
                input->onAdvanceLongPress();
                advance_hold_.triggered = true;
            }
        }
    }

    if (navigation_long_press_hold_.active && input && !navigation_long_press_hold_.triggered) {
        navigation_long_press_hold_.elapsed_seconds += dt;
        input->onNavigationLongPressCharge(
            navigation_long_press_hold_.elapsed_seconds,
            navigation_long_press_hold_.dx,
            navigation_long_press_hold_.dy);
        if (const auto threshold = input->navigate2dLongPressSeconds(navigation_long_press_hold_.dx, navigation_long_press_hold_.dy)) {
            if (navigation_long_press_hold_.elapsed_seconds >= *threshold) {
                input->onNavigate2dLongPress(navigation_long_press_hold_.dx, navigation_long_press_hold_.dy);
                navigation_long_press_hold_.triggered = true;
            }
        }
    }

    if (!input ||
        !((input->canNavigate2d() && (navigation_hold_.dx != 0 || navigation_hold_.dy != 0)) ||
          (input->canNavigate() && navigation_hold_.dy != 0))) {
        resetHold();
        return;
    }

    const double previous_elapsed = navigation_hold_.elapsed_seconds;
    navigation_hold_.elapsed_seconds += dt;

    if (previous_elapsed < kNavigationRepeatDelaySeconds &&
        navigation_hold_.elapsed_seconds >= kNavigationRepeatDelaySeconds) {
        dispatchNavigation(navigation_hold_, input);
        navigation_hold_.repeat_elapsed_seconds = 0.0;
        return;
    }

    if (navigation_hold_.elapsed_seconds >= kNavigationRepeatDelaySeconds) {
        navigation_hold_.repeat_elapsed_seconds += dt;
        while (navigation_hold_.repeat_elapsed_seconds >= kNavigationRepeatIntervalSeconds) {
            dispatchNavigation(navigation_hold_, input);
            navigation_hold_.repeat_elapsed_seconds -= kNavigationRepeatIntervalSeconds;
        }
    }
}

void InputRouter::resetHold() {
    navigation_hold_ = {};
}

void InputRouter::resetAdvanceHold() {
    advance_hold_ = {};
}

void InputRouter::resetNavigationLongPressHold() {
    navigation_long_press_hold_ = {};
}

InputRouter::NavigationHold InputRouter::navigationDeltaForKey(
    SDL_Keycode key,
    const InputConfig& config) {
    NavigationHold out;
    if (matchesBinding(key, config.navigate_up_keys)) out.dy = -1;
    if (matchesBinding(key, config.navigate_down_keys)) out.dy = 1;
    if (matchesBinding(key, config.navigate_left_keys)) out.dx = -1;
    if (matchesBinding(key, config.navigate_right_keys)) out.dx = 1;
    return out;
}

InputRouter::NavigationHold InputRouter::navigationDeltaForControllerButton(Uint8 button) {
    NavigationHold out;
    switch (button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            out.dy = -1;
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            out.dy = 1;
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            out.dx = -1;
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            out.dx = 1;
            break;
        default:
            break;
    }
    return out;
}

bool InputRouter::isControllerNavigationButton(Uint8 button) {
    return button == SDL_CONTROLLER_BUTTON_DPAD_UP ||
           button == SDL_CONTROLLER_BUTTON_DPAD_DOWN ||
           button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ||
           button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
}

bool InputRouter::dispatchNavigation(const NavigationHold& nav, ScreenInput* input) {
    if (!input || (nav.dx == 0 && nav.dy == 0)) {
        return false;
    }

    if (input->canNavigate2d()) {
        input->onNavigate2d(nav.dx, nav.dy);
        return true;
    }
    if (nav.dy != 0 && input->canNavigate()) {
        input->onNavigate(nav.dy);
        return true;
    }
    return false;
}

bool InputRouter::releaseNavigationHold(const NavigationHold& nav) {
    if ((nav.dx != 0 && navigation_hold_.dx == nav.dx) ||
        (nav.dy != 0 && navigation_hold_.dy == nav.dy)) {
        resetHold();
        return true;
    }
    return false;
}

void InputRouter::startHold(const NavigationHold& nav) {
    navigation_hold_ = nav;
    navigation_hold_.elapsed_seconds = 0.0;
    navigation_hold_.repeat_elapsed_seconds = 0.0;
}

} // namespace pr
