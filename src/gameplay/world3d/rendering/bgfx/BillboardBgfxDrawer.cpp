#include "gameplay/world3d/rendering/bgfx/BillboardBgfxDrawer.hpp"

#include <algorithm>
#include <cmath>

namespace pr::gameplay::world3d::rendering::bgfx_backend {

namespace {

std::uint32_t packAbgr(float r, float g, float b, float a = 1.0f) {
    const auto c = [](float v) -> std::uint32_t {
        return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return (c(a) << 24U) | (c(b) << 16U) | (c(g) << 8U) | c(r);
}

void identity(float (&m)[16]) {
    std::fill(std::begin(m), std::end(m), 0.0f);
    m[0] = 1.0f;
    m[5] = 1.0f;
    m[10] = 1.0f;
    m[15] = 1.0f;
}

std::uint64_t samplerFlags() {
    return BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
}

camera::Vec3 horizontalCameraRight(const camera::Gen4FollowCamera& camera) {
    const auto pose = camera.pose();
    camera::Vec3 right{pose.right.x, 0.0f, pose.right.z};
    const float len = std::sqrt((right.x * right.x) + (right.z * right.z));
    if (len <= 0.0001f) {
        return camera::Vec3{1.0f, 0.0f, 0.0f};
    }
    return camera::Vec3{right.x / len, 0.0f, right.z / len};
}

} // namespace

BillboardBgfxDrawer::BillboardBgfxDrawer(Dependencies dependencies) : deps_(std::move(dependencies)) {}

void BillboardBgfxDrawer::submitGroundShadow(
    const camera::Gen4FollowCamera& camera,
    const BillboardPlacement& placement,
    const SDL_Rect& source_rect,
    const SpriteShadowConfig& shadow_cfg) const {
    if (!deps_.shadow_texture.valid() || !placement.visible || !deps_.scene) {
        return;
    }

    float half_w = placement.world_w * 0.5f;
    float half_h = placement.world_h * 0.5f;
    if (shadow_cfg.pixel_coherent) {
        half_w = placement.world_w * 0.5f *
            (static_cast<float>(shadow_cfg.texture_width_px) / std::max(1.0f, static_cast<float>(source_rect.w)));
        half_h = placement.world_h * 0.5f *
            (static_cast<float>(shadow_cfg.texture_height_px) / std::max(1.0f, static_cast<float>(source_rect.h)));
    } else {
        half_w = placement.world_w * shadow_cfg.radius_x_tiles * 2.0f * 0.5f;
        half_h = placement.world_h * shadow_cfg.radius_z_tiles * 2.0f * 0.5f;
    }

    const camera::Vec3 flat_right = horizontalCameraRight(camera);
    const camera::Vec3 flat_forward{-flat_right.z, 0.0f, flat_right.x};

    const camera::Vec3 center{placement.shadow_ground.x, placement.shadow_ground.y, placement.shadow_ground.z};
    const camera::Vec3 right{flat_right.x * half_w, 0.0f, flat_right.z * half_w};
    const camera::Vec3 forward{flat_forward.x * half_h, 0.0f, flat_forward.z * half_h};

    const std::uint32_t color = 0xffffffffu;
    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;
    if (!bgfx::allocTransientBuffers(&tvb, deps_.layout, 4, &tib, 6)) {
        return;
    }
    auto* verts = reinterpret_cast<Vertex*>(tvb.data);
    const camera::Vec3 p0{center.x - right.x - forward.x, center.y, center.z - right.z - forward.z};
    const camera::Vec3 p1{center.x + right.x - forward.x, center.y, center.z + right.z - forward.z};
    const camera::Vec3 p2{center.x + right.x + forward.x, center.y, center.z + right.z + forward.z};
    const camera::Vec3 p3{center.x - right.x + forward.x, center.y, center.z - right.z + forward.z};
    verts[0] = Vertex{p0.x, p0.y, p0.z, color, 0.0f, 0.0f};
    verts[1] = Vertex{p1.x, p1.y, p1.z, color, 1.0f, 0.0f};
    verts[2] = Vertex{p2.x, p2.y, p2.z, color, 1.0f, 1.0f};
    verts[3] = Vertex{p3.x, p3.y, p3.z, color, 0.0f, 1.0f};
    auto* idx = reinterpret_cast<std::uint16_t*>(tib.data);
    idx[0] = 0;
    idx[1] = 1;
    idx[2] = 2;
    idx[3] = 0;
    idx[4] = 2;
    idx[5] = 3;

    const float br = std::max(0.0f, deps_.scene->lighting_brightness);
    float tint[4] = {
        deps_.scene->lighting_tint_r * br,
        deps_.scene->lighting_tint_g * br,
        deps_.scene->lighting_tint_b * br,
        0.01f};
    float model[16];
    identity(model);
    bgfx::setTransform(model);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setIndexBuffer(&tib);
    bgfx::setTexture(0, deps_.tex_uniform, deps_.shadow_texture.handle, samplerFlags());
    bgfx::setUniform(deps_.tint_cutoff_uniform, tint);
    bgfx::setState(
        BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS |
        BGFX_STATE_BLEND_ALPHA);
    bgfx::submit(deps_.view_id, deps_.billboard_program);
    (void)camera;
}

void BillboardBgfxDrawer::submitBillboardQuad(
    const camera::Gen4FollowCamera& camera,
    const BillboardPlacement& placement,
    const TextureGpuResource& texture,
    const SDL_Rect& source_rect,
    float tint_r,
    float tint_g,
    float tint_b,
    float vertex_alpha,
    float alpha_cutoff,
    std::uint64_t state) const {
    if (!texture.valid() || !placement.visible || !deps_.scene) {
        return;
    }
    const float half_w = placement.world_w * 0.5f;
    const camera::Vec3 flat_right = horizontalCameraRight(camera);
    const auto pose = camera.pose();
    const float vertical_projection = std::max(0.1f, std::abs(pose.up.y));
    const float upright_screen_h = placement.world_h / vertical_projection;
    const camera::Vec3 right{
        -flat_right.x * half_w,
        0.0f,
        -flat_right.z * half_w};
    const camera::Vec3 up{0.0f, upright_screen_h, 0.0f};

    const float u0 = static_cast<float>(source_rect.x) / static_cast<float>(texture.width);
    const float v0 = static_cast<float>(source_rect.y) / static_cast<float>(texture.height);
    const float u1 =
        static_cast<float>(source_rect.x + source_rect.w) / static_cast<float>(texture.width);
    const float v1 =
        static_cast<float>(source_rect.y + source_rect.h) / static_cast<float>(texture.height);
    const float br = std::max(0.0f, deps_.scene->lighting_brightness);
    const std::uint32_t color = packAbgr(tint_r * br, tint_g * br, tint_b * br, vertex_alpha);

    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;
    if (!bgfx::allocTransientBuffers(&tvb, deps_.layout, 4, &tib, 6)) {
        return;
    }
    auto* verts = reinterpret_cast<Vertex*>(tvb.data);
    const camera::Vec3 p0{
        placement.feet.x - right.x + up.x,
        placement.feet.y - right.y + up.y,
        placement.feet.z - right.z + up.z};
    const camera::Vec3 p1{
        placement.feet.x + right.x + up.x,
        placement.feet.y + right.y + up.y,
        placement.feet.z + right.z + up.z};
    const camera::Vec3 p2{placement.feet.x + right.x, placement.feet.y + right.y, placement.feet.z + right.z};
    const camera::Vec3 p3{placement.feet.x - right.x, placement.feet.y - right.y, placement.feet.z - right.z};
    verts[0] = Vertex{p0.x, p0.y, p0.z, color, u0, v0};
    verts[1] = Vertex{p1.x, p1.y, p1.z, color, u1, v0};
    verts[2] = Vertex{p2.x, p2.y, p2.z, color, u1, v1};
    verts[3] = Vertex{p3.x, p3.y, p3.z, color, u0, v1};
    auto* idx = reinterpret_cast<std::uint16_t*>(tib.data);
    idx[0] = 0;
    idx[1] = 1;
    idx[2] = 2;
    idx[3] = 0;
    idx[4] = 2;
    idx[5] = 3;

    float tint_uniform[4] = {1.0f, 1.0f, 1.0f, alpha_cutoff};
    float model[16];
    identity(model);
    bgfx::setTransform(model);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setIndexBuffer(&tib);
    bgfx::setTexture(0, deps_.tex_uniform, texture.handle, samplerFlags());
    bgfx::setUniform(deps_.tint_cutoff_uniform, tint_uniform);
    bgfx::setState(state);
    bgfx::submit(deps_.view_id, deps_.billboard_program);
}

void BillboardBgfxDrawer::submitCharacterDraw(
    const camera::Gen4FollowCamera& camera,
    const CharacterBillboardDraw& draw) const {
    if (!draw.character || !draw.placement.visible || !deps_.textures_for_character) {
        return;
    }
    const CharacterGpuTextures textures = deps_.textures_for_character(*draw.character);
    if (!textures.color.valid()) {
        return;
    }

    const std::uint64_t opaque_state =
        BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
    const std::uint64_t blend_state = opaque_state | BGFX_STATE_BLEND_ALPHA;
    submitBillboardQuad(
        camera,
        draw.placement,
        textures.color,
        draw.source_rect,
        draw.tint_r,
        draw.tint_g,
        draw.tint_b,
        draw.alpha_multiplier,
        0.5f,
        draw.alpha_multiplier < 0.999f ? blend_state : opaque_state);

    if (draw.white_overlay_alpha > 0.0f && textures.white.valid()) {
        submitBillboardQuad(
            camera,
            draw.placement,
            textures.white,
            draw.source_rect,
            1.0f,
            1.0f,
            1.0f,
            draw.white_overlay_alpha,
            0.01f,
            blend_state);
    }
}

void BillboardBgfxDrawer::submitCharacterShadow(
    const camera::Gen4FollowCamera& camera,
    const CharacterBillboardDraw& draw) const {
    if (!draw.draw_shadow || !draw.character || !draw.placement.visible || !deps_.scene) {
        return;
    }
    submitGroundShadow(camera, draw.placement, draw.source_rect, deps_.scene->sprite_shadow);
}

void BillboardBgfxDrawer::submitTextureDraw(
    const camera::Gen4FollowCamera& camera,
    const TextureBillboardDraw& draw) const {
    if (!draw.placement.visible || !deps_.texture_for_key) {
        return;
    }
    const TextureGpuResource texture = deps_.texture_for_key(
        draw.texture_cache_key,
        draw.png_bytes,
        draw.fallback_path,
        "effect-billboard");
    if (!texture.valid()) {
        return;
    }
    const std::uint64_t blend_state =
        BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
        BGFX_STATE_BLEND_ALPHA;
    submitBillboardQuad(
        camera,
        draw.placement,
        texture,
        draw.source_rect,
        1.0f,
        1.0f,
        1.0f,
        1.0f,
        0.5f,
        blend_state);
}

} // namespace pr::gameplay::world3d::rendering::bgfx_backend
