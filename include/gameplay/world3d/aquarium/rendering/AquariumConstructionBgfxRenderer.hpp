#pragma once

#include "gameplay/world3d/aquarium/construction/AquariumConstructionVisual.hpp"

#include <bgfx/bgfx.h>

#include <memory>

namespace pr::gameplay::world3d::aquarium::rendering {

class AquariumConstructionBgfxRenderer {
public:
    AquariumConstructionBgfxRenderer();
    ~AquariumConstructionBgfxRenderer();
    AquariumConstructionBgfxRenderer(const AquariumConstructionBgfxRenderer&) = delete;
    AquariumConstructionBgfxRenderer& operator=(const AquariumConstructionBgfxRenderer&) = delete;

    void initialize(
        const bgfx::VertexLayout& layout,
        bgfx::ProgramHandle program,
        bgfx::TextureHandle white_texture,
        bgfx::UniformHandle texture_uniform,
        bgfx::UniformHandle tint_cutoff_uniform,
        bgfx::UniformHandle color_adjust_uniform,
        bgfx::UniformHandle texture_blur_uniform,
        bgfx::UniformHandle uv_offset_uniform,
        bgfx::UniformHandle light_dir_uniform,
        bgfx::UniformHandle light_params_uniform);
    void shutdown();
    void setVisual(construction::AquariumConstructionVisual visual);
    void submitWorld(std::uint16_t view_id);
    void submitHud(
        std::uint16_t view_id,
        int framebuffer_width,
        int framebuffer_height,
        int logical_width,
        int logical_height,
        bool homogeneous_depth);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pr::gameplay::world3d::aquarium::rendering
