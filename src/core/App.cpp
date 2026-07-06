#include "core/App.hpp"
#include "core/assets/Assets.hpp"
#include "core/config/ConfigLoader.hpp"
#include "core/input/InputRouter.hpp"
#include "core/input/InputBindings.hpp"
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
#include "ui/AttendTestScreen.hpp"
#include "ui/Overworld3DTestScreen.hpp"
#include "ui/TransferFlowCoordinator.hpp"
#include "ui/TitleScreen.hpp"
#include "core/assets/Font.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_metal.h>
#include <SDL_ttf.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <array>

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

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool world3dBackendWantsBgfx(const AppConfig& config) {
    const std::string backend = lower(config.renderer.world3d_backend);
    return backend == "bgfx" || backend == "auto";
}

enum class WindowPresentation {
    Sdl2D,
    Bgfx3D
};

RendererPtr createAppRenderer(SDL_Window* window, const WindowConfig& window_config) {
    RendererPtr renderer(SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED));
    if (!renderer) {
        throw std::runtime_error(std::string("Failed to create renderer: ") + SDL_GetError());
    }
    if (SDL_RenderSetLogicalSize(renderer.get(), window_config.virtual_width, window_config.virtual_height) != 0) {
        throw std::runtime_error(std::string("Failed to set renderer logical size: ") + SDL_GetError());
    }
    return renderer;
}

void logRendererInfo(SDL_Renderer* renderer) {
    SDL_RendererInfo renderer_info{};
    if (SDL_GetRendererInfo(renderer, &renderer_info) == 0) {
        std::cerr << "[App] SDL renderer: "
                  << (renderer_info.name ? renderer_info.name : "unknown")
                  << '\n';
    }
}

void rebindSdlUi(
    RendererPtr& renderer,
    SDL_Window* window,
    const TitleScreenConfig& config,
    const std::string& root,
    TitleScreen& title_screen,
    AppLoadingCoordinator& loading,
    TransferFlowCoordinator& transfer_flow) {
    renderer = createAppRenderer(window, config.window);
    logRendererInfo(renderer.get());
    title_screen.replaceAssets(loadAssets(renderer.get(), config, root));
    loading.rebindRenderer(renderer.get());
    transfer_flow.rebindRenderer(renderer.get());
}

void releaseSdlUi(
    RendererPtr& renderer,
    TitleScreen& title_screen,
    AppLoadingCoordinator& loading,
    TransferFlowCoordinator& transfer_flow) {
    title_screen.replaceAssets(Assets{});
    loading.rebindRenderer(nullptr);
    transfer_flow.rebindRenderer(nullptr);
    renderer.reset();
}

void renderFilledCircle(SDL_Renderer* renderer, int cx, int cy, int radius) {
    for (int dy = -radius; dy <= radius; ++dy) {
        const int dx = static_cast<int>(std::floor(std::sqrt(static_cast<double>(radius * radius - dy * dy))));
        SDL_RenderDrawLine(renderer, cx - dx, cy + dy, cx + dx, cy + dy);
    }
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

std::string timestampNow() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return out.str();
}

std::string sanitizeFilenameSlug(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '_' ||
            c == '-') {
            out.push_back(static_cast<char>(c));
        } else if (c >= 'A' && c <= 'Z') {
            out.push_back(static_cast<char>(SDL_tolower(c)));
        } else if (c == ' ') {
            out.push_back('_');
        }
    }
    return out;
}

fs::path uniqueOutputPath(const fs::path& directory, const std::string& basename, const std::string& extension) {
    fs::path candidate = directory / (basename + extension);
    if (!fs::exists(candidate)) {
        return candidate;
    }

    for (int suffix = 2; suffix < 10000; ++suffix) {
        candidate = directory / (basename + "_" + std::to_string(suffix) + extension);
        if (!fs::exists(candidate)) {
            return candidate;
        }
    }

    return directory / (basename + "_" + timestampNow() + extension);
}

bool ffmpegSupportsWebpEncoding() {
    static const bool supported = []() {
        std::array<char, 256> buffer{};
        std::string output;
        FILE* pipe = popen("ffmpeg -hide_banner -encoders 2>/dev/null", "r");
        if (!pipe) {
            return false;
        }
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output.append(buffer.data());
        }
        pclose(pipe);
        return output.find("libwebp") != std::string::npos;
    }();
    return supported;
}

