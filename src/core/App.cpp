#include "core/App.hpp"
#include "core/Assets.hpp"
#include "core/Audio.hpp"
#include "core/ConfigLoader.hpp"
#include "core/InputBindings.hpp"
#include "core/PokeSpriteAssets.hpp"
#include "core/SaveDataStore.hpp"
#include "core/SaveLibrary.hpp"
#include "resort/services/PokemonResortService.hpp"
#include "ui/LoadingScreen.hpp"
#include "ui/ScreenInput.hpp"
#include "ui/TransferSaveSelection.hpp"
#include "ui/TransferSystemScreen.hpp"
#include "ui/TransferTicketScreen.hpp"
#include "ui/TitleScreen.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
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
    Loading,
    TransferTicket,
    TransferSystem
};

enum class ActiveMusicTrack {
    None,
    Menu,
    Transfer
};

enum class LoadingPurpose {
    None,
    ScanTransferTickets,
    DeepProbeSelectedSave
};

constexpr double kNavigationRepeatDelaySeconds = 0.42;
constexpr double kNavigationRepeatIntervalSeconds = 0.18;

struct NavigationHold {
    int dx = 0;
    int dy = 0;
    double elapsed_seconds = 0.0;
    double repeat_elapsed_seconds = 0.0;
};

double clamp01(double value) {
    return std::max(0.0, std::min(1.0, value));
}

