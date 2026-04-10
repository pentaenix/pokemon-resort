#include "core/App.hpp"
#include "core/Assets.hpp"
#include "core/Audio.hpp"
#include "core/ConfigLoader.hpp"
#include "core/InputBindings.hpp"
#include "core/SaveDataStore.hpp"
#include "ui/TransferTicketSandboxScreen.hpp"
#include "ui/TitleScreen.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
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

enum class ActiveScreen {
    Title,
    TransferTicketSandbox
};

std::string findProjectRoot() {
    std::vector<fs::path> candidates {
        fs::current_path(),
        fs::current_path().parent_path(),
        fs::current_path().parent_path().parent_path()
    };
    for (const auto& c : candidates) {
        if (fs::exists(c / "config" / "title_screen.json")) return c.string();
    }
    throw std::runtime_error("Could not locate project root with config/title_screen.json");
}

bool mapWindowToLogical(
    int window_x,
    int window_y,
    int window_w,
    int window_h,
    int logical_w,
    int logical_h,
    int& logical_x,
    int& logical_y) {
    if (window_w <= 0 || window_h <= 0 || logical_w <= 0 || logical_h <= 0) {
        return false;
    }

    const double scale = std::min(
        static_cast<double>(window_w) / static_cast<double>(logical_w),
        static_cast<double>(window_h) / static_cast<double>(logical_h));

    const double viewport_w = static_cast<double>(logical_w) * scale;
    const double viewport_h = static_cast<double>(logical_h) * scale;
    const double viewport_x = (static_cast<double>(window_w) - viewport_w) * 0.5;
    const double viewport_y = (static_cast<double>(window_h) - viewport_h) * 0.5;

    if (window_x < viewport_x ||
        window_y < viewport_y ||
        window_x >= viewport_x + viewport_w ||
        window_y >= viewport_y + viewport_h) {
        return false;
    }

    logical_x = static_cast<int>((static_cast<double>(window_x) - viewport_x) / scale);
    logical_y = static_cast<int>((static_cast<double>(window_y) - viewport_y) / scale);
    return true;
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

int runApplication(const char* argv0, const char* config_path_override) {
    std::string root = findProjectRoot();
    std::string config_path = config_path_override ? config_path_override : (fs::path(root) / "config" / "title_screen.json").string();
    TitleScreenConfig config = loadConfigFromJson(config_path);

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

    if (SDL_RenderSetLogicalSize(
            renderer.get(),
            config.window.virtual_width,
            config.window.virtual_height) != 0) {
        throw std::runtime_error(std::string("Failed to set renderer logical size: ") + SDL_GetError());
    }

    Assets assets = loadAssets(renderer.get(), config, root);
    TitleScreen title_screen(config, std::move(assets));
    std::unique_ptr<TransferTicketSandboxScreen> transfer_ticket_sandbox;
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
    const fs::path menu_music_path = fs::path(root) / config.audio.menu_music;
    if (!audio.loadMusic(menu_music_path.string())) {
        std::cerr << "Warning: could not load menu music at " << menu_music_path << '\n';
    }
    const fs::path button_sfx_path = fs::path(root) / config.audio.button_sfx;
    if (!audio.loadButtonSfx(button_sfx_path.string())) {
        std::cerr << "Warning: could not load button sfx at " << button_sfx_path << '\n';
    }

    bool running = true;
    Uint64 last_counter = SDL_GetPerformanceCounter();
    bool menu_music_playing = false;
    ActiveScreen active_screen = ActiveScreen::Title;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
                continue;
            }

            if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                const SDL_Keycode key = event.key.keysym.sym;
                if (matchesBinding(key, config.input.navigate_up_keys)) {
                    if (active_screen == ActiveScreen::Title) {
                        title_screen.onNavigate(-1);
                    } else {
                        if (transfer_ticket_sandbox) {
                            transfer_ticket_sandbox->onNavigate(-1);
                        }
                    }
                    continue;
                }
                if (matchesBinding(key, config.input.navigate_down_keys)) {
                    if (active_screen == ActiveScreen::Title) {
                        title_screen.onNavigate(1);
                    } else {
                        if (transfer_ticket_sandbox) {
                            transfer_ticket_sandbox->onNavigate(1);
                        }
                    }
                    continue;
                }
                if (matchesBinding(key, config.input.back_keys)) {
                    if (active_screen == ActiveScreen::Title) {
                        title_screen.onBackPressed();
                    } else {
                        if (transfer_ticket_sandbox) {
                            transfer_ticket_sandbox->onBackPressed();
                        }
                    }
                    continue;
                }
                if (matchesBinding(key, config.input.forward_keys)) {
                    if (active_screen == ActiveScreen::Title) {
                        if (title_screen.acceptsAdvanceInput()) {
                            title_screen.onAdvancePressed();
                        }
                    } else {
                        if (transfer_ticket_sandbox) {
                            transfer_ticket_sandbox->onAdvancePressed();
                        }
                    }
                    continue;
                }
            }

            if (event.type == SDL_MOUSEMOTION && config.input.accept_mouse) {
                if (active_screen == ActiveScreen::TransferTicketSandbox) {
                    int window_w = 0;
                    int window_h = 0;
                    SDL_GetWindowSize(window.get(), &window_w, &window_h);

                    int logical_x = 0;
                    int logical_y = 0;
                    if (mapWindowToLogical(
                            event.motion.x,
                            event.motion.y,
                            window_w,
                            window_h,
                            config.window.virtual_width,
                            config.window.virtual_height,
                            logical_x,
                            logical_y)) {
                        if (transfer_ticket_sandbox) {
                            transfer_ticket_sandbox->handlePointerMoved(logical_x, logical_y);
                        }
                    }
                }
                continue;
            }

            if (event.type == SDL_MOUSEBUTTONDOWN && config.input.accept_mouse) {
                int window_w = 0;
                int window_h = 0;
                SDL_GetWindowSize(window.get(), &window_w, &window_h);

                int logical_x = 0;
                int logical_y = 0;
                if (mapWindowToLogical(
                        event.button.x,
                        event.button.y,
                        window_w,
                        window_h,
                        config.window.virtual_width,
                        config.window.virtual_height,
                        logical_x,
                        logical_y)) {
                    if (active_screen == ActiveScreen::Title) {
                        title_screen.handlePointerPressed(logical_x, logical_y);
                    } else {
                        if (transfer_ticket_sandbox) {
                            transfer_ticket_sandbox->handlePointerPressed(logical_x, logical_y);
                        }
                    }
                }
                continue;
            }

            if (event.type == SDL_MOUSEBUTTONUP && config.input.accept_mouse) {
                if (active_screen == ActiveScreen::TransferTicketSandbox) {
                    int window_w = 0;
                    int window_h = 0;
                    SDL_GetWindowSize(window.get(), &window_w, &window_h);

                    int logical_x = 0;
                    int logical_y = 0;
                    if (mapWindowToLogical(
                            event.button.x,
                            event.button.y,
                            window_w,
                            window_h,
                            config.window.virtual_width,
                            config.window.virtual_height,
                            logical_x,
                            logical_y)) {
                        if (transfer_ticket_sandbox) {
                            transfer_ticket_sandbox->handlePointerReleased(logical_x, logical_y);
                        }
                    }
                }
                continue;
            }

            if (event.type == SDL_CONTROLLERBUTTONDOWN && config.input.accept_controller) {
                switch (event.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:
                        if (active_screen == ActiveScreen::Title) {
                            title_screen.onNavigate(-1);
                        } else {
                            if (transfer_ticket_sandbox) {
                                transfer_ticket_sandbox->onNavigate(-1);
                            }
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                        if (active_screen == ActiveScreen::Title) {
                            title_screen.onNavigate(1);
                        } else {
                            if (transfer_ticket_sandbox) {
                                transfer_ticket_sandbox->onNavigate(1);
                            }
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_A:
                        if (active_screen == ActiveScreen::Title) {
                            if (title_screen.acceptsAdvanceInput()) {
                                title_screen.onAdvancePressed();
                            }
                        } else {
                            if (transfer_ticket_sandbox) {
                                transfer_ticket_sandbox->onAdvancePressed();
                            }
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_B:
                        if (active_screen == ActiveScreen::Title) {
                            title_screen.onBackPressed();
                        } else {
                            if (transfer_ticket_sandbox) {
                                transfer_ticket_sandbox->onBackPressed();
                            }
                        }
                        break;
                    default:
                        break;
                }
            }
        }

        Uint64 now = SDL_GetPerformanceCounter();
        double dt = static_cast<double>(now - last_counter) / static_cast<double>(SDL_GetPerformanceFrequency());
        last_counter = now;

        if (active_screen == ActiveScreen::Title) {
            title_screen.update(dt);
            if (title_screen.consumeOpenTransferSandboxRequest()) {
                if (!transfer_ticket_sandbox) {
                    transfer_ticket_sandbox = std::make_unique<TransferTicketSandboxScreen>(
                        renderer.get(),
                        config.window,
                        config.assets.font,
                        root);
                }
                transfer_ticket_sandbox->enter();
                active_screen = ActiveScreen::TransferTicketSandbox;
            }
        } else {
            transfer_ticket_sandbox->update(dt);
            if (transfer_ticket_sandbox->consumeReturnToMainMenuRequest()) {
                title_screen.returnToMainMenuFromTransferSandbox();
                active_screen = ActiveScreen::Title;
            }
        }

        const bool wants_menu_music =
            active_screen == ActiveScreen::Title && title_screen.wantsMenuMusic();

        if (wants_menu_music && audio.isMusicLoaded()) {
            audio.setMusicVolume(title_screen.musicVolume());
            if (!menu_music_playing) {
                audio.playMusicLoop();
                menu_music_playing = true;
            }
        } else if (menu_music_playing) {
            audio.stopMusic();
            menu_music_playing = false;
        }

        audio.setSfxVolume(title_screen.sfxVolume());
        if ((active_screen == ActiveScreen::Title && title_screen.consumeButtonSfxRequest()) ||
            (active_screen == ActiveScreen::TransferTicketSandbox &&
             transfer_ticket_sandbox &&
             transfer_ticket_sandbox->consumeButtonSfxRequest())) {
            audio.playButtonSfx();
        }
        if (config.persistence.save_options && title_screen.consumeUserSettingsSaveRequest()) {
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

        if (active_screen == ActiveScreen::Title) {
            title_screen.render(renderer.get());
        } else {
            transfer_ticket_sandbox->render(renderer.get());
        }
        SDL_RenderPresent(renderer.get());
    }

    return 0;
}

} // namespace pr