std::optional<fs::path> buildScreenshotOutputPath(
    const std::string& project_root,
    const std::string& context_slug,
    bool force_png = false) {
    const fs::path screenshot_dir = fs::path(project_root) / "screenshots";
    std::error_code ec;
    fs::create_directories(screenshot_dir, ec);
    if (ec) {
        std::cerr << "Screenshot error: cannot create screenshots directory: "
                  << ec.message() << '\n';
        return std::nullopt;
    }

    const std::string slug = sanitizeFilenameSlug(context_slug);
    const std::string basename = slug.empty() ? ("screenshot_" + timestampNow()) : slug;
    const std::string extension =
        force_png ? ".png" : (ffmpegSupportsWebpEncoding() ? ".webp" : ".png");
    return uniqueOutputPath(screenshot_dir, basename, extension);
}

bool captureScreenshot(
    SDL_Renderer* renderer,
    const WindowConfig& window,
    const std::string& project_root,
    const std::string& context_slug) {
    (void)window;
    if (!renderer) {
        return false;
    }
    int frame_width = 0;
    int frame_height = 0;
    if (SDL_GetRendererOutputSize(renderer, &frame_width, &frame_height) != 0 ||
        frame_width <= 0 || frame_height <= 0) {
        std::cerr << "Screenshot error: could not query renderer output size: "
                  << SDL_GetError() << '\n';
        return false;
    }

    std::vector<unsigned char> frame_buffer(
        static_cast<std::size_t>(frame_width) * static_cast<std::size_t>(frame_height) * 3U);
    if (SDL_RenderReadPixels(
            renderer,
            nullptr,
            SDL_PIXELFORMAT_RGB24,
            frame_buffer.data(),
            frame_width * 3) != 0) {
        std::cerr << "Screenshot error: SDL_RenderReadPixels failed: "
                  << SDL_GetError() << '\n';
        return false;
    }

    const auto output_path = buildScreenshotOutputPath(project_root, context_slug);
    if (!output_path) {
        return false;
    }
    const bool can_encode_webp = output_path->extension() == ".webp";

    std::ostringstream cmd;
    cmd << "ffmpeg -y -f rawvideo -pix_fmt rgb24 -s "
        << frame_width << "x" << frame_height
        << " -i - -frames:v 1 "
        << (can_encode_webp ? "-c:v libwebp -lossless 1 " : "-c:v png ")
        << "\"" << output_path->string() << "\" >/dev/null 2>&1";
    FILE* ffmpeg_pipe = popen(cmd.str().c_str(), "w");
    if (!ffmpeg_pipe) {
        std::cerr << "Screenshot error: could not start ffmpeg. Is ffmpeg installed?\n";
        return false;
    }
    const std::size_t bytes = frame_buffer.size();
    const std::size_t wrote = std::fwrite(frame_buffer.data(), 1, bytes, ffmpeg_pipe);
    const int close_code = pclose(ffmpeg_pipe);
    if (wrote != bytes || close_code != 0) {
        std::cerr << "Screenshot error: ffmpeg write/encode failed\n";
        return false;
    }
    if (!can_encode_webp) {
        std::cerr << "Screenshot note: ffmpeg WebP encoder not found; saved PNG instead.\n";
    }
    std::cout << "Screenshot saved: " << *output_path << '\n';
    return true;
}

class ScreenshotFlashOverlay {
public:
    void trigger() { remaining_seconds_ = kDurationSeconds; }
    void update(double dt) { remaining_seconds_ = std::max(0.0, remaining_seconds_ - dt); }

    void render(SDL_Renderer* renderer, const WindowConfig& window) const {
        if (!renderer || remaining_seconds_ <= 0.0) {
            return;
        }
        const double alpha01 = std::clamp(remaining_seconds_ / kDurationSeconds, 0.0, 1.0);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, static_cast<Uint8>(std::round(alpha01 * 180.0)));
        const SDL_Rect full{0, 0, window.virtual_width, window.virtual_height};
        SDL_RenderFillRect(renderer, &full);
    }

private:
    static constexpr double kDurationSeconds = 0.16;
    double remaining_seconds_ = 0.0;
};

void renderRecordingIndicator(SDL_Renderer* renderer, const WindowConfig& window, bool recording) {
    if (!renderer || !recording) {
        return;
    }

    const int pad = 14;
    const int radius = 7;
    const int center_x = pad + radius;
    const int center_y = pad + radius;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 20, 20, 20, 180);
    SDL_Rect bg{center_x - radius - 4, center_y - radius - 4, radius * 2 + 8, radius * 2 + 8};
    SDL_RenderFillRect(renderer, &bg);
    SDL_SetRenderDrawColor(renderer, 220, 32, 32, 255);
    renderFilledCircle(renderer, center_x, center_y, radius);
    (void)window;
}

