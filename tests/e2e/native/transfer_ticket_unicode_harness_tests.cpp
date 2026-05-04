#include "core/config/ConfigLoader.hpp"
#include "core/assets/PokeSpriteAssets.hpp"
#include "ui/TransferTicketScreen.hpp"

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

struct SdlHarness {
    SdlHarness() {
        setenv("SDL_VIDEODRIVER", "dummy", 1);
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            throw TestFailure(std::string("SDL_Init failed: ") + SDL_GetError());
        }
        if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
            throw TestFailure(std::string("IMG_Init failed: ") + IMG_GetError());
        }
        if (TTF_Init() != 0) {
            throw TestFailure(std::string("TTF_Init failed: ") + TTF_GetError());
        }

        window.reset(SDL_CreateWindow("transfer-ticket-unicode-test", 0, 0, 1280, 800, SDL_WINDOW_HIDDEN));
        if (!window) {
            throw TestFailure(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        }

        renderer.reset(SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_SOFTWARE));
        if (!renderer) {
            throw TestFailure(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
        }
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

pr::TransferSaveSelection makeUnicodeSelection() {
    pr::TransferSaveSelection selection;
    selection.game_key = "pokemon_blue";
    selection.game_title = "ポケモン 青？";
    selection.trainer_name = "サトシ？";
    selection.time = "1:23";
    selection.pokedex = "42";
    selection.badges = "8";
    return selection;
}

void testTransferTicketBuildsUnicodeTextTextures() {
    SdlHarness sdl;
    const fs::path repo_root = repositoryRoot();
    const std::string project_root = repo_root.string();
    const pr::AppConfig app_config =
        pr::loadAppConfigFromJson((repo_root / "config" / "app.json").string());
    const pr::TitleScreenConfig title_config =
        pr::loadConfigFromJson((repo_root / "config" / "title_screen.json").string());
    auto sprite_assets = pr::PokeSpriteAssets::create(project_root);

    pr::TransferTicketScreen screen(
        sdl.renderer.get(),
        app_config.window,
        title_config.assets.font,
        project_root,
        std::move(sprite_assets));
    screen.setSaveSelections(sdl.renderer.get(), std::vector<pr::TransferSaveSelection>{makeUnicodeSelection()});
    screen.enter();
    screen.render(sdl.renderer.get());

    expect(screen.debugTicketGameTitleTextureReady(0),
           "ticket screen should build a renderable game-title texture for Unicode save names");
    expect(screen.debugTicketTrainerTextureReady(0),
           "ticket screen should build a renderable trainer-name texture for Unicode save names");
}

} // namespace

int main() {
    try {
        testTransferTicketBuildsUnicodeTextTextures();
        return EXIT_SUCCESS;
    } catch (const TestFailure& failure) {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return EXIT_FAILURE;
    } catch (const std::exception& ex) {
        std::cerr << "ERROR: unexpected exception: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}
