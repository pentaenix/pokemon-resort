#pragma once

#include "gameplay/world3d/aquarium/AquariumSimulation.hpp"

#include <bgfx/bgfx.h>

#include <memory>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::aquarium::rendering {

class AquariumPokemonBgfxRenderer {
public:
    AquariumPokemonBgfxRenderer();
    ~AquariumPokemonBgfxRenderer();
    AquariumPokemonBgfxRenderer(const AquariumPokemonBgfxRenderer&) = delete;
    AquariumPokemonBgfxRenderer& operator=(const AquariumPokemonBgfxRenderer&) = delete;

    void initialize(
        const bgfx::VertexLayout& layout,
        bgfx::ProgramHandle program,
        bgfx::UniformHandle texture_uniform,
        bgfx::UniformHandle tint_cutoff_uniform,
        bgfx::UniformHandle color_adjust_uniform,
        bgfx::UniformHandle texture_blur_uniform,
        bgfx::UniformHandle uv_offset_uniform,
        bgfx::UniformHandle light_dir_uniform,
        bgfx::UniformHandle light_params_uniform);
    void shutdown();
    void setActors(const std::vector<AquariumPokemonActor>& actors);
    void submit(std::uint16_t view_id, bool blended_pass);
    const std::string& lastError() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pr::gameplay::world3d::aquarium::rendering