class AppScreenRecorder {
public:
    explicit AppScreenRecorder(std::string project_root)
        : project_root_(std::move(project_root)) {}

    bool isRecording() const { return recording_; }

    void toggle(SDL_Renderer* renderer, const WindowConfig& window) {
        if (recording_) {
            stop();
            return;
        }
        start(renderer, window);
    }

    void capture(SDL_Renderer* renderer, const WindowConfig& window) {
        (void)window;
        if (!recording_ || !ffmpeg_pipe_ || frame_width_ <= 0 || frame_height_ <= 0) {
            return;
        }
        if (frame_buffer_.empty()) {
            frame_buffer_.resize(static_cast<std::size_t>(frame_width_) *
                                 static_cast<std::size_t>(frame_height_) * 3U);
        }
        if (SDL_RenderReadPixels(
                renderer,
                nullptr,
                SDL_PIXELFORMAT_RGB24,
                frame_buffer_.data(),
                frame_width_ * 3) != 0) {
            std::cerr << "Recording warning: SDL_RenderReadPixels failed: "
                      << SDL_GetError() << '\n';
            return;
        }
        const std::size_t bytes = frame_buffer_.size();
        if (std::fwrite(frame_buffer_.data(), 1, bytes, ffmpeg_pipe_) != bytes) {
            std::cerr << "Recording warning: failed writing frame to ffmpeg pipe\n";
        }
    }

    ~AppScreenRecorder() { stop(); }

private:
    std::string project_root_;
    bool recording_ = false;
    FILE* ffmpeg_pipe_ = nullptr;
    std::vector<unsigned char> frame_buffer_{};
    int frame_width_ = 0;
    int frame_height_ = 0;

    void start(SDL_Renderer* renderer, const WindowConfig& window) {
        (void)window;
        const fs::path recordings_dir = fs::path(project_root_) / "recordings";
        std::error_code ec;
        fs::create_directories(recordings_dir, ec);
        if (ec) {
            std::cerr << "Recording error: cannot create recordings directory: "
                      << ec.message() << '\n';
            return;
        }

        const fs::path output_path = recordings_dir / ("recording_" + timestampNow() + ".mp4");
        if (SDL_GetRendererOutputSize(renderer, &frame_width_, &frame_height_) != 0 ||
            frame_width_ <= 0 || frame_height_ <= 0) {
            std::cerr << "Recording error: could not query renderer output size: "
                      << SDL_GetError() << '\n';
            frame_width_ = 0;
            frame_height_ = 0;
            return;
        }
        std::ostringstream cmd;
        cmd << "ffmpeg -y -f rawvideo -pix_fmt rgb24 -s "
            << frame_width_ << "x" << frame_height_
            << " -r 60 -i - -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p "
            << "\"" << output_path.string() << "\" >/dev/null 2>&1";

        ffmpeg_pipe_ = popen(cmd.str().c_str(), "w");
        if (!ffmpeg_pipe_) {
            std::cerr << "Recording error: could not start ffmpeg. Is ffmpeg installed?\n";
            return;
        }
        frame_buffer_.clear();
        recording_ = true;
        std::cout << "Recording started: " << output_path << '\n';
    }

    void stop() {
        if (ffmpeg_pipe_) {
            pclose(ffmpeg_pipe_);
            ffmpeg_pipe_ = nullptr;
        }
        if (recording_) {
            std::cout << "Recording stopped.\n";
        }
        recording_ = false;
        frame_width_ = 0;
        frame_height_ = 0;
        frame_buffer_.clear();
    }
};

class FrameCounterOverlay {
public:
    FrameCounterOverlay(const std::string& font_path, const std::string& project_root)
        : font_(loadFontPreferringUnicode(font_path, 20, project_root)) {}

    void update(SDL_Renderer* renderer, double dt) {
        accum_seconds_ += dt;
        ++accum_frames_;
        if (accum_seconds_ < 0.20) return;
        const double fps = static_cast<double>(accum_frames_) / std::max(0.0001, accum_seconds_);
        accum_seconds_ = 0.0;
        accum_frames_ = 0;
        const int fps_int = static_cast<int>(std::lround(fps));
        if (fps_int == last_fps_) return;
        last_fps_ = fps_int;
        label_ = "FPS: " + std::to_string(fps_int);
        texture_dirty_ = true;
        rebuildTextureIfNeeded(renderer);
    }

