#include "AttendBgfxRendererInternal.hpp"

namespace pr::gameplay::attend::rendering {

void AttendBgfxRenderer::Impl::render(double scene_time_seconds, int width, int height) {
    if (!valid()) return;
    if (face_view_ && !faceViewAvailable()) {
        face_view_ = false;
    }
    backend_.reset(width, height);
    const int base_w = std::clamp(world_viewport_.base_width, 160, 1920);
    const int base_h = std::clamp(world_viewport_.base_height, 120, 1080);
    const int upscale = std::clamp(world_viewport_.internal_scale, 1, 4);
    const int scene_w = world_viewport_.enabled ? base_w * upscale : std::max(1, width);
    const int scene_h = world_viewport_.enabled ? base_h * upscale : std::max(1, height);
    bool pixel_scene_enabled = world_viewport_.enabled;
    if (pixel_scene_enabled && !ensurePixelSceneTarget(scene_w, scene_h)) {
        std::cerr << "[AttendBgfx] Pixel scene target unavailable, rendering direct: "
                  << last_error_ << '\n';
        pixel_scene_enabled = false;
    }
    if (!pixel_scene_enabled) {
        pixel_scene_target_.destroy();
    }
    const int render_w = pixel_scene_enabled ? scene_w : std::max(1, width);
    const int render_h = pixel_scene_enabled ? scene_h : std::max(1, height);
    presentation_rect_ = SDL_Rect{0, 0, std::max(1, width), std::max(1, height)};
    if (pixel_scene_enabled) {
        const int safe_w = std::max(1, width);
        const int safe_h = std::max(1, height);
        const int integer_scale = std::max(1, std::min(safe_w / render_w, safe_h / render_h));
        const int dest_w = render_w * integer_scale;
        const int dest_h = render_h * integer_scale;
        presentation_rect_ = SDL_Rect{(safe_w - dest_w) / 2, (safe_h - dest_h) / 2, dest_w, dest_h};
    }
    bgfx::FrameBufferHandle scene_frame_buffer = BGFX_INVALID_HANDLE;
    if (pixel_scene_enabled) {
        scene_frame_buffer = pixel_scene_target_.frame_buffer;
    }

    backend_.beginFrame(config_.clear_color.r, config_.clear_color.g, config_.clear_color.b, 1.0f);
    const auto c = [](float v) -> std::uint32_t {
        return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    const std::uint32_t clear_rgba =
        (c(config_.clear_color.r) << 24U) |
        (c(config_.clear_color.g) << 16U) |
        (c(config_.clear_color.b) << 8U) |
        c(1.0f);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL, clear_rgba, 1.0f, 0);
    bgfx::setViewFrameBuffer(0, scene_frame_buffer);

    updateViewportLook(scene_time_seconds);
    updateFaceViewTransition(scene_time_seconds);
    const float look_x = config_.viewport_look.enabled ? viewport_look_x_ * config_.viewport_look.max_x : 0.0f;
    const float look_y = config_.viewport_look.enabled ? viewport_look_y_ * config_.viewport_look.max_y : 0.0f;
    bx::Vec3 eye{0.0f, 0.0f, 0.0f};
    bx::Vec3 at{0.0f, 0.0f, 0.0f};
    cameraForFrame(look_x, look_y, eye, at);
    if (!freecam_enabled_) {
        const float dx = at.x - eye.x;
        const float dy = at.y - eye.y;
        const float dz = at.z - eye.z;
        const float yaw = std::atan2(dx, dz) * (180.0f / kPi);
        const float pitch = std::atan2(dy, std::sqrt(dx * dx + dz * dz)) * (180.0f / kPi);
        last_camera_pose_ = AttendFreeCameraPose{eye.x, eye.y, eye.z, yaw, pitch};
        last_camera_pose_valid_ = true;
    } else {
        last_camera_pose_ = freecam_pose_;
        last_camera_pose_valid_ = true;
    }
    float view[16];
    float proj[16];
    bx::mtxLookAt(view, eye, at);
    bx::mtxProj(
        proj,
        config_.camera.fov_y_degrees,
        static_cast<float>(std::max(1, render_w)) / static_cast<float>(std::max(1, render_h)),
        config_.camera.near_clip,
        config_.camera.far_clip,
        bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewTransform(0, view, proj);
    bgfx::setViewRect(
        0,
        0,
        0,
        static_cast<std::uint16_t>(std::max(1, render_w)),
        static_cast<std::uint16_t>(std::max(1, render_h)));

    float ident[16];
    identity(ident);
    updateFloorAnimation(scene_time_seconds);
    submitMesh(wall_mesh_, ident, false, true);
    submitMesh(floor_mesh_, ident, false, false, true);
    for (const MeshResource& mesh : floor_extension_meshes_) {
        submitMesh(mesh, ident, false, false, true);
    }
    submitPokemonShadow();
    updatePetControls(scene_time_seconds);
    updateRandomBlink(scene_time_seconds);
    updateFaceControls(scene_time_seconds);
    updatePokemonAnimation(scene_time_seconds);

    float pokemon_matrix[16];
    placementMatrix(
        config_.pokemon.x,
        config_.pokemon.y,
        config_.pokemon.z,
        config_.pokemon.yaw_degrees,
        config_.pokemon.pitch_degrees,
        config_.pokemon.scale,
        pokemon_matrix);
    updatePokemonPointerRect(pokemon_matrix, view, proj, render_w, render_h);
    submitMesh(pokemon_mesh_, pokemon_matrix);
    if (pixel_scene_enabled && config_.ui.pixelated_overlay) {
        const bool normal_sd = config_.ui.normal_render_mode == "sd";
        const bool debug_sd = config_.ui.debug_render_mode == "sd";
        const SDL_Rect saved_presentation = presentation_rect_;
        presentation_rect_ = SDL_Rect{0, 0, render_w, render_h};
        if (normal_sd) {
            submitProfilePlate(render_w, render_h, scene_frame_buffer, 1, false, true);
            submitCornerButtons(render_w, render_h, scene_frame_buffer, 2);
        }
        if (debug_sd) {
            submitOverlayButtons(render_w, render_h, scene_frame_buffer, 3);
        }
        presentation_rect_ = saved_presentation;
    }
    if (pixel_scene_enabled) {
        submitPixelSceneToBackbuffer(width, height, render_w, render_h, config_.ui.pixelated_overlay ? 4 : 1);
        if (config_.ui.pixelated_overlay) {
            if (config_.ui.normal_render_mode != "sd") {
                submitProfilePlate(width, height, BGFX_INVALID_HANDLE, 5, false, true);
                submitCornerButtons(width, height, BGFX_INVALID_HANDLE, 6);
            }
            if (config_.ui.debug_render_mode != "sd") {
                submitOverlayButtons(width, height, BGFX_INVALID_HANDLE, 7);
            }
        }
    }
    if (!pixel_scene_enabled || !config_.ui.pixelated_overlay) {
        submitProfilePlate(width, height, BGFX_INVALID_HANDLE, 2);
        submitOverlayButtons(width, height, BGFX_INVALID_HANDLE, 3);
        submitCornerButtons(width, height, BGFX_INVALID_HANDLE, 4);
    }

    if (iris_visible_ && white_texture_.valid()) {
        const int segments = iris_segments_;
        bgfx::TransientVertexBuffer tvb; bgfx::TransientIndexBuffer tib;
        if (bgfx::allocTransientBuffers(&tvb, layout_, segments * 4, &tib, segments * 6)) {
            const float cx = iris_x_ * width / std::max(1, overlay_logical_w_);
            const float cy = iris_y_ * height / std::max(1, overlay_logical_h_);
            const float diagonal = std::hypot(static_cast<float>(width), static_cast<float>(height));
            const float inner = diagonal * iris_radius_scale_ * (1.0f - iris_amount_);
            const float outer = diagonal * 2.5f;
            auto* v = reinterpret_cast<Vertex*>(tvb.data); auto* idx = reinterpret_cast<uint16_t*>(tib.data);
            for (int i=0;i<segments;++i) {
                const float a0=2*kPi*i/segments, a1=2*kPi*(i+1)/segments; const int n=i*4, k=i*6;
                v[n]={cx+std::cos(a0)*inner,cy+std::sin(a0)*inner,0,0,1,0,0xff000000u,0,0};
                v[n+1]={cx+std::cos(a1)*inner,cy+std::sin(a1)*inner,0,0,1,0,0xff000000u,0,0};
                v[n+2]={cx+std::cos(a1)*outer,cy+std::sin(a1)*outer,0,0,1,0,0xff000000u,0,0};
                v[n+3]={cx+std::cos(a0)*outer,cy+std::sin(a0)*outer,0,0,1,0,0xff000000u,0,0};
                idx[k]=n;idx[k+1]=n+1;idx[k+2]=n+2;idx[k+3]=n;idx[k+4]=n+2;idx[k+5]=n+3;
            }
            float iv[16], ip[16], im[16]; identity(iv); identity(im);
            bx::mtxOrtho(ip,0.0f,(float)width,(float)height,0.0f,0.0f,100.0f,0.0f,bgfx::getCaps()->homogeneousDepth);
            constexpr bgfx::ViewId iris_view=8; bgfx::setViewTransform(iris_view,iv,ip); bgfx::setViewRect(iris_view,0,0,width,height);
            bgfx::setTransform(im); bgfx::setVertexBuffer(0,&tvb); bgfx::setIndexBuffer(&tib);
            bgfx::setTexture(0,tex_uniform_,white_texture_.handle,samplerFlags());
            const float z[4]={0,0,0,0}, one[4]={1,1,1,0}, light[4]={0,1,0,0};
            bgfx::setUniform(tint_cutoff_uniform_,one);bgfx::setUniform(color_adjust_uniform_,one);bgfx::setUniform(texture_blur_uniform_,z);bgfx::setUniform(light_dir_uniform_,light);bgfx::setUniform(light_params_uniform_,one);
            bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);bgfx::submit(iris_view,program_);
        }
    }
    backend_.endFrame();
}

} // namespace pr::gameplay::attend::rendering
