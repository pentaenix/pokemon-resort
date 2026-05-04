#include "ui/loading/LoadingScreenFactory.hpp"
#include "ui/loading/ResortTransferLoadingConfig.hpp"
#include "ui/loading/ResortTransferLoadingScreen.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

pr::WindowConfig testWindowConfig() {
    pr::WindowConfig config;
    config.virtual_width = 1280;
    config.virtual_height = 800;
    config.design_width = 1280;
    config.design_height = 800;
    return config;
}

fs::path repositoryRoot() {
    fs::path current = fs::current_path();
    while (!current.empty()) {
        if (fs::exists(current / "config" / "app.json") &&
            fs::exists(current / "assets" / "loading" / "boat.png")) {
            return current;
        }
        current = current.parent_path();
    }
    throw std::runtime_error("Could not locate repository root from " + fs::current_path().string());
}

struct SdlHarness {
    SdlHarness() {
        setenv("SDL_VIDEODRIVER", "dummy", 1);
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
        }
        if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
            throw std::runtime_error(std::string("IMG_Init failed: ") + IMG_GetError());
        }
        if (TTF_Init() != 0) {
            throw std::runtime_error(std::string("TTF_Init failed: ") + TTF_GetError());
        }

        window.reset(SDL_CreateWindow("resort-loading-test", 0, 0, 1280, 800, SDL_WINDOW_HIDDEN));
        if (!window) {
            throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        }
        renderer.reset(SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_SOFTWARE));
        if (!renderer) {
            throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
        }
        SDL_RenderSetLogicalSize(renderer.get(), 1280, 800);
    }

    ~SdlHarness() {
        renderer.reset();
        window.reset();
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
    }

    struct WindowDeleter {
        void operator()(SDL_Window* value) const {
            if (value) {
                SDL_DestroyWindow(value);
            }
        }
    };

    struct RendererDeleter {
        void operator()(SDL_Renderer* value) const {
            if (value) {
                SDL_DestroyRenderer(value);
            }
        }
    };

    std::unique_ptr<SDL_Window, WindowDeleter> window;
    std::unique_ptr<SDL_Renderer, RendererDeleter> renderer;
};

void testStateMachine() {
    auto loading = pr::createLoadingScreen(pr::LoadingScreenType::ResortTransfer, nullptr, testWindowConfig(), "", "");
    auto* screen = dynamic_cast<pr::ResortTransferLoadingScreen*>(loading.get());
    expect(screen != nullptr, "factory should create ResortTransferLoadingScreen for resort transfer loading type");

    expect(screen->debugState() == pr::ResortTransferLoadingState::WhiteIdle,
           "resort loading starts on a blank WhiteIdle page");
    expect(screen->acceptsAdvanceInput(),
           "WhiteIdle accepts advance input to start the enter animation");

    screen->onAdvancePressed();
    expect(screen->debugState() == pr::ResortTransferLoadingState::Intro,
           "advance from WhiteIdle starts Intro");
    expect(!screen->acceptsAdvanceInput(),
           "Intro ignores advance input until the loading loop");

    screen->update(1.05);
    expect(screen->debugState() == pr::ResortTransferLoadingState::LoadingLoop,
           "Intro settles into LoadingLoop after its staged animation");
    expect(screen->acceptsAdvanceInput(),
           "LoadingLoop accepts advance input to start ExitAnimation");

    screen->onAdvancePressed();
    expect(screen->debugState() == pr::ResortTransferLoadingState::Outro,
           "advance from LoadingLoop starts Outro");
    screen->update(1.05);
    expect(screen->debugState() == pr::ResortTransferLoadingState::WhiteIdle,
           "Outro returns to WhiteIdle after its staged animation");
    expect(screen->acceptsAdvanceInput(),
           "WhiteIdle accepts another advance after exit completes");

    screen->onAdvancePressed();
    expect(screen->debugState() == pr::ResortTransferLoadingState::Intro,
           "advance after a completed exit restarts Intro");

    screen->onBackPressed();
    expect(screen->consumeReturnToMenuRequest(),
           "cancel requests return to the main menu from the current animation state");
    expect(!screen->consumeReturnToMenuRequest(),
           "return-to-menu request is consumed once");
}

