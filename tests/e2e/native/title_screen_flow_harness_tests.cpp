#include "core/assets/Assets.hpp"
#include "core/config/ConfigLoader.hpp"
#include "core/input/InputRouter.hpp"
#include "ui/TitleScreen.hpp"

#include <SDL.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
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

bool containsEvent(const std::vector<pr::TitleScreenEvent>& events, pr::TitleScreenEvent expected) {
    for (const pr::TitleScreenEvent event : events) {
        if (event == expected) {
            return true;
        }
    }
    return false;
}

fs::path repositoryRoot() {
    fs::path current = fs::current_path();
    while (!current.empty()) {
        if (fs::exists(current / "config" / "app.json") &&
            fs::exists(current / "config" / "title_screen.json")) {
            return current;
        }
        current = current.parent_path();
    }
    throw TestFailure("Could not locate repository root from " + fs::current_path().string());
}

pr::TitleScreenConfig loadRuntimeTitleConfig() {
    const fs::path root = repositoryRoot();
    pr::AppConfig app_config = pr::loadAppConfigFromJson((root / "config" / "app.json").string());
    pr::TitleScreenConfig config =
        pr::loadConfigFromJson((root / "config" / "title_screen.json").string());
    config.window = app_config.window;
    config.input = app_config.input;
    config.audio = app_config.audio;
    return config;
}

pr::Assets makeHarnessAssets(std::size_t menu_label_count) {
    pr::Assets assets;
    assets.menu_labels.resize(menu_label_count);
    return assets;
}

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

class TitleFlowHarness {
public:
    TitleFlowHarness()
        : config_(loadRuntimeTitleConfig()),
          screen_(config_, makeHarnessAssets(config_.menu.items.size())) {}

    pr::TitleScreen& screen() { return screen_; }
    const pr::TitleScreenConfig& config() const { return config_; }

    pr::TitleState state() const { return screen_.debugState(); }
    int selectedMenuIndex() const { return screen_.debugMainMenuSelectedIndex(); }
    int selectedOptionsIndex() const { return screen_.debugOptionsSelectedIndex(); }
    std::string currentSectionTitle() const { return screen_.debugCurrentSectionTitle(); }

    std::vector<pr::TitleScreenEvent> consumeEvents() {
        return screen_.consumeEvents();
    }

    void press(SDL_Keycode key) {
        router_.handleEvent(keyDown(key), config_.input, &screen_);
    }

    void pressController(Uint8 button) {
        router_.handleEvent(controllerDown(button), config_.input, &screen_);
    }

    void advance(double seconds) {
        router_.update(seconds, &screen_);
        screen_.update(seconds);
    }

    void reachWaitingForStart() {
        for (int i = 0; i < 8 && state() != pr::TitleState::WaitingForStart; ++i) {
            press(SDLK_m);
            advance(0.01);
        }
        expect(state() == pr::TitleState::WaitingForStart,
               "expected scripted intro skips to reach WaitingForStart; check title skip flags and TitleScreen::skipTargetState");
    }

    void reachMainMenuIdle() {
        reachWaitingForStart();
        press(SDLK_m);
        expect(state() == pr::TitleState::StartTransition,
               "forward input from WaitingForStart should enter StartTransition");
        advance(config_.timings.start_transition + 0.01);
        expect(state() == pr::TitleState::MainMenuIntro,
               "after start_transition duration, title flow should enter MainMenuIntro");
        advance(config_.menu.animation.intro_duration + 0.01);
        expect(state() == pr::TitleState::MainMenuIdle,
               "after menu intro duration, title flow should enter MainMenuIdle");
    }

    void completeSectionFade() {
        advance(config_.menu.section_transition.button_out_duration + 0.01);
        expect(state() == pr::TitleState::MainMenuSectionFade,
               "menu activation should complete button-out before section fade");
        advance(config_.menu.section_transition.fade_duration + 0.01);
    }

private:
    pr::TitleScreenConfig config_;
    pr::InputRouter router_;
    pr::TitleScreen screen_;
};

void testStartFlowReachesMainMenu() {
    TitleFlowHarness harness;

    harness.reachMainMenuIdle();

    expect(harness.selectedMenuIndex() == 0, "main menu should start with RESORT selected");
    expect(harness.screen().wantsMenuMusic(), "main menu should request menu music");
}

