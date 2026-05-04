#include "core/config/ConfigLoader.hpp"
#include "core/input/InputRouter.hpp"

#include <SDL.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

struct FakeScreen final : pr::ScreenInput {
    bool one_dimensional = true;
    bool two_dimensional = true;
    int navigate_calls = 0;
    int navigate_sum = 0;
    int navigate2d_calls = 0;
    int last_dx = 0;
    int last_dy = 0;
    int advance_calls = 0;
    int back_calls = 0;
    int pointer_press_calls = 0;

    bool canNavigate() const override { return one_dimensional; }
    void onNavigate(int delta) override {
        ++navigate_calls;
        navigate_sum += delta;
    }

    bool canNavigate2d() const override { return two_dimensional; }
    void onNavigate2d(int dx, int dy) override {
        ++navigate2d_calls;
        last_dx = dx;
        last_dy = dy;
    }

    void onAdvancePressed() override { ++advance_calls; }
    void onBackPressed() override { ++back_calls; }
    bool handlePointerPressed(int, int) override {
        ++pointer_press_calls;
        return true;
    }
};

SDL_Event keyDown(SDL_Keycode key) {
    SDL_Event event{};
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = key;
    return event;
}

SDL_Event controllerDown(Uint8 button) {
    SDL_Event event{};
    event.type = SDL_CONTROLLERBUTTONDOWN;
    event.cbutton.button = button;
    return event;
}

SDL_Event mouseDown() {
    SDL_Event event{};
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.x = 20;
    event.button.y = 30;
    return event;
}

fs::path writeTempJson(const std::string& name, const std::string& contents) {
    const fs::path path = fs::temp_directory_path() / ("pokemon_resort_" + name + ".json");
    std::ofstream out(path);
    if (!out) {
        throw TestFailure("Could not open temp config for writing: " + path.string());
    }
    out << contents;
    return path;
}

pr::AppConfig customInputConfig() {
    const fs::path path = writeTempJson("input_router_custom_bindings", R"json({
        "input": {
            "accept_mouse": false,
            "accept_controller": false,
            "navigate_up_keys": ["I"],
            "navigate_down_keys": ["K"],
            "navigate_left_keys": ["J"],
            "navigate_right_keys": ["L"],
            "forward_keys": ["P"],
            "back_keys": ["O"]
        }
    })json");
    pr::AppConfig config = pr::loadAppConfigFromJson(path.string());
    fs::remove(path);
    return config;
}

void testJsonKeyboardBindingsDriveRouterInsteadOfDefaults() {
    const pr::AppConfig app_config = customInputConfig();
    pr::InputRouter router;
    FakeScreen screen;

    expect(router.handleEvent(keyDown(SDLK_i), app_config.input, &screen), "custom I key should be handled as up");
    expect(screen.navigate2d_calls == 1 && screen.last_dy == -1, "custom I key should dispatch up navigation");

    expect(router.handleEvent(keyDown(SDLK_k), app_config.input, &screen), "custom K key should be handled as down");
    expect(screen.navigate2d_calls == 2 && screen.last_dy == 1, "custom K key should dispatch down navigation");

    expect(router.handleEvent(keyDown(SDLK_j), app_config.input, &screen), "custom J key should be handled as left");
    expect(screen.navigate2d_calls == 3 && screen.last_dx == -1, "custom J key should dispatch left navigation");

    expect(router.handleEvent(keyDown(SDLK_l), app_config.input, &screen), "custom L key should be handled as right");
    expect(screen.navigate2d_calls == 4 && screen.last_dx == 1, "custom L key should dispatch right navigation");

    expect(!router.handleEvent(keyDown(SDLK_w), app_config.input, &screen),
           "default W key must not navigate after JSON overrides navigate_up_keys");
    expect(screen.navigate2d_calls == 4, "default W key should not dispatch after override");
}

void testJsonActionBindingsDriveAdvanceAndBack() {
    const pr::AppConfig app_config = customInputConfig();
    pr::InputRouter router;
    FakeScreen screen;

    expect(router.handleEvent(keyDown(SDLK_p), app_config.input, &screen), "custom P key should be handled as forward");
    expect(screen.advance_calls == 1, "custom P key should dispatch advance");

    expect(router.handleEvent(keyDown(SDLK_o), app_config.input, &screen), "custom O key should be handled as back");
    expect(screen.back_calls == 1, "custom O key should dispatch back");

    expect(!router.handleEvent(keyDown(SDLK_RETURN), app_config.input, &screen),
           "default RETURN key must not advance after JSON overrides forward_keys");
    expect(screen.advance_calls == 1, "default RETURN key should not dispatch after override");
}

void testJsonInputGatesDisableMouseAndController() {
    const pr::AppConfig app_config = customInputConfig();
    pr::InputRouter router;
    FakeScreen screen;

    expect(!router.handleEvent(mouseDown(), app_config.input, &screen),
           "mouse press should be ignored when accept_mouse=false in JSON");
    expect(screen.pointer_press_calls == 0, "disabled mouse input should not reach the screen");

    expect(!router.handleEvent(controllerDown(SDL_CONTROLLER_BUTTON_A), app_config.input, &screen),
           "controller A should be ignored when accept_controller=false in JSON");
    expect(screen.advance_calls == 0, "disabled controller input should not advance the screen");
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests{
        {"JSON keyboard bindings drive router instead of defaults", testJsonKeyboardBindingsDriveRouterInsteadOfDefaults},
        {"JSON action bindings drive advance and back", testJsonActionBindingsDriveAdvanceAndBack},
        {"JSON input gates disable mouse and controller", testJsonInputGatesDisableMouseAndController},
    };

    int failures = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "[PASS] " << test.first << '\n';
        } catch (const std::exception& ex) {
            ++failures;
            std::cerr << "[FAIL] " << test.first << ": " << ex.what() << '\n';
        }
    }

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
