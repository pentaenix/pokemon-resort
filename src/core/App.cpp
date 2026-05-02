#include "core/App.hpp"
#include "core/assets/Assets.hpp"
#include "core/config/ConfigLoader.hpp"
#include "core/input/InputRouter.hpp"
#include "core/assets/PokeSpriteAssets.hpp"
#include "core/save/SavePaths.hpp"
#include "core/save/SaveLibrary.hpp"
#include "core/app/audio/AppAudioDirector.hpp"
#include "core/app/loading/AppLoadingCoordinator.hpp"
#include "core/app/AppPaths.hpp"
#include "core/app/screen/AppScreenCoordinator.hpp"
#include "core/app/persistence/UserSettingsPersistence.hpp"
#include "resort/services/PokemonResortService.hpp"
#include "ui/Screen.hpp"
#include "ui/TransferFlowCoordinator.hpp"
#include "ui/TitleScreen.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace pr {

namespace {

struct SdlQuit { void operator()(void*) const { SDL_Quit(); } };
struct ImgQuit { void operator()(void*) const { IMG_Quit(); } };
struct TtfQuit { void operator()(void*) const { TTF_Quit(); } };
struct WindowDestroy { void operator()(SDL_Window* p) const { if (p) SDL_DestroyWindow(p); } };
struct RendererDestroy { void operator()(SDL_Renderer* p) const { if (p) SDL_DestroyRenderer(p); } };

using WindowPtr = std::unique_ptr<SDL_Window, WindowDestroy>;
using RendererPtr = std::unique_ptr<SDL_Renderer, RendererDestroy>;

double clamp01(double value) {
    return std::max(0.0, std::min(1.0, value));
}

void renderBlackOverlay(SDL_Renderer* renderer, const WindowConfig& window, double alpha01) {
    if (!renderer || alpha01 <= 0.0) {
        return;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, static_cast<Uint8>(std::round(clamp01(alpha01) * 255.0)));
    const SDL_Rect full{0, 0, window.virtual_width, window.virtual_height};
    SDL_RenderFillRect(renderer, &full);
}

} // namespace

int clearTransferSaveCache(const char* config_path_override) {
    const std::string root = findProjectRoot();
    const std::string app_config_path = (fs::path(root) / "config" / "app.json").string();
    const std::string config_path = config_path_override
        ? config_path_override
        : (fs::path(root) / "config" / "title_screen.json").string();
    AppConfig app_config = loadAppConfigFromJson(app_config_path);
    TitleScreenConfig config = loadConfigFromJson(config_path);
    config.window = app_config.window;
    config.input = app_config.input;
    config.audio = app_config.audio;

    if (SDL_Init(0) != 0) {
        std::cerr << "Warning: SDL_Init failed while clearing save cache: " << SDL_GetError() << '\n';
    }

    const fs::path cache_path = resolveSaveDirectory(config.persistence, root) / "transfer_save_cache.json";
    std::error_code error;
    const bool removed = fs::remove(cache_path, error);
    SDL_Quit();

    if (error) {
        std::cerr << "Failed to clear transfer save cache at "
                  << cache_path << ": " << error.message() << '\n';
        return 1;
    }

    std::cout << (removed ? "Cleared" : "No cache found at")
              << " " << cache_path << '\n';
    return 0;
}