void testMenuMusicIntentFollowsTitleStates() {
    TitleFlowHarness harness;

    expect(!harness.screen().wantsMenuMusic(), "splash should not request menu music");
    harness.reachWaitingForStart();
    expect(harness.screen().wantsMenuMusic(), "WaitingForStart should request menu music");
    harness.reachMainMenuIdle();
    expect(harness.screen().wantsMenuMusic(), "MainMenuIdle should keep menu music requested");
}

void testKeyboardMenuNavigationAndBackReturnToTitlePrompt() {
    TitleFlowHarness harness;
    harness.reachMainMenuIdle();

    harness.press(SDLK_s);
    expect(harness.selectedMenuIndex() == 1, "S should move main menu selection down to TRANSFER via app.json bindings");
    expect(containsEvent(harness.consumeEvents(), pr::TitleScreenEvent::ButtonSfxRequested),
           "moving main menu selection should request button SFX event");

    harness.press(SDLK_w);
    expect(harness.selectedMenuIndex() == 0, "W should move main menu selection up to RESORT via app.json bindings");

    harness.press(SDLK_n);
    expect(harness.state() == pr::TitleState::WaitingForStart,
           "Back from MainMenuIdle should return to WaitingForStart");
    expect(containsEvent(harness.consumeEvents(), pr::TitleScreenEvent::ButtonSfxRequested),
           "backing out of main menu should request button SFX event");
}

void testOptionsFlowChangesSettingsAndReturnsToMenu() {
    TitleFlowHarness harness;
    harness.reachMainMenuIdle();

    harness.press(SDLK_w);
    expect(harness.selectedMenuIndex() == 3, "up from RESORT should wrap to OPTIONS");
    harness.press(SDLK_m);
    expect(harness.state() == pr::TitleState::OptionsIntro, "activating OPTIONS should enter OptionsIntro");
    expect(containsEvent(harness.consumeEvents(), pr::TitleScreenEvent::ButtonSfxRequested),
           "opening OPTIONS should request button SFX event");
    harness.advance(harness.config().menu.animation.intro_duration + 0.01);
    expect(harness.state() == pr::TitleState::OptionsIdle, "OptionsIntro should settle into OptionsIdle");
    expect(harness.selectedOptionsIndex() == 0, "options screen should start on text speed");

    const int original_text_speed = harness.screen().currentUserSettings().text_speed_index;
    harness.press(SDLK_m);
    const std::vector<pr::TitleScreenEvent> change_events = harness.consumeEvents();
    expect(containsEvent(change_events, pr::TitleScreenEvent::ButtonSfxRequested),
           "changing text speed should request button SFX event");
    expect(containsEvent(change_events, pr::TitleScreenEvent::UserSettingsSaveRequested),
           "changing text speed should request user settings persistence event");
    expect(harness.screen().currentUserSettings().text_speed_index != original_text_speed,
           "activating text speed row should cycle the persisted text speed index");

    harness.press(SDLK_w);
    expect(harness.selectedOptionsIndex() == 3, "up from first option should wrap to BACK");
    expect(containsEvent(harness.consumeEvents(), pr::TitleScreenEvent::ButtonSfxRequested),
           "moving to BACK option should request button SFX event");
    harness.press(SDLK_m);
    expect(harness.state() == pr::TitleState::OptionsOutro, "activating BACK should enter OptionsOutro");
    expect(containsEvent(harness.consumeEvents(), pr::TitleScreenEvent::ButtonSfxRequested),
           "activating BACK option should request button SFX event");
    harness.advance(harness.config().menu.animation.outro_duration + 0.01);
    expect(harness.state() == pr::TitleState::MainMenuIdle, "OptionsOutro should return to MainMenuIdle");
    expect(harness.selectedMenuIndex() == 3, "returning from options should preserve OPTIONS as the selected main-menu row");
}

void testTransferSelectionRaisesOpenTransferAfterFade() {
    TitleFlowHarness harness;
    harness.reachMainMenuIdle();

    harness.press(SDLK_s);
    expect(harness.selectedMenuIndex() == 1, "TRANSFER should be selected after one down press");
    harness.press(SDLK_m);
    expect(harness.state() == pr::TitleState::MainMenuToSection,
           "activating TRANSFER should start the shared menu-to-section transition");
    std::vector<pr::TitleScreenEvent> activation_events = harness.consumeEvents();
    expect(containsEvent(activation_events, pr::TitleScreenEvent::ButtonSfxRequested),
           "activating TRANSFER should request button SFX event");
    expect(!containsEvent(activation_events, pr::TitleScreenEvent::OpenTransferRequested),
           "transfer should not open until after the fade completes");

    harness.completeSectionFade();
    expect(containsEvent(harness.consumeEvents(), pr::TitleScreenEvent::OpenTransferRequested),
           "TRANSFER should raise open-transfer event after the fade completes");
}

