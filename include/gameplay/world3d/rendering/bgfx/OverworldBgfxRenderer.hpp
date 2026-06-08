#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"
#include "gameplay/world3d/rendering/BillboardPlacement.hpp"

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

    void render(
        const camera::Gen4FollowCamera& camera,
        const camera::Vec3& player_pos,
        const SDL_Rect& player_source_rect,
        const terrain::ActorTerrainBinding& player_binding,
        int logical_w,
        int logical_h,
        int framebuffer_w,
        int framebuffer_h,
        const std::vector<rendering::CharacterBillboardDraw>& character_draws = {},
        const std::vector<rendering::TextureBillboardDraw>& texture_draws = {});

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pr::gameplay::world3d::rendering::bgfx_backend
