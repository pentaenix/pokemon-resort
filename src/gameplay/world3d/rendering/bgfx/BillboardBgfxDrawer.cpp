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

bool resolveDepthBillboardBottomCenter(
    const SceneConfig& scene,
    const camera::Gen4FollowCamera& camera,
    const BillboardPlacement& placement,
    int base_w,
    int base_h,
    int screen_offset_x_px,
    camera::Vec3& out_world,
    float& out_screen_x,
    float& out_screen_y,
    float& out_depth) {
    const auto pose = camera.pose();
    const float horizontal_pixel_offset =
        authoredPixelsWorldUnits(scene, static_cast<float>(screen_offset_x_px), 1.0f);
    const camera::Vec3 offset = mul(pose.right, horizontal_pixel_offset);
    camera::Vec3 bottom_center = add(placement.feet, offset);

    if (scene.world_viewport.enabled) {
        float sx = 0.0f;
        float sy = 0.0f;
        float depth = 0.0f;
        if (camera.worldToScreen(bottom_center, base_w, base_h, sx, sy, depth)) {
            bottom_center = screenToWorldOnCameraPlane(
                pose,
                std::round(sx),
                std::round(sy + static_cast<float>(scene.pixel_compositor.actor_screen_offset_y_px)),
                depth,
                base_w,
                base_h);
        }
    } else if (scene.pixel_compositor.actor_screen_offset_y_px != 0) {
        bottom_center = add(
            bottom_center,
            camera.screenOffsetToWorldOffset(
                scene.pixel_compositor.actor_screen_offset_y_px,
                placement.depth,
                base_h));
    }

    const float actor_depth_bias =
        authoredPixelsWorldUnits(scene, scene.pixel_compositor.actor_depth_bias_px, 1.0f);
    bottom_center = add(bottom_center, mul(pose.forward, -actor_depth_bias));

    if (!camera.worldToScreen(bottom_center, base_w, base_h, out_screen_x, out_screen_y, out_depth)) {
        return false;
    }
    out_world = bottom_center;
    return true;
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
    const float adjust[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    const float texture_blur[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float light_dir[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    const float light_params[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    float model[16];
    identity(model);
    bgfx::setTransform(model);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setIndexBuffer(&tib);
    bgfx::setTexture(0, deps_.tex_uniform, texture.handle, samplerFlags());
    bgfx::setUniform(deps_.tint_cutoff_uniform, tint_uniform);
    bgfx::setUniform(deps_.color_adjust_uniform, adjust);
    bgfx::setUniform(deps_.texture_blur_uniform, texture_blur);
    bgfx::setUniform(deps_.light_dir_uniform, light_dir);
    bgfx::setUniform(deps_.light_params_uniform, light_params);
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
    float depth_priority_bias_px,
    std::uint64_t state) const {
    if (!texture.valid() || !placement.visible || !deps_.scene) {
        return;
    }

    const auto pose = camera.pose();
    const int base_w = deps_.scene->world_viewport.enabled
        ? std::max(1, deps_.base_viewport_w)
        : std::max(1, deps_.render_viewport_w);
    const int base_h = deps_.scene->world_viewport.enabled
        ? std::max(1, deps_.base_viewport_h)
        : std::max(1, deps_.render_viewport_h);

    camera::Vec3 bottom_center{};
    float bottom_x = 0.0f;
    float bottom_y = 0.0f;
    float bottom_depth = 0.0f;
    if (!resolveDepthBillboardBottomCenter(
            *deps_.scene,
            camera,
            placement,
            base_w,
            base_h,
            screen_offset_x_px,
            bottom_center,
            bottom_x,
            bottom_y,
            bottom_depth)) {
        return;
    }
    const float depth_bias_world =
        authoredPixelsWorldUnits(*deps_.scene, std::max(0.0f, depth_priority_bias_px), 1.0f);
    bottom_center = add(bottom_center, mul(pose.forward, -depth_bias_world));
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
    const float adjust[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    const float texture_blur[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float light_dir[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    const float light_params[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    float model[16];
    identity(model);
    bgfx::setTransform(model);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setIndexBuffer(&tib);
    bgfx::setTexture(0, deps_.tex_uniform, texture.handle, samplerFlags());
    bgfx::setUniform(deps_.tint_cutoff_uniform, tint_uniform);
    bgfx::setUniform(deps_.color_adjust_uniform, adjust);
    bgfx::setUniform(deps_.texture_blur_uniform, texture_blur);
    bgfx::setUniform(deps_.light_dir_uniform, light_dir);
    bgfx::setUniform(deps_.light_params_uniform, light_params);
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
    const auto activity_color = textures.activity_color.find(draw.activity_id);
    const auto activity_white = textures.activity_white.find(draw.activity_id);
    const TextureGpuResource& color_texture = !draw.activity_id.empty() &&
            activity_color != textures.activity_color.end() &&
            activity_color->second.valid()
        ? activity_color->second
        : (draw.use_run_texture && textures.run_color.valid() ? textures.run_color : textures.color);
    const TextureGpuResource& white_texture = !draw.activity_id.empty() &&
            activity_white != textures.activity_white.end() &&
            activity_white->second.valid()
        ? activity_white->second
        : (draw.use_run_texture && textures.run_white.valid() ? textures.run_white : textures.white);
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
        0.0f,
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
            0.0f,
            transparentEffectState());
    }
}

void BillboardBgfxDrawer::submitTextureDraw(
    const camera::Gen4FollowCamera& camera,
    const TextureBillboardDraw& draw) const {
    if (!draw.placement.visible || !deps_.textures_for_character) {
        return;
    }
    CharacterSpriteDefinition effect_sprite{};
    effect_sprite.texture_path = draw.texture_cache_key;
    effect_sprite.texture_png_bytes = draw.png_bytes;
    const CharacterGpuTextures textures = deps_.textures_for_character(effect_sprite);
    const TextureGpuResource& texture = textures.color;
    if (!texture.valid()) {
        return;
    }
    if (deps_.scene && deps_.scene->world_viewport.enabled) {
        submitDepthCharacterQuad(
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
            0.25f,
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