void testResortSelectionRaisesOpenResortLoadingAfterFade() {
    TitleFlowHarness harness;
    harness.reachMainMenuIdle();

    harness.press(SDLK_m);
    expect(harness.state() == pr::TitleState::MainMenuToSection,
           "activating RESORT should start the shared menu-to-section transition");
    std::vector<pr::TitleScreenEvent> activation_events = harness.consumeEvents();
    expect(containsEvent(activation_events, pr::TitleScreenEvent::ButtonSfxRequested),
           "activating RESORT should request button SFX event");
    expect(!containsEvent(activation_events, pr::TitleScreenEvent::OpenResortLoadingRequested),
           "resort loading should not open until after the fade completes");

    harness.completeSectionFade();
    expect(containsEvent(harness.consumeEvents(), pr::TitleScreenEvent::OpenResortLoadingRequested),
           "RESORT should raise open-resort-loading event after the fade completes");
}

void testTradeSelectionRaisesOpenTradeLoadingAfterFade() {
    TitleFlowHarness harness;
    harness.reachMainMenuIdle();

    harness.press(SDLK_s);
    harness.press(SDLK_s);
    expect(harness.selectedMenuIndex() == 2, "TRADE should be selected after two down presses");
    harness.press(SDLK_m);
    expect(harness.state() == pr::TitleState::MainMenuToSection,
           "activating TRADE should start the shared menu-to-section transition");
    std::vector<pr::TitleScreenEvent> activation_events = harness.consumeEvents();
    expect(containsEvent(activation_events, pr::TitleScreenEvent::ButtonSfxRequested),
           "activating TRADE should request button SFX event");
    expect(!containsEvent(activation_events, pr::TitleScreenEvent::OpenTradeLoadingRequested),
           "trade loading should not open until after the fade completes");

    harness.completeSectionFade();
    expect(containsEvent(harness.consumeEvents(), pr::TitleScreenEvent::OpenTradeLoadingRequested),
           "TRADE should raise open-trade-loading event after the fade completes");
}

void testControllerCanDriveStartAndMenuNavigation() {
    TitleFlowHarness harness;
    harness.reachWaitingForStart();

    harness.pressController(SDL_CONTROLLER_BUTTON_A);
    expect(harness.state() == pr::TitleState::StartTransition,
           "controller A should advance from WaitingForStart");
    harness.advance(harness.config().timings.start_transition + 0.01);
    harness.advance(harness.config().menu.animation.intro_duration + 0.01);
    expect(harness.state() == pr::TitleState::MainMenuIdle,
           "controller-driven start should reach MainMenuIdle");

    harness.pressController(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    expect(harness.selectedMenuIndex() == 1, "controller D-pad down should select TRANSFER");
    expect(containsEvent(harness.consumeEvents(), pr::TitleScreenEvent::ButtonSfxRequested),
           "controller D-pad menu navigation should request button SFX event");
    harness.pressController(SDL_CONTROLLER_BUTTON_B);
    expect(harness.state() == pr::TitleState::WaitingForStart,
           "controller B from MainMenuIdle should return to WaitingForStart");
    expect(containsEvent(harness.consumeEvents(), pr::TitleScreenEvent::ButtonSfxRequested),
           "controller B from main menu should request button SFX event");
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests{
        {"start flow reaches main menu", testStartFlowReachesMainMenu},
        {"menu music intent follows title states", testMenuMusicIntentFollowsTitleStates},
        {"keyboard menu navigation and back return to title prompt", testKeyboardMenuNavigationAndBackReturnToTitlePrompt},
        {"options flow changes settings and returns to menu", testOptionsFlowChangesSettingsAndReturnsToMenu},
        {"transfer selection raises open-transfer after fade", testTransferSelectionRaisesOpenTransferAfterFade},
        {"resort selection raises open-resort-loading after fade", testResortSelectionRaisesOpenResortLoadingAfterFade},
        {"trade selection raises open-trade-loading after fade", testTradeSelectionRaisesOpenTradeLoadingAfterFade},
        {"controller can drive start and menu navigation", testControllerCanDriveStartAndMenuNavigation},
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
