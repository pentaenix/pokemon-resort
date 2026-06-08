#pragma once

#include <SDL.h>
#include <cstdint>
#include <string>

namespace pr::gameplay::world3d::rendering::bgfx_backend {

class BgfxBackend {
public:
    BgfxBackend() = default;
    BgfxBackend(const BgfxBackend&) = delete;
    BgfxBackend& operator=(const BgfxBackend&) = delete;
    ~BgfxBackend();

    bool initialize(
        SDL_Window* window,
        int width,
        int height,
        const std::string& renderer_preference,
        void* sdl_metal_view = nullptr);
    void shutdown();
    void reset(int width, int height);
    void beginFrame(float clear_r, float clear_g, float clear_b, float clear_a);
    void endFrame();

    bool valid() const { return initialized_; }
    bool homogeneousDepth() const { return homogeneous_depth_; }
    std::string shaderDirectory() const;
    std::string shaderSubdirectory() const;
    std::string rendererName() const;
    std::string lastError() const { return last_error_; }

private:
    bool initialized_ = false;
    bool homogeneous_depth_ = true;
    int width_ = 0;
    int height_ = 0;
    int renderer_type_ = 0;
    std::string last_error_;
};

} // namespace pr::gameplay::world3d::rendering::bgfx_backend