void testQuickBoatPassCompletesWithoutLoopPause() {
    auto loading = pr::createLoadingScreen(pr::LoadingScreenType::QuickBoatPass, nullptr, testWindowConfig(), "", "");
    auto* screen = dynamic_cast<pr::ResortTransferLoadingScreen*>(loading.get());
    expect(screen != nullptr, "factory should create ResortTransferLoadingScreen for quick boat pass test");
    expect(loading->loadingScreenType() == pr::LoadingScreenType::QuickBoatPass,
           "factory should preserve the requested quick boat pass loading type");

    screen->beginQuickPass();
    expect(screen->debugState() == pr::ResortTransferLoadingState::QuickPass,
           "quick boat pass starts the continuous fly-through state");
    screen->update(0.9);
    expect(screen->debugState() == pr::ResortTransferLoadingState::QuickPass,
           "quick boat pass does not stop in the LoadingLoop");
    expect(!screen->isLoadingAnimationComplete(),
           "quick boat pass is not complete until the fly-through finishes");
    screen->update(0.9);
    expect(screen->debugState() == pr::ResortTransferLoadingState::WhiteIdle,
           "quick boat pass returns to WhiteIdle after the fly-through");
    expect(screen->isLoadingAnimationComplete(),
           "quick boat pass reports completion after the fly-through");
}

void testQuickBoatPassWaitsOnOceanIfLoadRunsLong() {
    auto loading = pr::createLoadingScreen(pr::LoadingScreenType::QuickBoatPass, nullptr, testWindowConfig(), "", "");
    auto* screen = dynamic_cast<pr::ResortTransferLoadingScreen*>(loading.get());
    expect(screen != nullptr, "factory should create ResortTransferLoadingScreen for delayed quick boat pass test");

    screen->beginQuickPass(true);
    screen->update(2.2);
    expect(screen->debugState() == pr::ResortTransferLoadingState::QuickPass,
           "delayed quick boat pass keeps the ocean scene alive after the boat leaves");
    expect(!screen->isLoadingAnimationComplete(),
           "delayed quick boat pass does not finish before loading is marked complete");
    screen->markLoadingComplete();
    screen->update(0.9);
    expect(screen->debugState() == pr::ResortTransferLoadingState::WhiteIdle,
           "delayed quick boat pass finishes after loading completion moves the ocean away");
    expect(screen->isLoadingAnimationComplete(),
           "delayed quick boat pass reports completion after the ocean exit");
}

void testBoatAutoFlowCanSkipLoopPause() {
    auto loading = pr::createLoadingScreen(pr::LoadingScreenType::ResortTransfer, nullptr, testWindowConfig(), "", "");
    auto* screen = dynamic_cast<pr::ResortTransferLoadingScreen*>(loading.get());
    expect(screen != nullptr, "factory should create ResortTransferLoadingScreen for boat auto-flow test");

    screen->beginLoadingWithMessageKey("message_transport_pokemon", 0.0);
    expect(screen->debugState() == pr::ResortTransferLoadingState::Intro,
           "boat loading begins the intro immediately");
    screen->markLoadingComplete();
    expect(screen->debugState() == pr::ResortTransferLoadingState::Intro,
           "boat loading stays in Intro until the boat reaches the middle");
    screen->update(1.05);
    expect(screen->debugState() == pr::ResortTransferLoadingState::Outro,
           "boat loading skips the loop pause when work completed during Intro");
    screen->update(1.05);
    expect(screen->debugState() == pr::ResortTransferLoadingState::WhiteIdle,
           "boat loading returns to WhiteIdle after the outro");
    expect(screen->isLoadingAnimationComplete(),
           "boat loading reports completion after the outro");
}

void testBoatAutoFlowUsesConfiguredMinimumLoopByDefault() {
    auto loading = pr::createLoadingScreen(pr::LoadingScreenType::ResortTransfer, nullptr, testWindowConfig(), "", "");
    auto* screen = dynamic_cast<pr::ResortTransferLoadingScreen*>(loading.get());
    expect(screen != nullptr, "factory should create ResortTransferLoadingScreen for configured minimum-loop test");

    screen->beginLoadingWithMessageKey("message_transport_pokemon");
    screen->markLoadingComplete();
    screen->update(1.05);
    expect(screen->debugState() == pr::ResortTransferLoadingState::LoadingLoop,
           "boat loading uses the configured minimum loop by default");
    screen->update(0.5);
    expect(screen->debugState() == pr::ResortTransferLoadingState::LoadingLoop,
           "boat loading does not leave before the configured minimum loop passes");
    screen->update(0.6);
    expect(screen->debugState() == pr::ResortTransferLoadingState::Outro,
           "boat loading leaves once the configured minimum loop passes");
}

