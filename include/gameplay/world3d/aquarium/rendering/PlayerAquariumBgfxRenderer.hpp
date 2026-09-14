#pragma once

#include "gameplay/world3d/aquarium/construction/AquariumPlayerRuntime.hpp"
#include "gameplay/world3d/aquarium/AquariumSubstratePreset.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::aquarium::rendering {

struct AquariumWaterSurfaceMaterial {
    // Borrowed from the owning overworld renderer.
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    std::vector<std::array<float, 2>> animation_uv_offsets;
    std::array<float, 2> u_per_tile{0.25f, 0.0f};
    std::array<float, 2> v_per_tile{0.0f, 0.25f};
    std::array<float, 2> uv_wrap_period{1.0f, 1.0f};
    float animation_timebase_hz = 30.0f;
    float opacity = 1.0f;
    bool animation_loop = true;
};

class PlayerAquariumBgfxRenderer {
public:
    PlayerAquariumBgfxRenderer();
    ~PlayerAquariumBgfxRenderer();
    PlayerAquariumBgfxRenderer(const PlayerAquariumBgfxRenderer&) = delete;
    PlayerAquariumBgfxRenderer& operator=(const PlayerAquariumBgfxRenderer&) = delete;

    void initialize(
        const bgfx::VertexLayout& layout,
        bgfx::ProgramHandle program,
        bgfx::ProgramHandle water_program,
        bgfx::ProgramHandle glass_program,
        bgfx::ProgramHandle fog_program,
        bgfx::TextureHandle white_texture,
        const std::array<bgfx::TextureHandle, kAquariumSubstratePresets.size()>&
            substrate_textures,
        AquariumWaterSurfaceMaterial water_surface_material,
        bgfx::UniformHandle texture_uniform,
        bgfx::UniformHandle tint_cutoff_uniform,
        bgfx::UniformHandle color_adjust_uniform,
        bgfx::UniformHandle texture_blur_uniform,
        bgfx::UniformHandle uv_offset_uniform,
        bgfx::UniformHandle light_dir_uniform,
        bgfx::UniformHandle light_params_uniform);
    void shutdown();
    void setDecorationFocus(std::string tank_id);
    void setInspectionHiddenTanks(std::vector<std::string> tank_ids);
    void setLighting(
        float brightness,
        const std::array<float, 3>& tint,
        float water_attenuation_intensity,
        float water_surface_speed,
        float sand_darkening);
    void setAnimationClock(bool enabled, double time_seconds);
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
    void submitFog(
        std::uint16_t view_id,
        bgfx::TextureHandle scene_depth,
        const float* inverse_view_projection,
        float camera_x,
        float camera_y,
        float camera_z,
        bool homogeneous_depth);
    void submitTransparent(std::uint16_t view_id, float camera_x, float camera_y, float camera_z);
    std::size_t resourceCount() const;
    std::size_t retiredResourceCount() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pr::gameplay::world3d::aquarium::rendering
