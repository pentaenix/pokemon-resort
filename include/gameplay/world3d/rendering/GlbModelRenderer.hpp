#pragma once

#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"
#include "gameplay/world3d/data/GlbModelLoader.hpp"

#include <SDL.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace pr::gameplay::world3d::rendering {

// Renders a decoded GLB model instance into the Gen-4 software-projected scene using
// SDL_RenderGeometry, matching the look of the terrain/prop renderer. The placement
// transform (world position, yaw about +Y, uniform scale) is applied per frame; embedded
// textures are uploaded lazily against the active SDL_Renderer.
class GlbModelRenderer {
public:
    struct MaterialAlphaMask {
        int width = 0;
        int height = 0;
        std::vector<Uint8> alpha;
    };

    GlbModelRenderer(data::GlbMesh mesh, float x, float y, float z, float yaw_deg, float scale);

    bool valid() const { return mesh_.valid && !mesh_.triangles.empty(); }

    // Camera-space depth of the placement ground anchor, used to depth-sort this model
    // against character billboards and other props (far-to-near painter order). Returns
    // nullopt when the anchor is behind the near plane.
    std::optional<float> anchorDepth(
        const camera::Gen4FollowCamera& camera,
        int viewport_w,
        int viewport_h) const;

    void render(
        SDL_Renderer* renderer,
        const camera::Gen4FollowCamera& camera,
        int viewport_w,
        int viewport_h,
        float tint_r,
        float tint_g,
        float tint_b,
        float brightness) const;

private:
    data::GlbMesh mesh_;
    float x_ = 0.0f;
    float y_ = 0.0f;
    float z_ = 0.0f;
    float cos_yaw_ = 1.0f;
    float sin_yaw_ = 0.0f;
    float scale_ = 1.0f;

    mutable std::vector<std::shared_ptr<SDL_Texture>> material_textures_;
    mutable std::vector<MaterialAlphaMask> material_alpha_masks_;
    mutable std::vector<std::uint8_t> triangle_cutout_;
    mutable bool textures_ready_ = false;
    mutable bool triangle_cutout_ready_ = false;

    void ensureTextures(SDL_Renderer* renderer) const;
    void classifyTriangleCutouts() const;
};

} // namespace pr::gameplay::world3d::rendering