void testAutoFlowHonorsMinimumLoopDuration() {
    auto loading = pr::createLoadingScreen(pr::LoadingScreenType::ResortTransfer, nullptr, testWindowConfig(), "", "");
    auto* screen = dynamic_cast<pr::ResortTransferLoadingScreen*>(loading.get());
    expect(screen != nullptr, "factory should create ResortTransferLoadingScreen for minimum-loop test");

    screen->beginLoadingWithMessageKey("message_transport_pokemon", 2.0);
    screen->markLoadingComplete();
    screen->update(1.05);
    expect(screen->debugState() == pr::ResortTransferLoadingState::LoadingLoop,
           "auto-flow enters LoadingLoop when a minimum duration is requested");
    screen->update(1.0);
    expect(screen->debugState() == pr::ResortTransferLoadingState::LoadingLoop,
           "auto-flow holds LoadingLoop until the minimum duration passes");
    screen->update(1.05);
    expect(screen->debugState() == pr::ResortTransferLoadingState::Outro,
           "auto-flow starts Outro after the minimum LoadingLoop duration");
}

void testTemporalTradeDemoConfigLoads() {
    const fs::path root = repositoryRoot();
    const pr::ResortTransferLoadingConfig config = pr::loadResortTransferLoadingConfig(root.string());
    const auto trade = config.temporal.find("trade_button");
    expect(trade != config.temporal.end(),
           "loading_screen.json should define resort_transfer.temporal.trade_button for the temporary Trade demo");
    if (trade != config.temporal.end()) {
        expect(trade->second.message_key == "message_transport_pokemon",
               "temporary Trade demo should use the standard transport Pokemon message key");
        expect(trade->second.loading_type == pr::TemporalLoadingDemoType::QuickBoatPass,
               "temporary Trade demo should default to testing the quick boat pass loading type");
        expect(trade->second.simulated_load_duration_seconds == 0.0,
               "temporary Trade demo should default to a zero-second simulated load");
        expect(config.quick_pass.duration_seconds > 0.0,
               "quick boat pass duration should live in the reusable quick_pass config");
        expect(config.quick_pass.message.show_text,
               "quick boat pass should support authored message overlay via quick_pass.message.show_text");
        expect(config.quick_pass.message.align == pr::LoadingMessageHorizontalAlign::Left,
               "quick boat pass defaults should honor authored horizontal alignment (loading_screen.json text_align)");
        expect(config.minimum_loop_seconds == 1.0,
               "normal boat minimum loop duration should live in reusable loading config");
    }
}

void testRenderSmokeLoadsAssetsAndDrawsStates() {
    SdlHarness harness;
    const fs::path root = repositoryRoot();
    auto loading = pr::createLoadingScreen(
        pr::LoadingScreenType::ResortTransfer,
        harness.renderer.get(),
        testWindowConfig(),
        "",
        root.string());
    auto* screen = dynamic_cast<pr::ResortTransferLoadingScreen*>(loading.get());
    expect(screen != nullptr, "factory render smoke should create ResortTransferLoadingScreen");
    screen->setLoadingMessageKey("message_transport_pokemon");

    screen->render(harness.renderer.get());
    SDL_RenderPresent(harness.renderer.get());

    screen->onAdvancePressed();
    // Intro length is max(stage start+duration) from loading_screen.json (boat stage can exceed 1s).
    screen->update(0.70);
    screen->render(harness.renderer.get());
    SDL_RenderPresent(harness.renderer.get());

    screen->update(0.60);
    expect(screen->debugState() == pr::ResortTransferLoadingState::LoadingLoop,
           "render smoke should reach LoadingLoop after enter animation");
    screen->render(harness.renderer.get());
    SDL_RenderPresent(harness.renderer.get());

    screen->onAdvancePressed();
    screen->update(0.60);
    screen->render(harness.renderer.get());
    SDL_RenderPresent(harness.renderer.get());
}

} // namespace

int main() {
    try {
        testStateMachine();
        testQuickBoatPassCompletesWithoutLoopPause();
        testQuickBoatPassWaitsOnOceanIfLoadRunsLong();
        testBoatAutoFlowCanSkipLoopPause();
        testBoatAutoFlowUsesConfiguredMinimumLoopByDefault();
        testAutoFlowHonorsMinimumLoopDuration();
        testTemporalTradeDemoConfigLoads();
        testRenderSmokeLoadsAssetsAndDrawsStates();
    } catch (const std::exception& ex) {
        ++failures;
        std::cerr << "FAIL: " << ex.what() << '\n';
    }

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
