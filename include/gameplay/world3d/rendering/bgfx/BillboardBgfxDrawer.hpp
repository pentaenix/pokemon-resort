#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"
#include "gameplay/world3d/rendering/BillboardPlacement.hpp"
#include "gameplay/world3d/rendering/SpriteShadowDecal.hpp"

#include <SDL.h>
#include <bgfx/bgfx.h>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace pr::gameplay::world3d::rendering::bgfx_backend {

// GPU billboard submission from pre-built BillboardPlacement (no terrain height sampling).
class BillboardBgfxDrawer {
public:
    struct TextureGpuResource {
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        int width = 0;
        int height = 0;
        bool valid() const { return bgfx::isValid(handle); }
    };

    struct CharacterGpuTextures {
        TextureGpuResource color;
        TextureGpuResource white;
        TextureGpuResource run_color;
        TextureGpuResource run_white;
    };

    struct Dependencies {
        bgfx::VertexLayout layout{};
        bgfx::ProgramHandle billboard_program = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle tex_uniform = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle tint_cutoff_uniform = BGFX_INVALID_HANDLE;
        bgfx::ViewId view_id = 0;
        int base_viewport_w = 1;
        int base_viewport_h = 1;
        int render_viewport_w = 1;
        int render_viewport_h = 1;
        int internal_scale = 1;
        const SceneConfig* scene = nullptr;
        TextureGpuResource shadow_texture{};
        std::function<CharacterGpuTextures(const CharacterSpriteDefinition&)> textures_for_character;
        std::function<TextureGpuResource(
            const std::string& cache_key,
            const std::vector<std::uint8_t>& png_bytes,
            const std::string& fallback_path,
            const char* debug_name)>
            texture_for_key;
    };

    explicit BillboardBgfxDrawer(Dependencies dependencies);

    void setWorldViewport(int base_width, int base_height, int render_width, int render_height, int internal_scale);

    void submitCharacterDraw(
        const camera::Gen4FollowCamera& camera,
        const CharacterBillboardDraw& draw) const;

    void submitCharacterShadow(
        const camera::Gen4FollowCamera& camera,
        const CharacterBillboardDraw& draw) const;

    void submitTextureDraw(
        const camera::Gen4FollowCamera& camera,
        const TextureBillboardDraw& draw) const;

private:
    struct Vertex {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        std::uint32_t abgr = 0xffffffffu;
        float u = 0.0f;
        float v = 0.0f;
    };

    Dependencies deps_;

    void submitGroundShadow(
        const camera::Gen4FollowCamera& camera,
        const BillboardPlacement& placement,
        const SDL_Rect& source_rect,
        const SpriteShadowConfig& shadow_cfg) const;

    void submitBillboardQuad(
        const camera::Gen4FollowCamera& camera,
        const BillboardPlacement& placement,
        const TextureGpuResource& texture,
        const SDL_Rect& source_rect,
        float tint_r,
        float tint_g,
        float tint_b,
        float vertex_alpha,
        float alpha_cutoff,
        std::uint64_t state) const;

    void submitWorldBillboardQuad(
        const camera::Gen4FollowCamera& camera,
        const BillboardPlacement& placement,
        const TextureGpuResource& texture,
        const SDL_Rect& source_rect,
        int screen_offset_x_px,
        float tint_r,
        float tint_g,
        float tint_b,
        float vertex_alpha,
        float alpha_cutoff,
        float depth_priority_bias,
        std::uint64_t state) const;
};

} // namespace pr::gameplay::world3d::rendering::bgfx_backend
