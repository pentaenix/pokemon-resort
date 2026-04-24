#pragma once

#include "core/Types.hpp"
#include "ui/ScreenInput.hpp"

#include <SDL.h>

namespace pr {

class InputRouter {
public:
    bool handleEvent(const SDL_Event& event, const InputConfig& config, ScreenInput* input);
    void update(double dt, ScreenInput* input);
    void resetHold();

private:
    struct NavigationHold {
        int dx = 0;
        int dy = 0;
        double elapsed_seconds = 0.0;
        double repeat_elapsed_seconds = 0.0;
    };

    static constexpr double kNavigationRepeatDelaySeconds = 0.42;
    static constexpr double kNavigationRepeatIntervalSeconds = 0.18;

    static NavigationHold navigationDeltaForKey(SDL_Keycode key, const InputConfig& config);
    static NavigationHold navigationDeltaForControllerButton(Uint8 button);
    static bool isControllerNavigationButton(Uint8 button);

    bool dispatchNavigation(const NavigationHold& nav, ScreenInput* input);
    bool releaseNavigationHold(const NavigationHold& nav);
    void startHold(const NavigationHold& nav);

    NavigationHold navigation_hold_;
};

} // namespace pr
