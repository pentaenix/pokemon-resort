#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"
#include "gameplay/world3d/rendering/bgfx/OverworldBgfxRenderer.hpp"
#include "gameplay/world3d/terrain/ActorTerrainBinding.hpp"

#include <SDL.h>

#include <filesystem>
#include <memory>
#include <string>

namespace pr::mapmaker {

// Owns the exact game renderer state needed by the native editor viewport.
// It never owns the process-wide bgfx frame boundary or global shutdown.
class ExactWorldPreview {
public:
    using ViewportTexture = gameplay::world3d::rendering::bgfx_backend::
        OverworldBgfxRenderer::EmbeddedViewportTexture;

    explicit ExactWorldPreview(std::filesystem::path project_root);
    ExactWorldPreview(const ExactWorldPreview&) = delete;
    ExactWorldPreview& operator=(const ExactWorldPreview&) = delete;
    ~ExactWorldPreview();

    // May be called before or after loadMap(). The SDL window and Metal view
    // remain externally owned. This does not begin or end an editor frame.
    bool initialize(
        SDL_Window* window,
        int framebuffer_width,
        int framebuffer_height,
        const std::string& bgfx_preference,
        void* sdl_metal_view = nullptr);
    void shutdown();

    // Transactional committed reloads: a failed load leaves the current live
    // scene intact. Call between ImGui frames because retiring the old game
    // renderer drains its deferred bgfx resource commands.
    bool loadMap(const std::filesystem::path& owmap_path);
    bool reloadMap();

    // Submits world views 0 and 1 only. The editor must composite the returned
    // texture on a later view and call bgfx::frame() exactly once.
    ViewportTexture render(
        int framebuffer_width,
        int framebuffer_height,
        double delta_seconds = 0.0);

    void setAnimationsEnabled(bool enabled);
    bool animationsEnabled() const;
    void setAnimationTimeSeconds(double seconds);
    double animationTimeSeconds() const;

    // Play-test input uses the shipping grid motor and terrain traversal rules.
    // Input is sampled once per editor frame and normalized to one cardinal axis.
    void setMovementInput(int dx, int dy);
    void resetPlayer();
    bool startPlayerAtTile(int tile_x, int tile_y);

    void focusMap();
    void focusPlayer();
    void focusTile(int tile_x, int tile_y);
    void focusWorld(gameplay::world3d::camera::Vec3 world);
    // Pointer-drag semantics: positive X/Y means the pointer moved right/down.
    void panScreenPixels(float delta_x, float delta_y, int viewport_height);
    void panWorld(float delta_world_x, float delta_world_z);
    void setZoomScale(float scale);
    void zoomByWheel(float wheel_delta);

    bool ready() const;
    const gameplay::world3d::SceneConfig* scene() const;
    const gameplay::world3d::camera::Gen4FollowCamera* camera() const;
    const std::filesystem::path& projectRoot() const;
    const std::filesystem::path& mapPath() const;
    const std::string& lastError() const;
    int sceneRebuildCount() const;
    float zoomScale() const;
    float zoomMinimum() const;
    float zoomMaximum() const;
    gameplay::world3d::camera::Vec3 cameraFocus() const;
    gameplay::world3d::camera::Vec3 playerPosition() const;
    const gameplay::world3d::terrain::ActorTerrainBinding* playerBinding() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pr::mapmaker
