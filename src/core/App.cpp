#include "core/App.hpp"
#include "core/Assets.hpp"
#include "core/Audio.hpp"
#include "core/ConfigLoader.hpp"
#include "core/InputRouter.hpp"
#include "core/PokeSpriteAssets.hpp"
#include "core/SavePaths.hpp"
#include "core/SaveDataStore.hpp"
#include "core/SaveLibrary.hpp"
#include "resort/services/PokemonResortService.hpp"
#include "ui/Screen.hpp"
#include "ui/ScreenInput.hpp"
#include "ui/TransferFlowCoordinator.hpp"
#include "ui/TitleScreen.hpp"
#include "ui/loading/LoadingScreenFactory.hpp"
#include "ui/loading/ResortTransferLoadingConfig.hpp"

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
#include <vector>

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

enum class ActiveScreen { Title, ResortLoading, TransferFlow };
enum class LoadingReturnTarget { ResortTitle, TradeTitle, TransferTickets };
enum class ActiveMusicTrack { None, Menu, Transfer };
enum class AppTransitionPhase {
    None,
    SaveSoundDelay,
    FadeOutToLoading,
    FadeInLoading,
    LoadingActive,
    FadeOutToDestination,
    FadeInDestination
};

constexpr double kSaveSoundPreTransitionDelaySeconds = 0.5;
constexpr double kQuickTransitionFadeSeconds = 0.25;

double clamp01(double value) {
    return std::max(0.0, std::min(1.0, value));
}

