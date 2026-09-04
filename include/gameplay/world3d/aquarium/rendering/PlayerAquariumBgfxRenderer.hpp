#pragma once

#include "gameplay/world3d/aquarium/construction/AquariumPlayerRuntime.hpp"

#include <bgfx/bgfx.h>

#include <memory>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::aquarium::rendering {

class PlayerAquariumBgfxRenderer {
public:
    PlayerAquariumBgfxRenderer();
    ~PlayerAquariumBgfxRenderer();
    PlayerAquariumBgfxRenderer(const PlayerAquariumBgfxRenderer&) = delete;
    PlayerAquariumBgfxRenderer& operator=(const PlayerAquariumBgfxRenderer&) = delete;

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
    bool replaceTanks(
        const std::vector<construction::PlayerTankRuntime>& tanks,
        std::string* error = nullptr);
    bool stageTanks(
        const std::vector<construction::PlayerTankRuntime>& tanks,
        std::string* error = nullptr);
    bool publishStagedTanks();
    void discardStagedTanks();
    void advanceFrame();
    void submitOpaque(std::uint16_t view_id);
    void submitTransparent(std::uint16_t view_id, float camera_x, float camera_y, float camera_z);
    std::size_t resourceCount() const;
    std::size_t retiredResourceCount() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pr::gameplay::world3d::aquarium::rendering
