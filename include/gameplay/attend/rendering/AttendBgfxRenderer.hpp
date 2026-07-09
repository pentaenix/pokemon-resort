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

struct AttendBgfxCornerButton {
    bool left = true;
    bool top = false;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    AttendCornerButtonConfig style{};
};

struct AttendBgfxProfilePlate {
    bool enabled = true;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    std::string name;
    std::string characteristic;
    std::string nature;
    std::string sprite_path;
    AttendProfilePlateConfig style{};
};

struct AttendFreeCameraPose {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yaw_degrees = 0.0f;
    float pitch_degrees = 0.0f;
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
    void setFreeCamera(bool enabled, AttendFreeCameraPose pose = {});
    bool currentCameraPose(AttendFreeCameraPose& out) const;
    void setWeatherMode(int index);
    void setTextureVariant(int index);
    void setFormVariant(int index);
    bool canTriggerReaction(const std::string& reaction_id, double interaction_seconds = 0.0) const;
    bool canTriggerSemanticAnimation(const std::string& animation_semantic) const;
    void triggerReaction(const std::string& reaction_id, double interaction_seconds = 0.0);
    void triggerSemanticAnimation(
        const std::string& animation_semantic,
        double duration_seconds = 0.0,
        double fade_in_seconds = 0.2,
        double fade_out_seconds = 0.3,
        const std::string& eye_expression = {},
        const std::string& mouth_expression = {});
    double wakeFromIdleAnimation();
    bool isIdleSleeping() const;
    void blendOutIdleAnimation(double scene_time_seconds, double fade_out_seconds);
    void setOverlayButtons(std::vector<AttendBgfxOverlayButton> buttons, int logical_w, int logical_h);
    void setCornerButtons(std::vector<AttendBgfxCornerButton> buttons, int logical_w, int logical_h);
    void setProfilePlate(AttendBgfxProfilePlate plate, int logical_w, int logical_h);
    void setFaceView(bool face_view);
    bool faceViewTransitionActive() const;
    bool faceViewAvailable() const;
    SDL_Rect pokemonPointerRect() const;
    int weatherModeCount() const;
    int textureVariantIndex() const;
    int textureVariantCount() const;
    std::string textureVariantLabel(int index) const;
    int formVariantIndex() const;
    int formVariantCount() const;
    std::string formVariantId(int index) const;
    std::string formVariantLabel(int index) const;
    void render(double scene_time_seconds, int width, int height);
    void queueScreenshot(const std::string& output_path);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pr::gameplay::attend::rendering
