#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"
#include "gameplay/world3d/dialogue/OverworldTextboxConfig.hpp"
#include "gameplay/world3d/rendering/BillboardPlacement.hpp"
#include "gameplay/world3d/aquarium/AquariumSimulation.hpp"

#include <SDL.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::rendering::bgfx_backend {

enum class MaterialClass {
    Opaque,
    MaskCutout,
    TrueBlend
};

class OverworldBgfxRenderer {
public:
    struct StaticMapChunk {
        SceneConfig scene;
        float origin_x = 0.0f;
        float origin_y = 0.0f;
        float origin_z = 0.0f;
    };

    // Header-friendly description of the renderer-owned color attachment used
    // by an embedded viewport. Consumers that already include bgfx may create a
    // bgfx::TextureHandle from texture_handle_idx. The handle remains owned by
    // this renderer and is invalidated by a target resize or shutdown.
    struct EmbeddedViewportTexture {
        static constexpr std::uint16_t invalid_texture_handle_idx =
            static_cast<std::uint16_t>(-1);

        std::uint16_t texture_handle_idx = invalid_texture_handle_idx;
        int width = 0;
        int height = 0;
        bool origin_bottom_left = false;

        bool valid() const {
            return texture_handle_idx != invalid_texture_handle_idx && width > 0 && height > 0;
        }
    };

    struct EmbeddedViewportOptions {
        // Editor rendering is deterministic by default. When enabled, all
        // ambient/model animation samples use animation_time_seconds instead of
        // the process clock; triggered door clips treat it as elapsed clip time.
        constexpr EmbeddedViewportOptions(
            bool enable_animations = false,
            double deterministic_time_seconds = 0.0)
            : animations_enabled(enable_animations),
              animation_time_seconds(deterministic_time_seconds) {}

        bool animations_enabled;
        double animation_time_seconds;
    };

    OverworldBgfxRenderer(
        std::string project_root,
        SceneConfig scene,
        CharacterSpriteDefinition character);
    OverworldBgfxRenderer(const OverworldBgfxRenderer&) = delete;
    OverworldBgfxRenderer& operator=(const OverworldBgfxRenderer&) = delete;
    ~OverworldBgfxRenderer();

    bool initialize(
        SDL_Window* window,
        int width,
        int height,
        const std::string& bgfx_preference,
        void* sdl_metal_view = nullptr);
    void shutdown();
    bool valid() const;
    std::string lastError() const;
    void setStaticMapChunks(std::vector<StaticMapChunk> chunks);
    void setAquariumPokemonActors(
        std::vector<aquarium::AquariumPokemonActor> actors);
    void setTextboxOverlay(dialogue::OverworldTextboxConfig config, bool visible, std::string text = {});
    void setAttendButtonOverlay(std::string icon_path, SDL_Rect logical_rect, bool visible);
    void setBlackIrisTransition(float logical_x, float logical_y, float closed_amount,
        bool visible, int circle_segments, float max_radius_scale);
    double playDoorTileAnimation(
        const std::string& map_id,
        const std::string& layer_id,
        int tile_x,
        int tile_y,
        bool reverse);

    void render(
        const camera::Gen4FollowCamera& camera,
        const camera::Vec3& player_pos,
        const SDL_Rect& player_source_rect,
        const std::string& player_activity_id,
        bool player_use_run_texture,
        bool player_draw_shadow,
        const terrain::ActorTerrainBinding& player_binding,
        int logical_w,
        int logical_h,
        int framebuffer_w,
        int framebuffer_h,
        const std::vector<rendering::CharacterBillboardDraw>& character_draws = {},
        const std::vector<rendering::TextureBillboardDraw>& texture_draws = {},
        const std::string& debug_frame_counter_label = {});

    // Submits the exact overworld world passes to the renderer's pixel-world
    // target without compositing to the backbuffer and without advancing bgfx.
    // The embedding host owns the eventual bgfx::frame() call and must sample
    // the returned texture from a view ordered after world views 0 and 1.
    EmbeddedViewportTexture renderEmbeddedViewport(
        const camera::Gen4FollowCamera& camera,
        const camera::Vec3& player_pos,
        const SDL_Rect& player_source_rect,
        const std::string& player_activity_id,
        bool player_use_run_texture,
        bool player_draw_shadow,
        const terrain::ActorTerrainBinding& player_binding,
        int logical_w,
        int logical_h,
        int framebuffer_w,
        int framebuffer_h,
        const std::vector<rendering::CharacterBillboardDraw>& character_draws = {},
        const std::vector<rendering::TextureBillboardDraw>& texture_draws = {},
        const EmbeddedViewportOptions& options = {});

    void queueScreenshot(const std::string& output_path);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pr::gameplay::world3d::rendering::bgfx_backend
