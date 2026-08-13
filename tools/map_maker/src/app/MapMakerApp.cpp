#include "mapmaker/app/MapMakerApp.hpp"

#include "gameplay/world3d/rendering/bgfx/BgfxBackend.hpp"
#include "mapmaker/app/AutosaveRecovery.hpp"
#include "mapmaker/app/EditorController.hpp"
#include "mapmaker/app/FrameMetrics.hpp"
#include "mapmaker/app/ProjectWorkspace.hpp"
#include "mapmaker/app/SdlImGuiInputBridge.hpp"
#include "mapmaker/assets/BgfxThumbnailCache.hpp"
#include "mapmaker/assets/EditorAssetCatalog.hpp"
#include "mapmaker/logging/StructuredLogger.hpp"
#include "mapmaker/preview/ExactWorldPreview.hpp"
#include "mapmaker/project/ProjectDiscovery.hpp"
#include "mapmaker/ui/EditorShell.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <bgfx/bgfx.h>
#include <dear-imgui/imgui.h>
#include <imgui/imgui.h>
#if defined(__APPLE__)
#include <SDL_metal.h>
#endif

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace pr::mapmaker {
namespace {

using Window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;

std::filesystem::path projectPath(const MapMakerOptions& options) {
    if (options.project_path) return *options.project_path;
    const ProjectDiscoveryResult discovery = discoverMapProject(options.resort_root);
    if (discovery.path) return *discovery.path;
    std::string message = "No map project found. Searched:";
    for (const auto& path : discovery.searched_paths) message += "\n  " + path.string();
    throw std::runtime_error(message);
}

const char* severity(DiagnosticSeverity value) {
    switch (value) {
        case DiagnosticSeverity::Info: return "info";
        case DiagnosticSeverity::Warning: return "warning";
        case DiagnosticSeverity::Error: return "error";
    }
    return "error";
}

int validateProject(const MapMakerOptions& options) {
    ProjectWorkspace workspace = ProjectWorkspace::open(
        options.resort_root, projectPath(options));
    const auto diagnostics = workspace.validate();
    std::cout << "Project: " << workspace.projectPath() << '\n'
              << "Maps: " << workspace.project().maps().size()
              << " (" << workspace.sources().size() << " unique source files)\n";
    for (const auto& diagnostic : diagnostics) {
        std::cout << severity(diagnostic.severity) << " [" << diagnostic.code << "]";
        if (!diagnostic.map_id.empty()) std::cout << " " << diagnostic.map_id;
        if (!diagnostic.object_id.empty()) std::cout << "/" << diagnostic.object_id;
        std::cout << ": " << diagnostic.message << '\n';
    }
    if (diagnostics.empty()) std::cout << "Validation passed.\n";
    return hasValidationErrors(diagnostics) ? 2 : 0;
}

struct SdlRuntime {
    SdlRuntime() {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
            throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
        }
        if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
            const std::string error = IMG_GetError();
            SDL_Quit();
            throw std::runtime_error("IMG_Init failed: " + error);
        }
        if (TTF_Init() != 0) {
            const std::string error = TTF_GetError();
            IMG_Quit();
            SDL_Quit();
            throw std::runtime_error("TTF_Init failed: " + error);
        }
    }
    ~SdlRuntime() {
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
    }
};

struct MetalViewRuntime {
    explicit MetalViewRuntime(SDL_Window* window) {
#if defined(__APPLE__)
        view = SDL_Metal_CreateView(window);
        if (!view) {
            throw std::runtime_error(std::string("SDL_Metal_CreateView failed: ") + SDL_GetError());
        }
#else
        (void)window;
#endif
    }
    ~MetalViewRuntime() {
#if defined(__APPLE__)
        if (view) SDL_Metal_DestroyView(static_cast<SDL_MetalView>(view));
#endif
    }
    void* view = nullptr;
};

struct BgfxGlobalRuntime {
    ~BgfxGlobalRuntime() {
        gameplay::world3d::rendering::bgfx_backend::BgfxBackend::shutdownGlobal();
    }
};

struct ImGuiRuntime {
    ImGuiRuntime() {
        imguiCreate(17.0f);
        ImGui::GetIO().ConfigMacOSXBehaviors = true;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        applyMapMakerStyle();
    }
    ~ImGuiRuntime() { shutdown(); }
    void shutdown() {
        if (!active) return;
        imguiDestroy();
        active = false;
    }
    bool active = true;
};

struct TextInputRuntime {
    TextInputRuntime() { SDL_StartTextInput(); }
    ~TextInputRuntime() { SDL_StopTextInput(); }
};

void configureBackbuffer(int width, int height) {
    constexpr bgfx::ViewId view = 254;
    bgfx::setViewName(view, "Map Maker Backbuffer");
    bgfx::setViewRect(view, 0, 0,
        static_cast<std::uint16_t>(std::max(1, width)),
        static_cast<std::uint16_t>(std::max(1, height)));
    bgfx::setViewClear(view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x0e1015ffU, 1.0f, 0);
    bgfx::touch(view);
}