    void render(SDL_Renderer* renderer, int logical_w, int logical_h) const {
        if (!renderer || !texture_.texture || width_ <= 0 || height_ <= 0) return;
        const int margin = 12;
        SDL_Rect dst{
            std::max(0, logical_w - width_ - margin),
            margin,
            width_,
            height_};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 140);
        SDL_Rect bg{std::max(0, dst.x - 6), std::max(0, dst.y - 4), dst.w + 12, dst.h + 8};
        SDL_RenderFillRect(renderer, &bg);
        SDL_RenderCopy(renderer, texture_.texture.get(), nullptr, &dst);
    }

    void prepareSdlTexture(SDL_Renderer* renderer) {
        rebuildTextureIfNeeded(renderer);
    }

    const std::string& label() const {
        return label_;
    }

private:
    FontHandle font_{};
    TextureHandle texture_{};
    int width_ = 0;
    int height_ = 0;
    int last_fps_ = -1;
    double accum_seconds_ = 0.0;
    int accum_frames_ = 0;
    bool texture_dirty_ = false;
    std::string label_{};

    void rebuildTextureIfNeeded(SDL_Renderer* renderer) {
        if (!texture_dirty_ || !renderer || !font_ || label_.empty()) return;
        rebuildTexture(renderer, label_);
        texture_dirty_ = false;
    }

    void rebuildTexture(SDL_Renderer* renderer, const std::string& text) {
        texture_ = TextureHandle{};
        width_ = 0;
        height_ = 0;
        SDL_Color white{245, 245, 245, 255};
        SDL_Surface* surface = TTF_RenderUTF8_Blended(font_.get(), text.c_str(), white);
        if (!surface) return;
        SDL_Texture* raw = SDL_CreateTextureFromSurface(renderer, surface);
        width_ = surface->w;
        height_ = surface->h;
        SDL_FreeSurface(surface);
        if (!raw) return;
        texture_.texture.reset(raw, SDL_DestroyTexture);
        texture_.width = width_;
        texture_.height = height_;
        SDL_SetTextureBlendMode(texture_.texture.get(), SDL_BLENDMODE_BLEND);
    }
};

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

    const bool bgfx_world3d_enabled = world3dBackendWantsBgfx(app_config);
    RendererPtr renderer = createAppRenderer(window.get(), config.window);
    logRendererInfo(renderer.get());

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
    Overworld3DTestScreen overworld3d_test(root);
    AttendTestScreen attend_test(root, app_config);
    UserSettingsPersistence user_settings_persistence(
        config.persistence.save_options,
        save_file_path,
        backup_file_path);
    UserSettings user_settings;
    if (user_settings_persistence.load(user_settings)) {
        title_screen.applyUserSettings(user_settings);
    }

    AppAudioDirector audio(root, config.audio);
    AppScreenCoordinator screen_coordinator(title_screen, loading, transfer_flow, overworld3d_test, attend_test);
    AppScreenRecorder recorder(root);
    std::unique_ptr<FrameCounterOverlay> frame_counter;
    if (app_config.enable_frame_counter) {
        frame_counter = std::make_unique<FrameCounterOverlay>(config.assets.font, root);
    }
    ScreenshotFlashOverlay screenshot_flash;
    std::vector<std::string> record_toggle_keys = config.input.record_toggle_keys;
    if (record_toggle_keys.empty() && app_config.recording.enabled && !app_config.recording.hotkey.empty()) {
        record_toggle_keys.push_back(app_config.recording.hotkey);
    }
    const std::vector<std::string>& screenshot_keys = config.input.screenshot_keys;

    bool running = true;
    bool screenshot_requested = false;
    Uint64 last_counter = SDL_GetPerformanceCounter();
    InputRouter input_router;
    WindowPresentation presentation = WindowPresentation::Sdl2D;
    int pending_sdl_recreate_frames = 0;
    SDL_MetalView sdl_metal_view = nullptr;

    while (running) {
        const Uint64 frame_start_counter = SDL_GetPerformanceCounter();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
                continue;
            }
            if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                const SDL_Keycode key = event.key.keysym.sym;
                const bool bgfx_keys =
                    (screen_coordinator.activeScreen() == &overworld3d_test &&
                     overworld3d_test.wantsBgfxRenderer()) ||
                    (screen_coordinator.activeScreen() == &attend_test &&
                     attend_test.wantsBgfxRenderer());
                if (!record_toggle_keys.empty() && matchesBinding(key, record_toggle_keys)) {
                    if (renderer) {
                        recorder.toggle(renderer.get(), config.window);
                    } else if (bgfx_keys) {
                        std::cerr << "[App] Screen recording is not available in bgfx 3D mode yet. "
                                  << "Press T for screenshots.\n";
                    }
                    continue;
                }
                if (!screenshot_keys.empty() && matchesBinding(key, screenshot_keys)) {
                    screenshot_requested = true;
                    continue;
                }
            }

            input_router.handleEvent(event, config.input, screen_coordinator.activeInput());
        }

        Uint64 now = SDL_GetPerformanceCounter();
        double dt = static_cast<double>(now - last_counter) / static_cast<double>(SDL_GetPerformanceFrequency());
        last_counter = now;

        input_router.update(dt, screen_coordinator.activeInput());
        screen_coordinator.update(dt);
        screenshot_flash.update(dt);

        audio.updateMusic(dt, screen_coordinator.musicRequest());
        audio.playSfx(screen_coordinator.consumeSfxRequests(), screen_coordinator.sfxVolume());
        if (auto settings = screen_coordinator.consumeUserSettingsSaveRequest()) {
            user_settings_persistence.save(*settings);
        }
        if (frame_counter) {
            frame_counter->update(renderer.get(), dt);
        }

        const bool overworld_wants_bgfx =
            screen_coordinator.activeScreen() == &overworld3d_test && overworld3d_test.wantsBgfxRenderer();
        const bool attend_wants_bgfx =
            screen_coordinator.activeScreen() == &attend_test && attend_test.wantsBgfxRenderer();
        const bool active_wants_bgfx = overworld_wants_bgfx || attend_wants_bgfx;

        if (active_wants_bgfx) {
            if (presentation != WindowPresentation::Bgfx3D) {
                releaseSdlUi(renderer, title_screen, loading, transfer_flow);
                SDL_PumpEvents();
#if defined(__APPLE__)
                if (!sdl_metal_view) {
                    sdl_metal_view = SDL_Metal_CreateView(window.get());
                    if (!sdl_metal_view) {
                        std::cerr << "[App] SDL_Metal_CreateView failed: " << SDL_GetError() << '\n';
                    } else {
                        std::cerr << "[App] Created SDL_MetalView for bgfx presentation\n";
                    }
                }
                if (!sdl_metal_view) {
                    presentation = WindowPresentation::Sdl2D;
                    pending_sdl_recreate_frames = 0;
                } else
#endif
                {
                    presentation = WindowPresentation::Bgfx3D;
                    pending_sdl_recreate_frames = 0;
                    std::cerr << "[App] Presentation: Sdl2D -> Bgfx3D (SDL renderer released before bgfx init)\n";
                }
            }
        } else if (presentation == WindowPresentation::Bgfx3D) {
#if defined(__APPLE__)
            if (sdl_metal_view) {
                SDL_Metal_DestroyView(sdl_metal_view);
                sdl_metal_view = nullptr;
                std::cerr << "[App] Destroyed SDL_MetalView before SDL UI recreate\n";
            }
#endif
            presentation = WindowPresentation::Sdl2D;
            pending_sdl_recreate_frames = 1;
            std::cerr << "[App] Presentation: Bgfx3D -> Sdl2D (waiting before SDL recreate)\n";
        }

        if (presentation == WindowPresentation::Sdl2D && !renderer && bgfx_world3d_enabled) {
            if (pending_sdl_recreate_frames > 0) {
                pending_sdl_recreate_frames--;
            } else {
                rebindSdlUi(
                    renderer,
                    window.get(),
                    config,
                    root,
                    title_screen,
                    loading,
                    transfer_flow);
                std::cerr << "[App] Presentation: SDL renderer recreated for 2D UI\n";
            }
        }

        const bool use_bgfx_presenter = presentation == WindowPresentation::Bgfx3D && active_wants_bgfx;

        bool bgfx_frame_presented = false;
        bool bgfx_screenshot_this_frame = false;
        if (use_bgfx_presenter) {
            int framebuffer_w = 0;
            int framebuffer_h = 0;
#if defined(__APPLE__)
            if (sdl_metal_view) {
                SDL_Metal_GetDrawableSize(window.get(), &framebuffer_w, &framebuffer_h);
            } else
#endif
            {
                SDL_GetWindowSize(window.get(), &framebuffer_w, &framebuffer_h);
            }
            const int logical_w = config.window.virtual_width;
            const int logical_h = config.window.virtual_height;
            if (screenshot_requested) {
                if (const auto output_path = buildScreenshotOutputPath(
                        root, screen_coordinator.screenshotNameContext(), true)) {
                    if (overworld_wants_bgfx) {
                        overworld3d_test.queueBgfxScreenshot(output_path->string());
                    } else if (attend_wants_bgfx) {
                        attend_test.queueBgfxScreenshot(output_path->string());
                    }
                    bgfx_screenshot_this_frame = true;
                }
                screenshot_requested = false;
            }
            if (overworld_wants_bgfx) {
                bgfx_frame_presented = overworld3d_test.renderBgfx(
                    window.get(),
                    std::max(1, framebuffer_w),
                    std::max(1, framebuffer_h),
                    std::max(1, logical_w),
                    std::max(1, logical_h),
                    sdl_metal_view,
                    frame_counter ? frame_counter->label() : std::string{});
            } else if (attend_wants_bgfx) {
                bgfx_frame_presented = attend_test.renderBgfx(
                    window.get(),
                    std::max(1, framebuffer_w),
                    std::max(1, framebuffer_h),
                    sdl_metal_view);
            }
            if (!bgfx_frame_presented && presentation == WindowPresentation::Bgfx3D) {
#if defined(__APPLE__)
                if (sdl_metal_view) {
                    SDL_Metal_DestroyView(sdl_metal_view);
                    sdl_metal_view = nullptr;
                }
#endif
                presentation = WindowPresentation::Sdl2D;
                pending_sdl_recreate_frames = 1;
                std::cerr << "[App] Presentation: Bgfx3D init failed, falling back to Sdl2D\n";
            }
        }

        if (!bgfx_frame_presented && renderer) {
            if (Screen* screen = screen_coordinator.activeScreen()) {
                screen->render(renderer.get());
            }

            renderBlackOverlay(renderer.get(), config.window, screen_coordinator.transitionOverlayAlpha());
            recorder.capture(renderer.get(), config.window);
            if (screenshot_requested) {
                if (captureScreenshot(
                        renderer.get(),
                        config.window,
                        root,
                        screen_coordinator.screenshotNameContext())) {
                    screenshot_flash.trigger();
                }
                screenshot_requested = false;
            }

            if (frame_counter) {
                frame_counter->prepareSdlTexture(renderer.get());
                frame_counter->render(renderer.get(), config.window.virtual_width, config.window.virtual_height);
            }
            if (screen_coordinator.activeScreen() == &overworld3d_test) {
                overworld3d_test.renderPresentationOverlay(renderer.get());
            }
            renderRecordingIndicator(renderer.get(), config.window, recorder.isRecording());
            screenshot_flash.render(renderer.get(), config.window);
            SDL_RenderPresent(renderer.get());
        } else if (bgfx_frame_presented) {
            if (bgfx_screenshot_this_frame) {
                screenshot_flash.trigger();
            }
            if (renderer) {
                screenshot_flash.render(renderer.get(), config.window);
            }
            if (screen_coordinator.activeScreen() == &overworld3d_test && renderer) {
                overworld3d_test.renderPresentationOverlay(renderer.get());
                SDL_SetRenderDrawBlendMode(renderer.get(), SDL_BLENDMODE_BLEND);
                SDL_RenderPresent(renderer.get());
            } else if (screen_coordinator.activeScreen() == &attend_test && renderer) {
                attend_test.renderPresentationOverlay(renderer.get());
                SDL_SetRenderDrawBlendMode(renderer.get(), SDL_BLENDMODE_BLEND);
                SDL_RenderPresent(renderer.get());
            }
        }

        if (app_config.target_fps > 0) {
            const double target_seconds = 1.0 / static_cast<double>(app_config.target_fps);
            const Uint64 frame_end_counter = SDL_GetPerformanceCounter();
            const double elapsed_seconds = static_cast<double>(frame_end_counter - frame_start_counter) /
                                           static_cast<double>(SDL_GetPerformanceFrequency());
            if (elapsed_seconds < target_seconds) {
                const double remain_ms = (target_seconds - elapsed_seconds) * 1000.0;
                if (remain_ms > 0.0) {
                    SDL_Delay(static_cast<Uint32>(std::floor(remain_ms)));
                }
            }
        }
    }

#if defined(__APPLE__)
    if (sdl_metal_view) {
        SDL_Metal_DestroyView(sdl_metal_view);
    }
#endif

    return 0;
}

} // namespace pr
