#include "gameplay/world3d/rendering/bgfx/BillboardBgfxDrawer.hpp"

#include "gameplay/world3d/rendering/PixelScale.hpp"
#include "gameplay/world3d/rendering/WorldBillboardCompositor.hpp"

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

std::uint32_t nonStackingShadowStencil() {
    return BGFX_STENCIL_TEST_EQUAL |
           BGFX_STENCIL_FUNC_REF(0) |
           BGFX_STENCIL_FUNC_RMASK(0xff) |
           BGFX_STENCIL_OP_FAIL_S_KEEP |
           BGFX_STENCIL_OP_FAIL_Z_KEEP |
           BGFX_STENCIL_OP_PASS_Z_INCRSAT;
}

std::uint64_t depthCutoutCharacterState() {
    return BGFX_STATE_WRITE_RGB |
           BGFX_STATE_WRITE_A |
           BGFX_STATE_WRITE_Z |
           BGFX_STATE_DEPTH_TEST_LEQUAL;
}

std::uint64_t transparentEffectState() {
    return BGFX_STATE_WRITE_RGB |
           BGFX_STATE_WRITE_A |
           BGFX_STATE_DEPTH_TEST_LEQUAL |
           BGFX_STATE_BLEND_ALPHA;
}

std::uint64_t depthTestedShadowState() {
    return BGFX_STATE_WRITE_RGB |
           BGFX_STATE_WRITE_A |
           BGFX_STATE_DEPTH_TEST_LEQUAL |
           BGFX_STATE_BLEND_ALPHA;
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

camera::Vec3 add(const camera::Vec3& a, const camera::Vec3& b) {
    return camera::Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

camera::Vec3 mul(const camera::Vec3& v, float s) {
    return camera::Vec3{v.x * s, v.y * s, v.z * s};
}

float worldUnitsPerScreenPixel(
    const camera::Gen4FollowCamera::Pose& pose,
    float depth,
    int viewport_h) {
    constexpr float kPi = 3.1415926535f;
    const float fov_y = pose.preset.fov_y_deg * (kPi / 180.0f);
    const float f = 1.0f / std::tan(std::max(0.001f, fov_y * 0.5f));
    return std::max(pose.preset.near_clip, depth) /
        (f * static_cast<float>(std::max(1, viewport_h)) * 0.5f);
}

camera::Vec3 screenToWorldOnCameraPlane(
    const camera::Gen4FollowCamera::Pose& pose,
    float screen_x,
    float screen_y,
    float depth,
    int viewport_w,
    int viewport_h) {
    constexpr float kPi = 3.1415926535f;
    const float fov_y = pose.preset.fov_y_deg * (kPi / 180.0f);
    const float f = 1.0f / std::tan(std::max(0.001f, fov_y * 0.5f));
    const float aspect =
        static_cast<float>(std::max(1, viewport_w)) / static_cast<float>(std::max(1, viewport_h));
    const float ndc_x = ((screen_x / static_cast<float>(std::max(1, viewport_w))) - 0.5f) * 2.0f;
    const float ndc_y = (0.5f - (screen_y / static_cast<float>(std::max(1, viewport_h)))) * 2.0f;
    const float cam_x = ndc_x * depth * aspect / f;
    const float cam_y = ndc_y * depth / f;

    camera::Vec3 world = pose.position;
    world = add(world, mul(pose.right, cam_x));
    world = add(world, mul(pose.up, cam_y));
    world = add(world, mul(pose.forward, depth));
    return world;
}

} // namespace

BillboardBgfxDrawer::BillboardBgfxDrawer(Dependencies dependencies) : deps_(std::move(dependencies)) {}

void BillboardBgfxDrawer::setWorldViewport(
    int base_width,
    int base_height,
    int render_width,
    int render_height,
    int internal_scale) {
    deps_.base_viewport_w = std::max(1, base_width);
    deps_.base_viewport_h = std::max(1, base_height);
    deps_.render_viewport_w = std::max(1, render_width);
    deps_.render_viewport_h = std::max(1, render_height);
    deps_.internal_scale = std::max(1, internal_scale);
}

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
        const float base_sprite_h = authoredPixelsWorldUnits(*deps_.scene, static_cast<float>(source_rect.h), 1.0f);
        const float sprite_scale = placement.world_h / std::max(0.001f, base_sprite_h);
        half_w = authoredPixelsWorldUnits(*deps_.scene, static_cast<float>(shadow_cfg.texture_width_px), sprite_scale) * 0.5f;
        half_h = authoredPixelsWorldUnits(*deps_.scene, static_cast<float>(shadow_cfg.texture_height_px), sprite_scale) * 0.5f;
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
    bgfx::setStencil(nonStackingShadowStencil());
    bgfx::setState(depthTestedShadowState());
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
    constexpr float kBillboardDepthLift = 0.02f;
    const float vertical_projection = std::max(0.1f, std::abs(pose.up.y));
    const float upright_screen_h = placement.world_h / vertical_projection;
    const camera::Vec3 right{flat_right.x * half_w, 0.0f, flat_right.z * half_w};
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
        placement.feet.y - right.y + up.y + kBillboardDepthLift,
        placement.feet.z - right.z + up.z};
    const camera::Vec3 p1{
        placement.feet.x + right.x + up.x,
        placement.feet.y + right.y + up.y + kBillboardDepthLift,
        placement.feet.z + right.z + up.z};
    const camera::Vec3 p2{placement.feet.x + right.x, placement.feet.y + right.y + kBillboardDepthLift, placement.feet.z + right.z};
    const camera::Vec3 p3{placement.feet.x - right.x, placement.feet.y - right.y + kBillboardDepthLift, placement.feet.z - right.z};
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