double phaseProgress(double elapsed_seconds, double duration_seconds) {
    if (duration_seconds <= 1e-6) {
        return 1.0;
    }
    return clamp01(elapsed_seconds / duration_seconds);
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

std::string findProjectRoot() {
    // Support running the binary from the repo root (e.g. ./build/title_screen_demo) where cwd is
    // title_screen_demo/ but config lives in title_screen_demo/pokemon-resort/config/.
    std::vector<fs::path> candidates{
        fs::current_path(),
        fs::current_path() / "pokemon-resort",
        fs::current_path().parent_path(),
        fs::current_path().parent_path() / "pokemon-resort",
        fs::current_path().parent_path().parent_path(),
    };
    for (const auto& c : candidates) {
        if (fs::exists(c / "config" / "app.json") &&
            fs::exists(c / "config" / "title_screen.json")) {
            std::error_code ec;
            const fs::path canon = fs::weakly_canonical(c, ec);
            return ec ? c.string() : canon.string();
        }
    }
    throw std::runtime_error("Could not locate project root with config/app.json and config/title_screen.json");
}

std::string resolveSaveDirectory(const TitleScreenConfig& config, const std::string& root) {
    char* pref_path = SDL_GetPrefPath(
        config.persistence.organization.c_str(),
        config.persistence.application.c_str());
    if (pref_path) {
        std::string directory(pref_path);
        SDL_free(pref_path);
        return directory;
    }

    return (fs::path(root) / "save").string();
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

    const fs::path cache_path = fs::path(resolveSaveDirectory(config, root)) / "transfer_save_cache.json";
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

    const fs::path save_directory = resolveSaveDirectory(config, root);
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
    std::unique_ptr<LoadingScreenBase> resort_loading_screen = createLoadingScreen(
        LoadingScreenType::ResortTransfer, renderer.get(), config.window, config.assets.font, root);
    std::unique_ptr<LoadingScreenBase> pokeball_menu_loading_screen = createLoadingScreen(
        LoadingScreenType::Pokeball, renderer.get(), config.window, config.assets.font, root);
    std::unique_ptr<LoadingScreenBase> quick_boat_pass_screen = createLoadingScreen(
        LoadingScreenType::QuickBoatPass, renderer.get(), config.window, config.assets.font, root);
    LoadingScreenBase* active_loading_screen = resort_loading_screen.get();
    const ResortTransferLoadingConfig resort_loading_config = loadResortTransferLoadingConfig(root);
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
    if (config.persistence.save_options) {
        std::string load_error;
        std::string loaded_from_path;
        if (auto save_data = loadSaveData(
                save_file_path.string(),
                backup_file_path.string(),
                &loaded_from_path,
                &load_error)) {
            title_screen.applyUserSettings(save_data->options);
            if (!loaded_from_path.empty() && loaded_from_path != save_file_path.string()) {
                std::cerr << "Warning: primary save unavailable; loaded fallback save from "
                          << loaded_from_path << '\n';
            }
        } else if (!load_error.empty()) {
            std::cerr << "Warning: could not load save data from "
                      << save_file_path << " or backup " << backup_file_path
                      << ": " << load_error << '\n';
        }
    }

    AudioController audio;
    ActiveMusicTrack loaded_music_track = ActiveMusicTrack::None;
    const fs::path menu_music_path = fs::path(root) / config.audio.menu_music;
    if (audio.loadMusic(menu_music_path.string())) {
        loaded_music_track = ActiveMusicTrack::Menu;
    } else {
        std::cerr << "Warning: could not load menu music at " << menu_music_path << '\n';
    }
    const fs::path button_sfx_path = fs::path(root) / config.audio.button_sfx;
    if (!audio.loadButtonSfx(button_sfx_path.string())) {
        std::cerr << "Warning: could not load button sfx at " << button_sfx_path << '\n';
    }
    const fs::path rip_sfx_path = fs::path(root) / config.audio.rip_sfx;
    if (!audio.loadRipSfx(rip_sfx_path.string())) {
        std::cerr << "Warning: could not load rip sfx at " << rip_sfx_path << '\n';
    }
    const fs::path ui_move_sfx_path = fs::path(root) / config.audio.ui_move_sfx;
    if (!audio.loadUiMoveSfx(ui_move_sfx_path.string())) {
        std::cerr << "Warning: could not load ui move sfx at " << ui_move_sfx_path << '\n';
    }
    const fs::path pickup_sfx_path = fs::path(root) / config.audio.pickup_sfx;
    if (!audio.loadPickupSfx(pickup_sfx_path.string())) {
        std::cerr << "Warning: could not load pickup sfx at " << pickup_sfx_path << '\n';
    }
    const fs::path putdown_sfx_path = fs::path(root) / config.audio.putdown_sfx;
    if (!audio.loadPutdownSfx(putdown_sfx_path.string())) {
        std::cerr << "Warning: could not load putdown sfx at " << putdown_sfx_path << '\n';
    }
    const fs::path error_sfx_path = fs::path(root) / config.audio.error_sfx;
    if (!audio.loadErrorSfx(error_sfx_path.string())) {
        std::cerr << "Warning: could not load error sfx at " << error_sfx_path << '\n';
    }
    const fs::path save_sfx_path = fs::path(root) / config.audio.save_sfx;
    if (!audio.loadSaveSfx(save_sfx_path.string())) {
        std::cerr << "Warning: could not load save sfx at " << save_sfx_path << '\n';
    }

    bool running = true;
    Uint64 last_counter = SDL_GetPerformanceCounter();
    bool music_playing = false;
    double transfer_music_elapsed_seconds = 0.0;
    ActiveScreen active_screen = ActiveScreen::Title;
    LoadingReturnTarget loading_return_target = LoadingReturnTarget::ResortTitle;
    TemporalLoadingDemoType temporal_loading_type = TemporalLoadingDemoType::QuickBoatPass;
    double temporal_simulated_load_duration_seconds = 0.0;
    double temporal_loading_elapsed_seconds = 0.0;
    bool temporal_loading_completion_sent = false;
    InputRouter input_router;
    bool title_button_sfx_requested = false;
    bool title_user_settings_save_requested = false;
    bool transfer_save_sfx_requested = false;
    AppTransitionPhase transition_phase = AppTransitionPhase::None;
    double transition_elapsed_seconds = 0.0;
    double transition_overlay_alpha = 0.0;
    const auto active_screen_instance = [&]() -> Screen* {
        switch (active_screen) {
            case ActiveScreen::Title:
                return &title_screen;
            case ActiveScreen::ResortLoading:
                return active_loading_screen;
            case ActiveScreen::TransferFlow:
                return transfer_flow.activeScreen();
        }
        return nullptr;
    };
    const auto active_input = [&]() -> ScreenInput* {
        if (transition_phase != AppTransitionPhase::None) {
            return nullptr;
        }
        return active_screen_instance();
    };
    const auto start_successful_save_quick_transition = [&]() {
        transfer_save_sfx_requested = true;
        loading_return_target = LoadingReturnTarget::TransferTickets;
        transition_phase = AppTransitionPhase::SaveSoundDelay;
        transition_elapsed_seconds = 0.0;
        transition_overlay_alpha = 0.0;
    };
    const auto finish_loading_transition = [&]() {
        if (loading_return_target == LoadingReturnTarget::TransferTickets) {
            transfer_flow.completeSuccessfulSaveReturnToTickets();
            active_screen = ActiveScreen::TransferFlow;
        } else if (loading_return_target == LoadingReturnTarget::TradeTitle) {
            title_screen.returnToMainMenuFromTradeLoading();
            active_screen = ActiveScreen::Title;
        } else {
            title_screen.returnToMainMenuFromResort();
            active_screen = ActiveScreen::Title;
        }
    };

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
                continue;
            }

            input_router.handleEvent(event, config.input, active_input());
        }

        Uint64 now = SDL_GetPerformanceCounter();
        double dt = static_cast<double>(now - last_counter) / static_cast<double>(SDL_GetPerformanceFrequency());
        last_counter = now;

        input_router.update(dt, active_input());

        if (active_screen == ActiveScreen::Title) {
            title_screen.update(dt);
            for (TitleScreenEvent event : title_screen.consumeEvents()) {
                switch (event) {
                    case TitleScreenEvent::ButtonSfxRequested:
                        title_button_sfx_requested = true;
                        break;
                    case TitleScreenEvent::UserSettingsSaveRequested:
                        title_user_settings_save_requested = true;
                        break;
                    case TitleScreenEvent::OpenResortLoadingRequested:
                        loading_return_target = LoadingReturnTarget::ResortTitle;
                        active_loading_screen = resort_loading_screen.get();
                        resort_loading_screen->enterWithMessageKey("message_transport_pokemon");
                        active_screen = ActiveScreen::ResortLoading;
                        break;
                    case TitleScreenEvent::OpenTradeLoadingRequested:
                        loading_return_target = LoadingReturnTarget::TradeTitle;
                        temporal_loading_elapsed_seconds = 0.0;
                        temporal_loading_completion_sent = false;
                        if (auto it = resort_loading_config.temporal.find("trade_button"); it != resort_loading_config.temporal.end()) {
                            temporal_loading_type = it->second.loading_type;
                            temporal_simulated_load_duration_seconds = it->second.simulated_load_duration_seconds;
                            if (temporal_loading_type == TemporalLoadingDemoType::Pokeball) {
                                active_loading_screen = pokeball_menu_loading_screen.get();
                                active_loading_screen->enter();
                            } else if (temporal_loading_type == TemporalLoadingDemoType::QuickBoatPass) {
                                active_loading_screen = quick_boat_pass_screen.get();
                                active_loading_screen->beginQuickPass(temporal_simulated_load_duration_seconds > 0.0);
                            } else {
                                active_loading_screen = resort_loading_screen.get();
                                active_loading_screen->beginLoadingWithMessageKey(it->second.message_key);
                            }
                        } else {
                            temporal_loading_type = TemporalLoadingDemoType::QuickBoatPass;
                            temporal_simulated_load_duration_seconds = 0.0;
                            active_loading_screen = quick_boat_pass_screen.get();
                            active_loading_screen->beginQuickPass();
                        }
                        if (temporal_simulated_load_duration_seconds <= 0.0 &&
                            temporal_loading_type != TemporalLoadingDemoType::QuickBoatPass) {
                            active_loading_screen->markLoadingComplete();
                            temporal_loading_completion_sent = true;
                        } else if (temporal_simulated_load_duration_seconds <= 0.0) {
                            temporal_loading_completion_sent = true;
                        }
                        active_screen = ActiveScreen::ResortLoading;
                        break;
                    case TitleScreenEvent::OpenTransferRequested:
                        transfer_flow.beginTicketScan();
                        active_screen = ActiveScreen::TransferFlow;
                        break;
                }
            }
        } else if (active_screen == ActiveScreen::ResortLoading) {
            active_loading_screen->update(dt);
            if (loading_return_target == LoadingReturnTarget::TradeTitle && !temporal_loading_completion_sent) {
                temporal_loading_elapsed_seconds += dt;
                if (temporal_loading_elapsed_seconds >= temporal_simulated_load_duration_seconds) {
                    active_loading_screen->markLoadingComplete();
                    temporal_loading_completion_sent = true;
                }
            }
            if (transition_phase == AppTransitionPhase::None && active_loading_screen->consumeReturnToMenuRequest()) {
                if (loading_return_target == LoadingReturnTarget::TradeTitle) {
                    title_screen.returnToMainMenuFromTradeLoading();
                    active_screen = ActiveScreen::Title;
                } else if (loading_return_target == LoadingReturnTarget::TransferTickets) {
                    active_screen = ActiveScreen::TransferFlow;
                } else {
                    title_screen.returnToMainMenuFromResort();
                    active_screen = ActiveScreen::Title;
                }
            } else if (transition_phase == AppTransitionPhase::None &&
                       loading_return_target == LoadingReturnTarget::TradeTitle &&
                       active_loading_screen->isLoadingAnimationComplete()) {
                title_screen.returnToMainMenuFromTradeLoading();
                active_screen = ActiveScreen::Title;
            } else if (transition_phase == AppTransitionPhase::None &&
                       loading_return_target == LoadingReturnTarget::TransferTickets &&
                       active_loading_screen->isLoadingAnimationComplete()) {
                active_screen = ActiveScreen::TransferFlow;
            }
        } else if (active_screen == ActiveScreen::TransferFlow) {
            transfer_flow.update(dt);
            if (transfer_flow.consumeSuccessfulSaveReturnToTicketsRequest()) {
                start_successful_save_quick_transition();
            } else if (transfer_flow.consumeReturnToTitleRequest()) {
                title_screen.returnToMainMenuFromTransfer();
                active_screen = ActiveScreen::Title;
            }
        }

        if (transition_phase != AppTransitionPhase::None) {
            transition_elapsed_seconds += dt;
            switch (transition_phase) {
                case AppTransitionPhase::SaveSoundDelay:
                    transition_overlay_alpha = 0.0;
                    if (transition_elapsed_seconds >= kSaveSoundPreTransitionDelaySeconds) {
                        transition_phase = AppTransitionPhase::FadeOutToLoading;
                        transition_elapsed_seconds = 0.0;
                    }
                    break;
                case AppTransitionPhase::FadeOutToLoading: {
                    const double t = phaseProgress(transition_elapsed_seconds, kQuickTransitionFadeSeconds);
                    transition_overlay_alpha = t;
                    if (t >= 1.0) {
                        active_loading_screen = quick_boat_pass_screen.get();
                        active_loading_screen->beginQuickPass();
                        active_screen = ActiveScreen::ResortLoading;
                        transition_phase = AppTransitionPhase::FadeInLoading;
                        transition_elapsed_seconds = 0.0;
                        transition_overlay_alpha = 1.0;
                    }
                    break;
                }
                case AppTransitionPhase::FadeInLoading: {
                    const double t = phaseProgress(transition_elapsed_seconds, kQuickTransitionFadeSeconds);
                    transition_overlay_alpha = 1.0 - t;
                    if (t >= 1.0) {
                        transition_phase = AppTransitionPhase::LoadingActive;
                        transition_elapsed_seconds = 0.0;
                        transition_overlay_alpha = 0.0;
                    }
                    break;
                }
                case AppTransitionPhase::LoadingActive:
                    transition_overlay_alpha = 0.0;
                    if (active_loading_screen && active_loading_screen->isLoadingAnimationComplete()) {
                        transition_phase = AppTransitionPhase::FadeOutToDestination;
                        transition_elapsed_seconds = 0.0;
                    }
                    break;
                case AppTransitionPhase::FadeOutToDestination: {
                    const double t = phaseProgress(transition_elapsed_seconds, kQuickTransitionFadeSeconds);
                    transition_overlay_alpha = t;
                    if (t >= 1.0) {
                        finish_loading_transition();
                        transition_phase = AppTransitionPhase::FadeInDestination;
                        transition_elapsed_seconds = 0.0;
                        transition_overlay_alpha = 1.0;
                    }
                    break;
                }
                case AppTransitionPhase::FadeInDestination: {
                    const double t = phaseProgress(transition_elapsed_seconds, kQuickTransitionFadeSeconds);
                    transition_overlay_alpha = 1.0 - t;
                    if (t >= 1.0) {
                        transition_phase = AppTransitionPhase::None;
                        transition_elapsed_seconds = 0.0;
                        transition_overlay_alpha = 0.0;
                    }
                    break;
                }
                case AppTransitionPhase::None:
                    break;
            }
        }

        const bool wants_menu_music = active_screen == ActiveScreen::Title && title_screen.wantsMenuMusic();
        const bool transition_returns_to_transfer =
            transition_phase != AppTransitionPhase::None &&
            loading_return_target == LoadingReturnTarget::TransferTickets;
        const bool wants_transfer_music =
            (active_screen == ActiveScreen::TransferFlow || transition_returns_to_transfer) &&
            transfer_flow.hasTransferMusic();
        const ActiveMusicTrack desired_music_track =
            wants_menu_music
                ? ActiveMusicTrack::Menu
                : (wants_transfer_music ? ActiveMusicTrack::Transfer : ActiveMusicTrack::None);

        if (desired_music_track == ActiveMusicTrack::Menu) {
            if (loaded_music_track != ActiveMusicTrack::Menu) {
                audio.stopMusic();
                music_playing = false;
                if (audio.loadMusic(menu_music_path.string())) {
                    loaded_music_track = ActiveMusicTrack::Menu;
                } else {
                    loaded_music_track = ActiveMusicTrack::None;
                    std::cerr << "Warning: could not load menu music at " << menu_music_path << '\n';
                }
            }
            transfer_music_elapsed_seconds = 0.0;
            if (loaded_music_track == ActiveMusicTrack::Menu && audio.isMusicLoaded()) {
                audio.setMusicVolume(title_screen.musicVolume());
                if (!music_playing) {
                    audio.playMusicLoop();
                    music_playing = true;
                }
            }
        } else if (desired_music_track == ActiveMusicTrack::Transfer) {
            if (loaded_music_track != ActiveMusicTrack::Transfer) {
                audio.stopMusic();
                music_playing = false;
                transfer_music_elapsed_seconds = 0.0;
                const fs::path transfer_music_path = fs::path(root) / transfer_flow.musicPath();
                if (audio.loadMusic(transfer_music_path.string())) {
                    loaded_music_track = ActiveMusicTrack::Transfer;
                } else {
                    loaded_music_track = ActiveMusicTrack::None;
                    std::cerr << "Warning: could not load transfer music at " << transfer_music_path << '\n';
                }
            }
            if (loaded_music_track == ActiveMusicTrack::Transfer && audio.isMusicLoaded()) {
                transfer_music_elapsed_seconds += dt;
                const double silence_seconds = std::max(0.0, transfer_flow.musicSilenceSeconds());
                const double fade_in_seconds = std::max(0.0, transfer_flow.musicFadeInSeconds());
                double volume_scale = 0.0;
                if (transfer_music_elapsed_seconds >= silence_seconds) {
                    volume_scale = fade_in_seconds <= 0.0
                        ? 1.0
                        : clamp01((transfer_music_elapsed_seconds - silence_seconds) / fade_in_seconds);
                }
                audio.setMusicVolume(static_cast<float>(
                    static_cast<double>(title_screen.musicVolume()) * volume_scale));
                if (!music_playing) {
                    audio.playMusicLoop();
                    music_playing = true;
                }
            }
        } else if (music_playing) {
            audio.stopMusic();
            music_playing = false;
            transfer_music_elapsed_seconds = 0.0;
        }

        audio.setSfxVolume(title_screen.sfxVolume());
        const bool transfer_button_sfx_requested = transfer_flow.consumeButtonSfxRequest();
        if (title_button_sfx_requested || transfer_button_sfx_requested) {
            audio.playButtonSfx();
        }
        title_button_sfx_requested = false;
        if (transfer_flow.consumeUiMoveSfxRequest()) {
            audio.playUiMoveSfx();
        }
        if (transfer_flow.consumeRipSfxRequest()) {
            audio.playRipSfx();
        }
        if (transfer_flow.consumePickupSfxRequest()) {
            audio.playPickupSfx();
        }
        if (transfer_flow.consumePutdownSfxRequest()) {
            audio.playPutdownSfx();
        }
        if (transfer_flow.consumeErrorSfxRequest()) {
            audio.playErrorSfx();
        }
        if (transfer_save_sfx_requested) {
            audio.playSaveSfx();
        }
        transfer_save_sfx_requested = false;
        if (config.persistence.save_options && title_user_settings_save_requested) {
            SaveData save_data;
            save_data.options = title_screen.currentUserSettings();
            std::string save_error;
            if (!saveSaveDataAtomic(
                    save_file_path.string(),
                    backup_file_path.string(),
                    save_data,
                    &save_error)) {
                std::cerr << "Warning: could not save data to " << save_file_path
                          << " with backup " << backup_file_path
                          << ": " << save_error << '\n';
            }
        }
        title_user_settings_save_requested = false;

        if (Screen* screen = active_screen_instance()) {
            screen->render(renderer.get());
        }
        renderBlackOverlay(renderer.get(), config.window, transition_overlay_alpha);
        SDL_RenderPresent(renderer.get());
    }

    return 0;
}

} // namespace pr
