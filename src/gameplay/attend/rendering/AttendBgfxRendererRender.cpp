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

    backend_.endFrame();
}

} // namespace pr::gameplay::attend::rendering