int runWindow(const MapMakerOptions& options) {
    SdlRuntime sdl;
    std::uint32_t flags = SDL_WINDOW_RESIZABLE |
        (options.smoke_test ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN);
#if defined(__APPLE__)
    flags |= SDL_WINDOW_METAL;
#endif
    Window window(SDL_CreateWindow("Pokemon Resort Map Maker",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1440, 900, flags), SDL_DestroyWindow);
    if (!window) throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());

    MetalViewRuntime metal_view(window.get());
    BgfxGlobalRuntime bgfx_global;

    const std::filesystem::path state_root = options.resort_root / "build" / "map-maker-state";
    StructuredLogger logger({state_root / "logs", "map_maker.log"});
    logger.log(LogLevel::Info, "startup", "Opening " + projectPath(options).string());
    ProjectWorkspace workspace = ProjectWorkspace::open(options.resort_root, projectPath(options));
    if (options.initial_map_id && !workspace.activateMap(*options.initial_map_id)) {
        throw std::runtime_error("Unknown map id: " + *options.initial_map_id);
    }

    std::string catalog_error;
    RtpksEditorCatalog tile_catalog = RtpksEditorCatalog::load(
        options.resort_root / "assets" / "overworld" / "tilepacks" / "maptiles.rtpks.meta",
        &catalog_error);
    if (!tile_catalog.valid()) throw std::runtime_error(catalog_error);
    std::vector<std::string> model_warnings;
    auto model_catalog = loadModelAssetCatalog(
        options.resort_root / "assets" / "overworld" / "models", &model_warnings);
    for (const std::string& warning : model_warnings) {
        logger.log(LogLevel::Warning, "assets", warning);
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window.get(), &width, &height);
    gameplay::world3d::rendering::bgfx_backend::BgfxBackend host_backend;
    if (!host_backend.initialize(window.get(), width, height, options.renderer, metal_view.view)) {
        throw std::runtime_error(host_backend.lastError());
    }
    ExactWorldPreview preview(options.resort_root);
    if (!preview.initialize(window.get(), width, height, options.renderer, metal_view.view)) {
        throw std::runtime_error(preview.lastError());
    }
    // Normal authoring starts without constructing the heavyweight game scene.
    // The exact renderer is loaded on demand when Game Preview is selected.
    // Smoke mode still exercises that integration seam explicitly.
    if (options.smoke_test) {
        const OpenMapSource* initial_source = workspace.activeSource();
        if (!initial_source || !preview.loadMap(initial_source->path)) {
            throw std::runtime_error(preview.lastError().empty()
                ? "No active map source" : preview.lastError());
        }
        preview.setAnimationsEnabled(true);
    }

    ImGuiRuntime imgui;
    TextInputRuntime text_input;

    BgfxThumbnailCache thumbnails(tile_catalog);
    AutosaveRecovery recovery(state_root / "recovery");
    EditorController controller(options.resort_root, workspace, preview, tile_catalog,
        std::move(model_catalog), thumbnails, recovery, logger);
    EditorShell shell;
    SdlImGuiInputBridge input;
    FrameMetrics metrics;
    bool running = true;
    int frames = 0;
    std::uint32_t newest_event_time = SDL_GetTicks();
    auto previous = std::chrono::steady_clock::now();

    while (running) {
        SDL_Event event;
        bool received_event = false;
        while (SDL_PollEvent(&event)) {
            received_event = true;
            newest_event_time = event.common.timestamp;
            if (event.type == SDL_QUIT ||
                (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE)) {
                running = false;
            }
            input.handleEvent(event);
        }
        const auto now = std::chrono::steady_clock::now();
        const double delta = std::chrono::duration<double>(now - previous).count();
        previous = now;
        int logical_width = 0;
        int logical_height = 0;
        SDL_GetWindowSize(window.get(), &logical_width, &logical_height);
        SDL_GetWindowSizeInPixels(window.get(), &width, &height);
        const auto texture = (options.smoke_test || controller.gamePreviewVisible())
            ? preview.render(width, height, delta)
            : ExactWorldPreview::ViewportTexture{};
        configureBackbuffer(width, height);
        input.beginFrame(logical_width, logical_height, 255);
        thumbnails.beginFrame();
        controller.setViewportTexture(texture);
        EditorUiModel ui_model = controller.buildUiModel(metrics);
        const EditorUiEvents ui_events = shell.draw(ui_model);
        imguiEndFrame();
        bgfx::frame();

        controller.handle(ui_events);
        if (controller.gamePreviewVisible()) (void)controller.reloadPreview();
        controller.tickRecovery();
        const auto finished = std::chrono::steady_clock::now();
        metrics.recordFrame(std::chrono::duration<double, std::milli>(finished - now).count());
        if (received_event) {
            metrics.recordInputLatency(static_cast<double>(SDL_GetTicks() - newest_event_time));
        }
        ++frames;
        if (options.smoke_test && frames >= 12) running = false;
    }

    for (OpenMapSource* source : workspace.sources()) {
        if (!source->commands.isDirty()) continue;
        std::string recovery_error;
        if (!recovery.writeNow(source->key, source->document, &recovery_error)) {
            logger.log(LogLevel::Error, "recovery",
                "Could not preserve unsaved source '" + source->key + "' during shutdown: " +
                    recovery_error);
        }
    }

    logger.log(LogLevel::Info, "shutdown", options.smoke_test
        ? "Smoke test completed after " + std::to_string(frames) + " frames"
        : "Editor closed");
    logger.flush();
    const bool smoke_ready = preview.ready();
    // Destroy editor and exact-preview GPU resources while bgfx is still live,
    // then drain deferred destruction before the process-global backend exits.
    thumbnails.clear();
    preview.shutdown();
    imgui.shutdown();
    host_backend.shutdown();
    bgfx::frame();
    bgfx::frame();
    return options.smoke_test && !smoke_ready ? 1 : 0;
}

} // namespace

int runMapMaker(const MapMakerOptions& options) {
    try {
        if (options.validate_project) return validateProject(options);
        return runWindow(options);
    } catch (const std::exception& exception) {
        std::cerr << "pokemon_resort_map_maker: " << exception.what() << '\n';
        return 1;
    }
}

} // namespace pr::mapmaker