void BillboardBgfxDrawer::submitDepthCharacterQuad(
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
    std::uint64_t state) const {
    if (!texture.valid() || !placement.visible || !deps_.scene) {
        return;
    }

    const auto pose = camera.pose();
    const float horizontal_pixel_offset =
        authoredPixelsWorldUnits(*deps_.scene, static_cast<float>(screen_offset_x_px), 1.0f);
    const camera::Vec3 offset = mul(pose.right, horizontal_pixel_offset);
    camera::Vec3 bottom_center = add(placement.feet, offset);
    const int base_w = deps_.scene->world_viewport.enabled
        ? std::max(1, deps_.base_viewport_w)
        : std::max(1, deps_.render_viewport_w);
    const int base_h = deps_.scene->world_viewport.enabled
        ? std::max(1, deps_.base_viewport_h)
        : std::max(1, deps_.render_viewport_h);

    if (deps_.scene->world_viewport.enabled) {
        float sx = 0.0f;
        float sy = 0.0f;
        float depth = 0.0f;
        if (camera.worldToScreen(bottom_center, base_w, base_h, sx, sy, depth)) {
            bottom_center = screenToWorldOnCameraPlane(
                pose,
                std::round(sx),
                std::round(sy + static_cast<float>(deps_.scene->pixel_compositor.actor_screen_offset_y_px)),
                depth,
                base_w,
                base_h);
        }
    } else if (deps_.scene->pixel_compositor.actor_screen_offset_y_px != 0) {
        bottom_center = add(
            bottom_center,
            camera.screenOffsetToWorldOffset(
                deps_.scene->pixel_compositor.actor_screen_offset_y_px,
                placement.depth,
                base_h));
    }

    const float actor_depth_bias =
        authoredPixelsWorldUnits(*deps_.scene, deps_.scene->pixel_compositor.actor_depth_bias_px, 1.0f);
    bottom_center = add(bottom_center, mul(pose.forward, -actor_depth_bias));

    float bottom_x = 0.0f;
    float bottom_y = 0.0f;
    float bottom_depth = 0.0f;
    if (!camera.worldToScreen(bottom_center, base_w, base_h, bottom_x, bottom_y, bottom_depth)) {
        return;
    }
    const rendering::QuantizedBillboardRect target = rendering::projectWorldBillboardRect(
        *deps_.scene,
        camera,
        placement,
        source_rect,
        base_w,
        base_h,
        1,
        screen_offset_x_px);
    if (!target.visible) {
        return;
    }
    const float world_per_px = worldUnitsPerScreenPixel(pose, bottom_depth, base_h);
    const float half_w = static_cast<float>(std::max(1, target.base_w)) * world_per_px * 0.5f;
    const float world_h = static_cast<float>(std::max(1, target.base_h)) * world_per_px;
    const camera::Vec3 right = mul(pose.right, half_w);
    const camera::Vec3 up = mul(pose.up, world_h);
    (void)bottom_x;
    (void)bottom_y;

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
    const camera::Vec3 p0 = add(add(bottom_center, mul(right, -1.0f)), up);
    const camera::Vec3 p1 = add(add(bottom_center, right), up);
    const camera::Vec3 p2 = add(bottom_center, right);
    const camera::Vec3 p3 = add(bottom_center, mul(right, -1.0f));
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

void BillboardBgfxDrawer::submitScreenPlaneBillboardQuad(
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
    std::uint64_t state) const {
    if (!texture.valid() || !placement.visible || !deps_.scene) {
        return;
    }

    const int base_w = std::max(1, deps_.base_viewport_w);
    const int base_h = std::max(1, deps_.base_viewport_h);
    const int render_w = std::max(1, deps_.render_viewport_w);
    const int render_h = std::max(1, deps_.render_viewport_h);
    const int scale = std::max(1, deps_.internal_scale);

    const rendering::QuantizedBillboardRect rect = rendering::projectWorldBillboardRect(
        *deps_.scene,
        camera,
        placement,
        source_rect,
        base_w,
        base_h,
        scale,
        screen_offset_x_px);
    if (!rect.visible) {
        return;
    }

    const auto pose = camera.pose();
    (void)depth_priority_bias;
    const float quad_depth = std::max(pose.preset.near_clip + 0.001f, rect.depth - 0.05f);
    const float x0_i = static_cast<float>(rect.internal_x);
    const float y0_i = static_cast<float>(rect.internal_y);
    const float x1_i = static_cast<float>(rect.internal_x + rect.internal_w);
    const float y1_i = static_cast<float>(rect.internal_y + rect.internal_h);
    const camera::Vec3 p0 = screenToWorldOnCameraPlane(
        pose, x0_i, y0_i, quad_depth, render_w, render_h);
    const camera::Vec3 p1 = screenToWorldOnCameraPlane(
        pose, x1_i, y0_i, quad_depth, render_w, render_h);
    const camera::Vec3 p2 = screenToWorldOnCameraPlane(
        pose, x1_i, y1_i, quad_depth, render_w, render_h);
    const camera::Vec3 p3 = screenToWorldOnCameraPlane(
        pose, x0_i, y1_i, quad_depth, render_w, render_h);

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
    const TextureGpuResource& color_texture =
        draw.use_run_texture && textures.run_color.valid() ? textures.run_color : textures.color;
    const TextureGpuResource& white_texture =
        draw.use_run_texture && textures.run_white.valid() ? textures.run_white : textures.white;
    if (!color_texture.valid()) {
        return;
    }

    submitDepthCharacterQuad(
        camera,
        draw.placement,
        color_texture,
        draw.source_rect,
        draw.character->screen_offset_x_px,
        draw.tint_r,
        draw.tint_g,
        draw.tint_b,
        draw.alpha_multiplier,
        0.5f,
        depthCutoutCharacterState());

    if (draw.white_overlay_alpha > 0.0f && white_texture.valid()) {
        submitDepthCharacterQuad(
            camera,
            draw.placement,
            white_texture,
            draw.source_rect,
            draw.character->screen_offset_x_px,
            1.0f,
            1.0f,
            1.0f,
            draw.white_overlay_alpha,
            0.01f,
            transparentEffectState());
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
    if (deps_.scene && deps_.scene->world_viewport.enabled) {
        submitScreenPlaneBillboardQuad(
            camera,
            draw.placement,
            texture,
            draw.source_rect,
            0,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            0.5f,
            0.0f,
            transparentEffectState());
    } else {
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
            transparentEffectState());
    }
}

} // namespace pr::gameplay::world3d::rendering::bgfx_backend