NavigationHold navigationDeltaForKey(SDL_Keycode key, const InputConfig& input) {
    NavigationHold out;
    if (matchesBinding(key, input.navigate_up_keys)) out.dy = -1;
    if (matchesBinding(key, input.navigate_down_keys)) out.dy = 1;
    if (matchesBinding(key, input.navigate_left_keys)) out.dx = -1;
    if (matchesBinding(key, input.navigate_right_keys)) out.dx = 1;
    return out;
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

std::string gameTitleFromId(const std::string& game_id) {
    if (game_id == "pokemon_hgss") {
        return "Pokemon HG/SS";
    }
    if (game_id == "pokemon_heartgold") {
        return "Pokemon HeartGold";
    }
    if (game_id == "pokemon_soulsilver") {
        return "Pokemon SoulSilver";
    }
    if (game_id == "pokemon_firered") {
        return "Pokemon FireRed";
    }
    if (game_id == "pokemon_leafgreen") {
        return "Pokemon LeafGreen";
    }

    std::string title = game_id;
    for (char& c : title) {
        if (c == '_') {
            c = ' ';
        }
    }

    bool capitalize_next = true;
    for (char& c : title) {
        if (c == ' ') {
            capitalize_next = true;
            continue;
        }
        if (capitalize_next && c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
        capitalize_next = false;
    }
    return title;
}

std::vector<TransferSaveSelection> transferSelectionsFromRecords(const std::vector<SaveFileRecord>& records) {
    std::vector<TransferSaveSelection> selections;
    selections.reserve(records.size());
    for (const SaveFileRecord& record : records) {
        if (!record.transfer_summary) {
            continue;
        }

        const TransferSaveSummary& summary = *record.transfer_summary;
        TransferSaveSelection selection;
        selection.source_path = record.path;
        selection.source_filename = record.filename;
        selection.game_key = summary.game_id;
        selection.game_title = gameTitleFromId(summary.game_id);
        selection.trainer_name = summary.player_name;
        selection.time = summary.play_time;
        selection.pokedex = std::to_string(summary.pokedex_count);
        selection.badges = std::to_string(summary.badges);
        selection.party_slots = summary.party_slots;
        selection.box1_slots = summary.box_1_slots;
        selection.pc_boxes.reserve(summary.pc_boxes.size());
        for (const auto& box : summary.pc_boxes) {
            TransferSaveSelection::PcBox out_box;
            out_box.name = box.name;
            out_box.slots = box.slots;
            selection.pc_boxes.push_back(std::move(out_box));
        }
        selections.push_back(std::move(selection));
    }
    return selections;
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

    if (SDL_RenderSetLogicalSize(
            renderer.get(),
            config.window.virtual_width,
            config.window.virtual_height) != 0) {
        throw std::runtime_error(std::string("Failed to set renderer logical size: ") + SDL_GetError());
    }

    Assets assets = loadAssets(renderer.get(), config, root);
    TitleScreen title_screen(config, std::move(assets));
    SaveLibrary save_library(root, save_directory.string(), argv0);
    std::shared_ptr<PokeSpriteAssets> poke_sprite_assets = PokeSpriteAssets::create(root);
    std::unique_ptr<resort::PokemonResortService> pokemon_resort_service;
    try {
        pokemon_resort_service = std::make_unique<resort::PokemonResortService>(
            resort::defaultResortProfilePath(save_directory));
        pokemon_resort_service->ensureProfile("default");
    } catch (const std::exception& ex) {
        std::cerr << "Warning: could not initialize Pokemon Resort profile storage: "
                  << ex.what() << '\n';
    }
    std::unique_ptr<LoadingScreen> loading_screen;
    std::unique_ptr<TransferTicketScreen> transfer_ticket;
    std::unique_ptr<TransferSystemScreen> transfer_system_screen;
    std::unordered_map<std::string, int> last_game_box_index_by_game_key;
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

    bool running = true;
    Uint64 last_counter = SDL_GetPerformanceCounter();
    bool music_playing = false;
    double transfer_music_elapsed_seconds = 0.0;
    ActiveScreen active_screen = ActiveScreen::Title;
    std::future<void> transfer_load_future;
    std::future<std::optional<TransferSaveSummary>> transfer_detail_future;
    LoadingPurpose loading_purpose = LoadingPurpose::None;
    TransferSaveSelection pending_transfer_detail_selection;
    NavigationHold navigation_hold;
    const auto active_input = [&]() -> ScreenInput* {
        switch (active_screen) {
            case ActiveScreen::Title:
                return &title_screen;
            case ActiveScreen::TransferTicket:
                return transfer_ticket.get();
            case ActiveScreen::TransferSystem:
                return transfer_system_screen.get();
            case ActiveScreen::Loading:
                return nullptr;
        }
        return nullptr;
    };

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
                continue;
            }

            if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                const SDL_Keycode key = event.key.keysym.sym;
                const NavigationHold nav = navigationDeltaForKey(key, config.input);
                if (nav.dx != 0 || nav.dy != 0) {
                    if (ScreenInput* input = active_input()) {
                        if (input->canNavigate2d()) {
                            input->onNavigate2d(nav.dx, nav.dy);
                            navigation_hold = nav;
                            navigation_hold.elapsed_seconds = 0.0;
                            navigation_hold.repeat_elapsed_seconds = 0.0;
                            continue;
                        }
                        if (nav.dy != 0 && input->canNavigate()) {
                            input->onNavigate(nav.dy);
                            navigation_hold = {};
                            navigation_hold.dy = nav.dy;
                            navigation_hold.elapsed_seconds = 0.0;
                            navigation_hold.repeat_elapsed_seconds = 0.0;
                            continue;
                        }
                    }
                }
                if (matchesBinding(key, config.input.back_keys)) {
                    if (ScreenInput* input = active_input()) {
                        input->onBackPressed();
                    }
                    continue;
                }
                if (matchesBinding(key, config.input.forward_keys)) {
                    if (ScreenInput* input = active_input(); input && input->acceptsAdvanceInput()) {
                        input->onAdvancePressed();
                    }
                    continue;
                }
            }

            if (event.type == SDL_KEYUP) {
                const NavigationHold nav = navigationDeltaForKey(event.key.keysym.sym, config.input);
                if ((nav.dx != 0 && navigation_hold.dx == nav.dx) ||
                    (nav.dy != 0 && navigation_hold.dy == nav.dy)) {
                    navigation_hold = {};
                }
            }

            // SDL_RenderSetLogicalSize scales mouse events into renderer logical coordinates.
            if (event.type == SDL_MOUSEMOTION && config.input.accept_mouse) {
                if (ScreenInput* input = active_input()) {
                    input->handlePointerMoved(event.motion.x, event.motion.y);
                }
                continue;
            }

            if (event.type == SDL_MOUSEBUTTONDOWN && config.input.accept_mouse) {
                if (ScreenInput* input = active_input()) {
                    input->handlePointerPressed(event.button.x, event.button.y);
                }
                continue;
            }

            if (event.type == SDL_MOUSEBUTTONUP && config.input.accept_mouse) {
                if (ScreenInput* input = active_input()) {
                    input->handlePointerReleased(event.button.x, event.button.y);
                }
                continue;
            }

            if (event.type == SDL_CONTROLLERBUTTONDOWN && config.input.accept_controller) {
                switch (event.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:
                        if (ScreenInput* input = active_input()) {
                            if (input->canNavigate2d()) {
                                input->onNavigate2d(0, -1);
                                navigation_hold = {};
                                navigation_hold.dy = -1;
                                navigation_hold.elapsed_seconds = 0.0;
                                navigation_hold.repeat_elapsed_seconds = 0.0;
                            } else if (input->canNavigate()) {
                                input->onNavigate(-1);
                                navigation_hold = {};
                                navigation_hold.dy = -1;
                                navigation_hold.elapsed_seconds = 0.0;
                                navigation_hold.repeat_elapsed_seconds = 0.0;
                            }
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                        if (ScreenInput* input = active_input()) {
                            if (input->canNavigate2d()) {
                                input->onNavigate2d(0, 1);
                                navigation_hold = {};
                                navigation_hold.dy = 1;
                                navigation_hold.elapsed_seconds = 0.0;
                                navigation_hold.repeat_elapsed_seconds = 0.0;
                            } else if (input->canNavigate()) {
                                input->onNavigate(1);
                                navigation_hold = {};
                                navigation_hold.dy = 1;
                                navigation_hold.elapsed_seconds = 0.0;
                                navigation_hold.repeat_elapsed_seconds = 0.0;
                            }
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                        if (ScreenInput* input = active_input(); input && input->canNavigate2d()) {
                            input->onNavigate2d(-1, 0);
                            navigation_hold = {};
                            navigation_hold.dx = -1;
                            navigation_hold.elapsed_seconds = 0.0;
                            navigation_hold.repeat_elapsed_seconds = 0.0;
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                        if (ScreenInput* input = active_input(); input && input->canNavigate2d()) {
                            input->onNavigate2d(1, 0);
                            navigation_hold = {};
                            navigation_hold.dx = 1;
                            navigation_hold.elapsed_seconds = 0.0;
                            navigation_hold.repeat_elapsed_seconds = 0.0;
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_A:
                        if (ScreenInput* input = active_input(); input && input->acceptsAdvanceInput()) {
                            input->onAdvancePressed();
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_B:
                        if (ScreenInput* input = active_input()) {
                            input->onBackPressed();
                        }
                        break;
                    default:
                        break;
                }
            }

            if (event.type == SDL_CONTROLLERBUTTONUP && config.input.accept_controller) {
                if ((event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP && navigation_hold.dy == -1) ||
                    (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN && navigation_hold.dy == 1) ||
                    (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT && navigation_hold.dx == -1) ||
                    (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT && navigation_hold.dx == 1)) {
                    navigation_hold = {};
                }
            }
        }

        Uint64 now = SDL_GetPerformanceCounter();
        double dt = static_cast<double>(now - last_counter) / static_cast<double>(SDL_GetPerformanceFrequency());
        last_counter = now;

        if (ScreenInput* input = active_input();
            input && ((input->canNavigate2d() && (navigation_hold.dx != 0 || navigation_hold.dy != 0)) ||
                      (input->canNavigate() && navigation_hold.dy != 0))) {
            const double previous_elapsed = navigation_hold.elapsed_seconds;
            navigation_hold.elapsed_seconds += dt;

            if (previous_elapsed < kNavigationRepeatDelaySeconds &&
                navigation_hold.elapsed_seconds >= kNavigationRepeatDelaySeconds) {
                if (input->canNavigate2d()) {
                    input->onNavigate2d(navigation_hold.dx, navigation_hold.dy);
                } else {
                    input->onNavigate(navigation_hold.dy);
                }
                navigation_hold.repeat_elapsed_seconds = 0.0;
            } else if (navigation_hold.elapsed_seconds >= kNavigationRepeatDelaySeconds) {
                navigation_hold.repeat_elapsed_seconds += dt;
                while (navigation_hold.repeat_elapsed_seconds >=
                       kNavigationRepeatIntervalSeconds) {
                    if (input->canNavigate2d()) {
                        input->onNavigate2d(navigation_hold.dx, navigation_hold.dy);
                    } else {
                        input->onNavigate(navigation_hold.dy);
                    }
                    navigation_hold.repeat_elapsed_seconds -=
                        kNavigationRepeatIntervalSeconds;
                }
            }
        } else {
            navigation_hold = {};
        }

        if (active_screen == ActiveScreen::Title) {
            title_screen.update(dt);
            if (title_screen.consumeOpenTransferRequest()) {
                if (!loading_screen) {
                    loading_screen = std::make_unique<LoadingScreen>(
                        renderer.get(),
                        config.window,
                        config.assets.font,
                        root);
                }
                if (!transfer_ticket) {
                    transfer_ticket = std::make_unique<TransferTicketScreen>(
                        renderer.get(),
                        config.window,
                        config.assets.font,
                        root,
                        poke_sprite_assets);
                }
                loading_screen->enter();
                loading_purpose = LoadingPurpose::ScanTransferTickets;
                transfer_load_future = std::async(
                    std::launch::async,
                    [&save_library]() {
                        save_library.refreshForTransferPage();
                    });
                active_screen = ActiveScreen::Loading;
            }
        } else if (active_screen == ActiveScreen::Loading && loading_screen) {
            loading_screen->update(dt);
            if (loading_purpose == LoadingPurpose::ScanTransferTickets) {
                if (transfer_load_future.valid() &&
                    transfer_load_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                    try {
                        transfer_load_future.get();
                    } catch (const std::exception& ex) {
                        std::cerr << "Warning: transfer save loading failed: " << ex.what() << '\n';
                    }
                    if (transfer_ticket) {
                        transfer_ticket->setSaveSelections(
                            renderer.get(),
                            transferSelectionsFromRecords(save_library.transferPageRecords()));
                        transfer_ticket->enter();
                    }
                    loading_purpose = LoadingPurpose::None;
                    active_screen = ActiveScreen::TransferTicket;
                }
            } else if (loading_purpose == LoadingPurpose::DeepProbeSelectedSave) {
                if (transfer_detail_future.valid() &&
                    transfer_detail_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                    std::optional<TransferSaveSummary> fresh_summary;
                    try {
                        fresh_summary = transfer_detail_future.get();
                    } catch (const std::exception& ex) {
                        std::cerr << "Warning: transfer detail probe failed: " << ex.what() << '\n';
                    }

                    TransferSaveSelection merged = pending_transfer_detail_selection;
                    if (fresh_summary) {
                        merged.box1_slots = fresh_summary->box_1_slots;
                        merged.pc_boxes.clear();
                        merged.pc_boxes.reserve(fresh_summary->pc_boxes.size());
                        for (const auto& b : fresh_summary->pc_boxes) {
                            TransferSaveSelection::PcBox out;
                            out.name = b.name;
                            out.slots = b.slots;
                        merged.pc_boxes.push_back(std::move(out));
                    }
                        merged.party_slots = fresh_summary->party_slots;
                        merged.time = fresh_summary->play_time;
                        merged.pokedex = std::to_string(fresh_summary->pokedex_count);
                        merged.badges = std::to_string(fresh_summary->badges);
                        std::size_t filled_slots = 0;
                        for (const auto& slot : merged.box1_slots) {
                            if (slot.occupied()) {
                                ++filled_slots;
                            }
                        }
                        std::cerr << "[App] game transfer probe ok file=" << merged.source_filename
                                  << " box1_slots=" << merged.box1_slots.size() << " non_empty=" << filled_slots
                                  << '\n';
                        if (merged.box1_slots.empty()) {
                            std::cerr << "[App] hint: fresh PKHeX probe returned no PC box 1 slots (transfer_save_cache is "
                                         "not used here). Check bridge/PKHeX output for this save format.\n";
                        }
                    } else {
                        std::cerr << "Warning: fresh PKHeX probe failed — no box sprites (check .NET bridge / "
                                     "PKHEX_BRIDGE_EXECUTABLE). Run: pkr clear\n";
                        merged.box1_slots.clear();
                    }

                    if (!transfer_system_screen) {
                        transfer_system_screen = std::make_unique<TransferSystemScreen>(
                            renderer.get(),
                            config.window,
                            config.assets.font,
                            root,
                            poke_sprite_assets);
                    }
                    int initial_box_index = 0;
                    if (!merged.game_key.empty()) {
                        auto it = last_game_box_index_by_game_key.find(merged.game_key);
                        if (it != last_game_box_index_by_game_key.end()) {
                            initial_box_index = it->second;
                        }
                    }
                    transfer_system_screen->enter(merged, renderer.get(), initial_box_index);
                    loading_purpose = LoadingPurpose::None;
                    active_screen = ActiveScreen::TransferSystem;
                }
            }
        } else if (active_screen == ActiveScreen::TransferTicket && transfer_ticket) {
            transfer_ticket->update(dt);
            TransferSaveSelection selected_transfer_save;
            if (transfer_ticket->consumeOpenTransferSystemRequest(selected_transfer_save)) {
                pending_transfer_detail_selection = std::move(selected_transfer_save);
                if (!loading_screen) {
                    loading_screen = std::make_unique<LoadingScreen>(
                        renderer.get(),
                        config.window,
                        config.assets.font,
                        root);
                }
                loading_screen->enter();
                const std::string detail_path = pending_transfer_detail_selection.source_path;
                transfer_detail_future = std::async(
                    std::launch::async,
                    [root, argv0, detail_path]() -> std::optional<TransferSaveSummary> {
                        return probeTransferSummaryFresh(root, argv0, detail_path);
                    });
                loading_purpose = LoadingPurpose::DeepProbeSelectedSave;
                active_screen = ActiveScreen::Loading;
            }
            if (transfer_ticket->consumeReturnToMainMenuRequest()) {
                title_screen.returnToMainMenuFromTransfer();
                last_game_box_index_by_game_key.clear();
                active_screen = ActiveScreen::Title;
            }
        } else if (active_screen == ActiveScreen::TransferSystem && transfer_system_screen) {
            transfer_system_screen->update(dt);
            if (transfer_system_screen->consumeReturnToTicketListRequest()) {
                if (!transfer_system_screen->currentGameKey().empty()) {
                    last_game_box_index_by_game_key[transfer_system_screen->currentGameKey()] =
                        transfer_system_screen->currentGameBoxIndex();
                }
                if (transfer_ticket) {
                    transfer_ticket->prepareReturnFromGameTransferScreen();
                }
                active_screen = ActiveScreen::TransferTicket;
            }
        }

        const bool wants_menu_music = active_screen == ActiveScreen::Title && title_screen.wantsMenuMusic();
        const bool transfer_flow_screen =
            active_screen == ActiveScreen::TransferTicket ||
            active_screen == ActiveScreen::TransferSystem ||
            (active_screen == ActiveScreen::Loading &&
                (loading_purpose == LoadingPurpose::ScanTransferTickets ||
                 loading_purpose == LoadingPurpose::DeepProbeSelectedSave));
        const bool wants_transfer_music =
            transfer_flow_screen &&
            transfer_ticket &&
            !transfer_ticket->musicPath().empty();
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
        } else if (desired_music_track == ActiveMusicTrack::Transfer && transfer_ticket) {
            if (loaded_music_track != ActiveMusicTrack::Transfer) {
                audio.stopMusic();
                music_playing = false;
                transfer_music_elapsed_seconds = 0.0;
                const fs::path transfer_music_path = fs::path(root) / transfer_ticket->musicPath();
                if (audio.loadMusic(transfer_music_path.string())) {
                    loaded_music_track = ActiveMusicTrack::Transfer;
                } else {
                    loaded_music_track = ActiveMusicTrack::None;
                    std::cerr << "Warning: could not load transfer music at " << transfer_music_path << '\n';
                }
            }
            if (loaded_music_track == ActiveMusicTrack::Transfer && audio.isMusicLoaded()) {
                transfer_music_elapsed_seconds += dt;
                const double silence_seconds = std::max(0.0, transfer_ticket->musicSilenceSeconds());
                const double fade_in_seconds = std::max(0.0, transfer_ticket->musicFadeInSeconds());
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
        const bool title_button_sfx_requested = title_screen.consumeButtonSfxRequest();
        const bool transfer_button_sfx_requested =
            transfer_ticket && transfer_ticket->consumeButtonSfxRequest();
        const bool transfer_system_button_sfx_requested =
            transfer_system_screen && transfer_system_screen->consumeButtonSfxRequest();
        if (title_button_sfx_requested || transfer_button_sfx_requested || transfer_system_button_sfx_requested) {
            audio.playButtonSfx();
        }
        if (transfer_system_screen && transfer_system_screen->consumeUiMoveSfxRequest()) {
            audio.playUiMoveSfx();
        }
        if (transfer_ticket && transfer_ticket->consumeRipSfxRequest()) {
            audio.playRipSfx();
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
        } else if (active_screen == ActiveScreen::Loading && loading_screen) {
            loading_screen->render(renderer.get());
        } else if (active_screen == ActiveScreen::TransferTicket && transfer_ticket) {
            transfer_ticket->render(renderer.get());
        } else if (transfer_system_screen) {
            transfer_system_screen->render(renderer.get());
        }
        SDL_RenderPresent(renderer.get());
    }

    return 0;
}

} // namespace pr