int runApplication(const char* argv0, const char* config_path_override) {
    std::string root = findProjectRoot();
    const std::string app_config_path = (fs::path(root) / "config" / "app.json").string();
    std::string config_path = config_path_override ? config_path_override : (fs::path(root) / "config" / "title_screen.json").string();
    AppConfig app_config = loadAppConfigFromJson(app_config_path);
    TitleScreenConfig config = loadConfigFromJson(config_path);
    config.window = app_config.window;
    config.input = app_config.input;
    config.audio = app_config.audio;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    std::unique_ptr<void, SdlQuit> sdl_guard(nullptr);

    if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) throw std::runtime_error(std::string("IMG_Init failed: ") + IMG_GetError());
    std::unique_ptr<void, ImgQuit> img_guard(nullptr);

    if (TTF_Init() != 0) throw std::runtime_error(std::string("TTF_Init failed: ") + TTF_GetError());
    std::unique_ptr<void, TtfQuit> ttf_guard(nullptr);

    const fs::path save_directory = resolveSaveDirectory(config.persistence, root);
    const fs::path save_file_path = save_directory / config.persistence.save_file_name;
    const fs::path backup_file_path = save_directory / config.persistence.backup_file_name;

    WindowPtr window(SDL_CreateWindow(
        config.window.title.c_str(),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        config.window.width,
        config.window.height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE));
    if (!window) throw std::runtime_error(std::string("Failed to create window: ") + SDL_GetError());

    RendererPtr renderer(SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC));
    if (!renderer) throw std::runtime_error(std::string("Failed to create renderer: ") + SDL_GetError());

    if (SDL_RenderSetLogicalSize(renderer.get(), config.window.virtual_width, config.window.virtual_height) != 0) {
        throw std::runtime_error(std::string("Failed to set renderer logical size: ") + SDL_GetError());
    }

    Assets assets = loadAssets(renderer.get(), config, root);
    TitleScreen title_screen(config, std::move(assets));
    AppLoadingCoordinator loading(
        renderer.get(),
        config.window,
        config.assets.font,
        root);
    SaveLibrary save_library(root, save_directory.string(), argv0);
    std::shared_ptr<PokeSpriteAssets> poke_sprite_assets = PokeSpriteAssets::create(root);
    std::unique_ptr<resort::PokemonResortService> pokemon_resort_service;
    try {
        pokemon_resort_service = std::make_unique<resort::PokemonResortService>(
            resortProfileDatabasePath(save_directory, config.persistence));
        pokemon_resort_service->ensureProfile("default");
    } catch (const std::exception& ex) {
        std::cerr << "Warning: could not initialize Pokemon Resort profile storage: "
                  << ex.what() << '\n';
    }
    TransferFlowCoordinator transfer_flow(
        renderer.get(),
        config.window,
        config.assets.font,
        root,
        poke_sprite_assets,
        save_library,
        argv0,
        pokemon_resort_service ? pokemon_resort_service.get() : nullptr);
    UserSettingsPersistence user_settings_persistence(
        config.persistence.save_options,
        save_file_path,
        backup_file_path);
    UserSettings user_settings;
    if (user_settings_persistence.load(user_settings)) {
        title_screen.applyUserSettings(user_settings);
    }

    AppAudioDirector audio(root, config.audio);
    AppScreenCoordinator screen_coordinator(title_screen, loading, transfer_flow);

    bool running = true;
    Uint64 last_counter = SDL_GetPerformanceCounter();
    InputRouter input_router;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
                continue;
            }

            input_router.handleEvent(event, config.input, screen_coordinator.activeInput());
        }

        Uint64 now = SDL_GetPerformanceCounter();
        double dt = static_cast<double>(now - last_counter) / static_cast<double>(SDL_GetPerformanceFrequency());
        last_counter = now;

        input_router.update(dt, screen_coordinator.activeInput());
        screen_coordinator.update(dt);

        audio.updateMusic(dt, screen_coordinator.musicRequest());
        audio.playSfx(screen_coordinator.consumeSfxRequests(), screen_coordinator.sfxVolume());
        if (auto settings = screen_coordinator.consumeUserSettingsSaveRequest()) {
            user_settings_persistence.save(*settings);
        }

        if (Screen* screen = screen_coordinator.activeScreen()) {
            screen->render(renderer.get());
        }
        renderBlackOverlay(renderer.get(), config.window, screen_coordinator.transitionOverlayAlpha());
        SDL_RenderPresent(renderer.get());
    }

    return 0;
}

} // namespace pr
