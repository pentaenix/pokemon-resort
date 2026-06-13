#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"
#include "gameplay/world3d/terrain/ActorTerrainBinding.hpp"

#include <SDL.h>
#include <memory>

namespace pr::gameplay::world3d::rendering {

class BillboardSpriteRenderer {
public:
    BillboardSpriteRenderer(
        SDL_Renderer* renderer,
        const SceneConfig& scene,
        const CharacterSpriteDefinition& def,
        const SpriteShadowConfig& shadow_config);

    bool valid() const { return texture_ != nullptr; }
    void render(
        SDL_Renderer* renderer,
        const camera::Gen4FollowCamera& camera,
        const terrain::ActorTerrainBinding& binding,
        const camera::Vec3& simulation_pos,
        const SDL_Rect& source_rect,
        int viewport_w,
        int viewport_h,
        float tint_r,
        float tint_g,
        float tint_b,
        float brightness,
        float sprite_scale_multiplier = 1.0f,
        float alpha_multiplier = 1.0f,
        float white_overlay_alpha = 0.0f,
        const camera::Vec3* shadow_world_override = nullptr,
        int extra_screen_offset_x_px = 0,
        int extra_screen_offset_y_px = 0);

private:
    SceneConfig scene_;
    std::shared_ptr<SDL_Texture> texture_;
    std::shared_ptr<SDL_Texture> white_texture_;
    std::shared_ptr<SDL_Texture> shadow_texture_;
    CharacterSpriteDefinition def_;
    SpriteShadowConfig shadow_config_;
};

} // namespace pr::gameplay::world3d::rendering
