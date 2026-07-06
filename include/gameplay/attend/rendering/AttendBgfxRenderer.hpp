#pragma once

#include "gameplay/attend/AttendSceneConfig.hpp"
#include "gameplay/world3d/rendering/bgfx/BgfxBackend.hpp"

#include <SDL.h>
#include <memory>
#include <string>
#include <vector>

namespace pr::gameplay::attend::rendering {

struct AttendBgfxOverlayButton {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    std::string label;
    AttendOverlayButtonConfig style{};
};

class AttendBgfxRenderer {
public:
    AttendBgfxRenderer(std::string project_root, AttendSceneConfig config);
    AttendBgfxRenderer(const AttendBgfxRenderer&) = delete;
    AttendBgfxRenderer& operator=(const AttendBgfxRenderer&) = delete;
    ~AttendBgfxRenderer();

    bool initialize(
        SDL_Window* window,
        int width,
        int height,
        const std::string& bgfx_preference,
        void* sdl_metal_view = nullptr);
    void shutdown();
    bool valid() const;
    std::string lastError() const;
    void setPetting(bool petting);
    void setPetContact(float vertical_bias);
    void setFaceLook(float x, float y);
    void setViewportLook(float x, float y);
    void setWeatherMode(int index);
    void setTextureVariant(int index);
    void setFormVariant(int index);
    void triggerReaction(const std::string& reaction_id, double interaction_seconds = 0.0);
    void setOverlayButtons(std::vector<AttendBgfxOverlayButton> buttons, int logical_w, int logical_h);
    void setFaceView(bool face_view);
    bool faceViewAvailable() const;
    SDL_Rect pokemonPointerRect() const;
    int weatherModeCount() const;
    int textureVariantIndex() const;
    int textureVariantCount() const;
    std::string textureVariantLabel(int index) const;
    int formVariantIndex() const;
    int formVariantCount() const;
    std::string formVariantLabel(int index) const;
    void render(double scene_time_seconds, int width, int height);
    void queueScreenshot(const std::string& output_path);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pr::gameplay::attend::rendering
