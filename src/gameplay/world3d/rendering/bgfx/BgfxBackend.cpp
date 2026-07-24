#include "gameplay/world3d/rendering/bgfx/BgfxBackend.hpp"

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include <SDL_syswm.h>
#include <SDL_metal.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>

namespace pr::gameplay::world3d::rendering::bgfx_backend {

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

void* nativeWindowHandle(SDL_Window* window, bgfx::NativeWindowHandleType::Enum& type, void*& ndt) {
    type = bgfx::NativeWindowHandleType::Default;
    ndt = nullptr;
    if (!window) return nullptr;

    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    if (!SDL_GetWindowWMInfo(window, &wmi)) {
        return nullptr;
    }

#if defined(_WIN32)
    return wmi.info.win.window;
#elif defined(__APPLE__)
    return wmi.info.cocoa.window;
#elif defined(__ANDROID__)
    return wmi.info.android.window;
#elif defined(__linux__)
    if (wmi.subsystem == SDL_SYSWM_WAYLAND) {
        ndt = wmi.info.wl.display;
        type = bgfx::NativeWindowHandleType::Wayland;
        return wmi.info.wl.surface;
    }
    ndt = wmi.info.x11.display;
    return reinterpret_cast<void*>(static_cast<uintptr_t>(wmi.info.x11.window));
#else
    return nullptr;
#endif
}

bgfx::RendererType::Enum rendererTypeFromConfig(const std::string& value) {
    const std::string v = lower(value);
    if (v == "vulkan") return bgfx::RendererType::Vulkan;
    if (v == "opengl" || v == "gl") return bgfx::RendererType::OpenGL;
    if (v == "metal") return bgfx::RendererType::Metal;
    if (v == "d3d11" || v == "direct3d11") return bgfx::RendererType::Direct3D11;
    return bgfx::RendererType::Count;
}

std::string shaderFolder(bgfx::RendererType::Enum type) {
    switch (type) {
        case bgfx::RendererType::Direct3D11: return "dxbc";
        case bgfx::RendererType::Direct3D12: return "dxil";
        case bgfx::RendererType::Metal: return "metal";
        case bgfx::RendererType::OpenGL: return "glsl";
        case bgfx::RendererType::OpenGLES: return "essl";
        case bgfx::RendererType::Vulkan: return "spirv";
        default: return "glsl";
    }
}

} // namespace

BgfxBackend::~BgfxBackend() {
    shutdown();
}

bool BgfxBackend::initialize(
    SDL_Window* window,
    int width,
    int height,
    const std::string& renderer_preference,
    void* sdl_metal_view) {
    if (initialized_) {
        reset(width, height);
        return true;
    }

    bgfx::NativeWindowHandleType::Enum window_type = bgfx::NativeWindowHandleType::Default;
    void* ndt = nullptr;
    void* nwh = nullptr;
#if defined(__APPLE__)
    if (sdl_metal_view) {
        nwh = SDL_Metal_GetLayer(static_cast<SDL_MetalView>(sdl_metal_view));
        if (!nwh) {
            last_error_ = std::string("SDL_Metal_GetLayer failed: ") + SDL_GetError();
            return false;
        }
        std::cerr << "[BgfxBackend] Using CAMetalLayer from SDL_MetalView\n";
    }
#endif
    if (!nwh) {
        nwh = nativeWindowHandle(window, window_type, ndt);
        if (!nwh) {
            last_error_ = "Could not query SDL native window handle for bgfx";
            return false;
        }
    }

    bgfx::Init init;
    init.type = rendererTypeFromConfig(renderer_preference);
    init.platformData.ndt = ndt;
    init.platformData.nwh = nwh;
    init.platformData.type = window_type;
    init.resolution.width = static_cast<std::uint32_t>(std::max(1, width));
    init.resolution.height = static_cast<std::uint32_t>(std::max(1, height));
    init.resolution.reset = BGFX_RESET_VSYNC;

    if (!bgfx::init(init)) {
        last_error_ = "bgfx::init failed";
        return false;
    }

    initialized_ = true;
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    renderer_type_ = static_cast<int>(bgfx::getRendererType());
    homogeneous_depth_ = bgfx::getCaps()->homogeneousDepth;
    origin_bottom_left_ = bgfx::getCaps()->originBottomLeft;
    bgfx::setDebug(BGFX_DEBUG_NONE);
    std::cerr << "[BgfxBackend] Initialized renderer="
              << bgfx::getRendererName(static_cast<bgfx::RendererType::Enum>(renderer_type_))
              << " shaders=" << shaderSubdirectory()
              << " size=" << width_ << "x" << height_
              << '\n';
    return true;
}

void BgfxBackend::shutdown() {
    if (!initialized_) return;

    // Resource creation can fail before the first application frame is submitted
    // (for example after a Metal device/layer wake transition). The Metal backend
    // may already have opened a blit encoder for bgfx's internal uploads at that
    // point. Give it one ordinary frame to finish that encoder before entering
    // bgfx's renderer-shutdown sequence; otherwise Metal aborts while releasing an
    // encoder that never received endEncoding(). This is also safe for the normal
    // shutdown path and drains resource-destroy commands queued by the owner.
    bgfx::frame();
    bgfx::shutdown();
    initialized_ = false;
    renderer_type_ = static_cast<int>(bgfx::RendererType::Noop);
}

void BgfxBackend::reset(int width, int height) {
    if (!initialized_) return;
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    bgfx::reset(static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_), BGFX_RESET_VSYNC);
}

void BgfxBackend::beginFrame(float clear_r, float clear_g, float clear_b, float clear_a) {
    if (!initialized_) return;
    const auto c = [](float v) -> std::uint32_t {
        return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    const std::uint32_t rgba = (c(clear_r) << 24U) | (c(clear_g) << 16U) | (c(clear_b) << 8U) | c(clear_a);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, rgba, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, static_cast<std::uint16_t>(width_), static_cast<std::uint16_t>(height_));
    bgfx::touch(0);
}

void BgfxBackend::endFrame() {
    if (initialized_) {
        if (!pending_screenshot_path_.empty()) {
            bgfx::requestScreenShot(BGFX_INVALID_HANDLE, pending_screenshot_path_.c_str());
            std::cout << "Screenshot saved: " << pending_screenshot_path_ << '\n';
            pending_screenshot_path_.clear();
        }
        bgfx::frame();
    }
}

void BgfxBackend::queueScreenshot(const std::string& output_path) {
    pending_screenshot_path_ = output_path;
}

std::string BgfxBackend::shaderDirectory() const {
    char* base = SDL_GetBasePath();
    std::filesystem::path root = base ? std::filesystem::path(base) : std::filesystem::current_path();
    if (base) SDL_free(base);
    return (root / "shaders").string();
}

std::string BgfxBackend::shaderSubdirectory() const {
    return shaderFolder(static_cast<bgfx::RendererType::Enum>(renderer_type_));
}

std::string BgfxBackend::rendererName() const {
    return bgfx::getRendererName(static_cast<bgfx::RendererType::Enum>(renderer_type_));
}

} // namespace pr::gameplay::world3d::rendering::bgfx_backend
