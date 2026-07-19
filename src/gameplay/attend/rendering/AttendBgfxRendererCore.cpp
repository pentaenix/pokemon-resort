#include "AttendBgfxRendererInternal.hpp"

namespace pr::gameplay::attend::rendering {


AttendBgfxRenderer::Impl::Impl(std::string project_root, AttendSceneConfig config)
    : project_root_(std::move(project_root)),
      config_(std::move(config)),
      world_viewport_(loadSharedWorldViewportConfig(project_root_)) {}

AttendBgfxRenderer::Impl::~Impl() {
    shutdown();
}

bool AttendBgfxRenderer::Impl::valid() const {
    return initialized_ && backend_.valid();
}

std::string AttendBgfxRenderer::Impl::lastError() const {
    return last_error_;
}

void AttendBgfxRenderer::Impl::setPetting(bool petting) {
    petting_ = petting;
}

void AttendBgfxRenderer::Impl::setPetContact(float vertical_bias) {
    pet_contact_target_ = std::clamp(vertical_bias, -1.0f, 1.0f);
}

void AttendBgfxRenderer::Impl::setFaceLook(float x, float y) {
    face_target_x_ = std::clamp(x, -1.0f, 1.0f);
    face_target_y_ = std::clamp(y, -1.0f, 1.0f);
}

void AttendBgfxRenderer::Impl::setViewportLook(float x, float y) {
    viewport_look_target_x_ = std::clamp(x, -1.0f, 1.0f);
    viewport_look_target_y_ = std::clamp(y, -1.0f, 1.0f);
}

void AttendBgfxRenderer::Impl::setFreeCamera(bool enabled, AttendFreeCameraPose pose) {
    freecam_enabled_ = enabled;
    freecam_pose_ = pose;
}

bool AttendBgfxRenderer::Impl::currentCameraPose(AttendFreeCameraPose& out) const {
    if (!last_camera_pose_valid_) return false;
    out = last_camera_pose_;
    return true;
}

void AttendBgfxRenderer::Impl::setFaceView(bool face_view) {
    face_view_ = face_view;
}

bool AttendBgfxRenderer::Impl::faceViewTransitionActive() const {
    const float target = face_view_ ? 1.0f : 0.0f;
    return std::abs(face_view_blend_ - target) > 0.01f;
}

bool AttendBgfxRenderer::Impl::canTriggerReaction(
    const std::string& reaction_id,
    double interaction_seconds) const {
    const AttendInteractionAdapterConfig::ReactionCombo* combo = reactionCombo(reaction_id);
    return combo &&
           interaction_seconds >= static_cast<double>(combo->min_pet_seconds) &&
           resolveSemanticAnimation(combo->animation_semantic) != nullptr;
}

bool AttendBgfxRenderer::Impl::canTriggerSemanticAnimation(const std::string& animation_semantic) const {
    return resolveSemanticAnimation(animation_semantic) != nullptr;
}

void AttendBgfxRenderer::Impl::triggerReaction(std::string reaction_id, double interaction_seconds) {
    pending_reaction_id_ = std::move(reaction_id);
    pending_reaction_interaction_seconds_ = interaction_seconds;
}

void AttendBgfxRenderer::Impl::triggerSemanticAnimation(
    std::string animation_semantic,
    double duration_seconds,
    double fade_in_seconds,
    double fade_out_seconds,
    std::string eye_expression,
    std::string mouth_expression) {
    pending_semantic_animation_ = std::move(animation_semantic);
    pending_semantic_duration_seconds_ = duration_seconds;
    pending_semantic_fade_in_seconds_ = fade_in_seconds;
    pending_semantic_fade_out_seconds_ = fade_out_seconds;
    pending_semantic_eye_expression_ = std::move(eye_expression);
    pending_semantic_mouth_expression_ = std::move(mouth_expression);
    pending_semantic_reverse_ = false;
}

double AttendBgfxRenderer::Impl::wakeFromIdleAnimation() {
    if (!isIdleSleeping()) {
        return 0.0;
    }
    const AttendPokemonAnimation* wake_animation = semantic_sleeping_ ? resolveSemanticAnimation("sleep_start") : nullptr;
    pending_wake_from_sleep_ = wake_animation != nullptr;
    const double wake_seconds = wake_animation ? static_cast<double>(std::max(0.05f, wake_animation->duration_seconds)) : 0.0;
    pending_semantic_animation_.clear();
    pending_semantic_eye_expression_.clear();
    pending_semantic_mouth_expression_.clear();
    pending_semantic_duration_seconds_ = 0.0;
    pending_semantic_reverse_ = false;
    pending_sleep_loop_after_start_ = false;
    pending_sleep_loop_eye_expression_.clear();
    pending_sleep_loop_mouth_expression_.clear();
    if (reaction_from_semantic_) {
        reaction_active_ = false;
        reaction_animation_ = nullptr;
        reaction_from_semantic_ = false;
        reaction_reverse_ = false;
        reaction_eye_linger_until_seconds_ = -1.0;
        current_eye_expression_frame_ = normal_eye_expression_frame_;
        current_mouth_expression_frame_ = normal_mouth_expression_frame_;
    }
    semantic_sleeping_ = false;
    return wake_seconds;
}

bool AttendBgfxRenderer::Impl::isIdleSleeping() const {
    return semantic_sleeping_ || pending_sleep_loop_after_start_;
}

void AttendBgfxRenderer::Impl::blendOutIdleAnimation(double scene_time_seconds, double fade_out_seconds) {
    if (!reaction_active_ || !reaction_from_semantic_ || semantic_sleeping_) return;
    const double fade = std::clamp(fade_out_seconds, 0.05, 1.0);
    const double elapsed = std::clamp(scene_time_seconds - reaction_start_seconds_, 0.0, reaction_duration_seconds_);
    reaction_duration_seconds_ = std::max(0.05, elapsed + fade);
    reaction_fade_out_seconds_ = std::min(fade, reaction_duration_seconds_ * 0.5);
    reaction_eye_linger_until_seconds_ = scene_time_seconds + fade;
}

bool AttendBgfxRenderer::Impl::faceViewAvailable() const {
    return camera_bounds_valid_ && camera_world_height_ >= config_.camera.face_view_min_model_height;
}

SDL_Rect AttendBgfxRenderer::Impl::pokemonPointerRect() const {
    return last_pokemon_pointer_rect_;
}

void AttendBgfxRenderer::Impl::setWeatherMode(int index) {
    const int count = static_cast<int>(config_.floor.weather_modes.size());
    floor_weather_index_ = count > 0 ? ((index % count) + count) % count : 0;
}

void AttendBgfxRenderer::Impl::setTextureVariant(int index) {
    const int count = static_cast<int>(pokemon_model_.texture_variants.size());
    texture_variant_index_ = count > 0 ? ((index % count) + count) % count : 0;
}

void AttendBgfxRenderer::Impl::setFormVariant(int index) {
    const int count = static_cast<int>(pokemon_model_.form_variants.size());
    form_variant_index_ = count > 0 ? ((index % count) + count) % count : 0;
}

void AttendBgfxRenderer::Impl::setOverlayButtons(
    std::vector<AttendBgfxOverlayButton> buttons,
    int logical_w,
    int logical_h) {
    overlay_buttons_ = std::move(buttons);
    overlay_logical_w_ = std::max(1, logical_w);
    overlay_logical_h_ = std::max(1, logical_h);
    overlay_button_textures_.resize(overlay_buttons_.size());
}

void AttendBgfxRenderer::Impl::setCornerButtons(
    std::vector<AttendBgfxCornerButton> buttons,
    int logical_w,
    int logical_h) {
    corner_buttons_ = std::move(buttons);
    corner_logical_w_ = std::max(1, logical_w);
    corner_logical_h_ = std::max(1, logical_h);
    corner_button_textures_.resize(corner_buttons_.size());
}

void AttendBgfxRenderer::Impl::setProfilePlate(
    AttendBgfxProfilePlate plate,
    int logical_w,
    int logical_h) {
    profile_plate_ = std::move(plate);
    profile_plate_logical_w_ = std::max(1, logical_w);
    profile_plate_logical_h_ = std::max(1, logical_h);
}

void AttendBgfxRenderer::Impl::setBlackIrisTransition(float x, float y, float amount,
    bool visible, int segments, float radius_scale) {
    iris_x_ = x; iris_y_ = y; iris_amount_ = std::clamp(amount, 0.0f, 1.0f);
    iris_visible_ = visible; iris_segments_ = std::clamp(segments, 16, 192);
    iris_radius_scale_ = std::max(1.0f, radius_scale);
}

int AttendBgfxRenderer::Impl::weatherModeCount() const {
    return static_cast<int>(config_.floor.weather_modes.size());
}

int AttendBgfxRenderer::Impl::textureVariantIndex() const {
    return texture_variant_index_;
}

int AttendBgfxRenderer::Impl::textureVariantCount() const {
    return static_cast<int>(pokemon_model_.texture_variants.size());
}

std::string AttendBgfxRenderer::Impl::textureVariantLabel(int index) const {
    if (index >= 0 && index < static_cast<int>(pokemon_model_.texture_variants.size())) {
        const AttendTextureVariantOption& option = pokemon_model_.texture_variants[static_cast<std::size_t>(index)];
        return option.label.empty() ? option.id : option.label;
    }
    return "Normal";
}

int AttendBgfxRenderer::Impl::formVariantIndex() const {
    return form_variant_index_;
}

int AttendBgfxRenderer::Impl::formVariantCount() const {
    return static_cast<int>(pokemon_model_.form_variants.size());
}

std::string AttendBgfxRenderer::Impl::formVariantId(int index) const {
    if (index >= 0 && index < static_cast<int>(pokemon_model_.form_variants.size())) {
        return pokemon_model_.form_variants[static_cast<std::size_t>(index)].id;
    }
    return {};
}

std::string AttendBgfxRenderer::Impl::formVariantLabel(int index) const {
    if (index >= 0 && index < static_cast<int>(pokemon_model_.form_variants.size())) {
        const AttendTextureVariantOption& option = pokemon_model_.form_variants[static_cast<std::size_t>(index)];
        return option.label.empty() ? option.id : option.label;
    }
    return "Form";
}

void AttendBgfxRenderer::Impl::queueScreenshot(const std::string& output_path) {
    backend_.queueScreenshot(output_path);
}

bool AttendBgfxRenderer::Impl::initialize(
    SDL_Window* window,
    int width,
    int height,
    const std::string& bgfx_preference,
    void* sdl_metal_view) {
    if (initialized_) {
        backend_.reset(width, height);
        return true;
    }

    if (!backend_.initialize(window, width, height, bgfx_preference, sdl_metal_view)) {
        last_error_ = backend_.lastError();
        return false;
    }

    layout_.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();

    if (!createPrograms() || !createWhiteTexture() || !buildFloor() || !buildWall() || !buildPokemon()) {
        shutdown();
        return false;
    }

    setWeatherMode(config_.floor.active_weather);
    initialized_ = true;
    return true;
}

void AttendBgfxRenderer::Impl::shutdown() {
    pixel_scene_target_.destroy();
    floor_mesh_.destroy();
    for (MeshResource& mesh : floor_extension_meshes_) {
        mesh.destroy();
    }
    wall_mesh_.destroy();
    pokemon_mesh_.destroy();
    white_texture_.destroy();
    for (OverlayButtonTexture& button : overlay_button_textures_) {
        button.destroy();
    }
    for (OverlayButtonTexture& button : corner_button_textures_) {
        button.destroy();
    }
    profile_plate_texture_.destroy();
    profile_plate_text_texture_.destroy();
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    if (bgfx::isValid(camera_sphere_program_)) bgfx::destroy(camera_sphere_program_);
    if (bgfx::isValid(eye_program_)) bgfx::destroy(eye_program_);
    if (bgfx::isValid(eye_sclera_mask_program_)) bgfx::destroy(eye_sclera_mask_program_);
    if (bgfx::isValid(tex_uniform_)) bgfx::destroy(tex_uniform_);
    if (bgfx::isValid(eye_mask_uniform_)) bgfx::destroy(eye_mask_uniform_);
    if (bgfx::isValid(tint_cutoff_uniform_)) bgfx::destroy(tint_cutoff_uniform_);
    if (bgfx::isValid(color_adjust_uniform_)) bgfx::destroy(color_adjust_uniform_);
    if (bgfx::isValid(texture_blur_uniform_)) bgfx::destroy(texture_blur_uniform_);
    if (bgfx::isValid(light_dir_uniform_)) bgfx::destroy(light_dir_uniform_);
    if (bgfx::isValid(light_params_uniform_)) bgfx::destroy(light_params_uniform_);
    program_ = BGFX_INVALID_HANDLE;
    camera_sphere_program_ = BGFX_INVALID_HANDLE;
    eye_program_ = BGFX_INVALID_HANDLE;
    eye_sclera_mask_program_ = BGFX_INVALID_HANDLE;
    tex_uniform_ = BGFX_INVALID_HANDLE;
    eye_mask_uniform_ = BGFX_INVALID_HANDLE;
    tint_cutoff_uniform_ = BGFX_INVALID_HANDLE;
    color_adjust_uniform_ = BGFX_INVALID_HANDLE;
    texture_blur_uniform_ = BGFX_INVALID_HANDLE;
    light_dir_uniform_ = BGFX_INVALID_HANDLE;
    light_params_uniform_ = BGFX_INVALID_HANDLE;
    floor_model_ = AttendPokemonModel{};
    floor_animation_ = nullptr;
    floor_skinned_primitives_.clear();
    floor_frame_vertices_.clear();
    floor_animated_ = false;
    floor_extension_meshes_.clear();
    overlay_button_textures_.clear();
    overlay_buttons_.clear();
    corner_button_textures_.clear();
    corner_buttons_.clear();
    profile_plate_ = AttendBgfxProfilePlate{};
    pokemon_bounds_valid_ = false;
    pokemon_model_ = AttendPokemonModel{};
    pokemon_animation_ = nullptr;
    skinned_primitives_.clear();
    pokemon_frame_vertices_.clear();
    pokemon_frame_vertex_primitives_.clear();
    pokemon_primitive_vertex_offsets_.clear();
    backend_.shutdown();
    initialized_ = false;
}

bool AttendBgfxRenderer::Impl::ensurePixelSceneTarget(int width, int height) {
    width = std::max(1, width);
    height = std::max(1, height);
    if (pixel_scene_target_.valid() &&
        pixel_scene_target_.width == width &&
        pixel_scene_target_.height == height) {
        return true;
    }

    pixel_scene_target_.destroy();
    bgfx::TextureHandle color = bgfx::createTexture2D(
        static_cast<std::uint16_t>(width),
        static_cast<std::uint16_t>(height),
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_RT | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT);
    bgfx::TextureHandle depth = bgfx::createTexture2D(
        static_cast<std::uint16_t>(width),
        static_cast<std::uint16_t>(height),
        false,
        1,
        bgfx::TextureFormat::D24S8,
        BGFX_TEXTURE_RT);
    if (!bgfx::isValid(color) || !bgfx::isValid(depth)) {
        if (bgfx::isValid(color)) bgfx::destroy(color);
        if (bgfx::isValid(depth)) bgfx::destroy(depth);
        last_error_ = "Could not create TEST ATTEND pixel scene render target";
        return false;
    }

    bgfx::Attachment attachments[2];
    attachments[0].init(color);
    attachments[1].init(depth);
    pixel_scene_target_.frame_buffer = bgfx::createFrameBuffer(2, attachments, true);
    if (!pixel_scene_target_.valid()) {
        bgfx::destroy(color);
        bgfx::destroy(depth);
        last_error_ = "Could not create TEST ATTEND pixel scene framebuffer";
        return false;
    }
    pixel_scene_target_.width = width;
    pixel_scene_target_.height = height;
    std::cerr << "[AttendBgfx] Pixel scene target "
              << width << "x" << height
              << " from shared worldViewport.upscale="
              << std::clamp(world_viewport_.internal_scale, 1, 4)
              << '\n';
    return true;
}

bool AttendBgfxRenderer::Impl::createPrograms() {
    tex_uniform_ = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
    eye_mask_uniform_ = bgfx::createUniform("s_eyeMask", bgfx::UniformType::Sampler);
    tint_cutoff_uniform_ = bgfx::createUniform("u_tintCutoff", bgfx::UniformType::Vec4);
    color_adjust_uniform_ = bgfx::createUniform("u_colorAdjust", bgfx::UniformType::Vec4);
    texture_blur_uniform_ = bgfx::createUniform("u_textureBlur", bgfx::UniformType::Vec4);
    light_dir_uniform_ = bgfx::createUniform("u_lightDir", bgfx::UniformType::Vec4);
    light_params_uniform_ = bgfx::createUniform("u_lightParams", bgfx::UniformType::Vec4);
    const fs::path shader_root = backend_.shaderDirectory();
    bgfx::ShaderHandle vs = loadShader(shader_root, backend_.shaderSubdirectory(), "vs_world");
    bgfx::ShaderHandle fs = loadShader(shader_root, backend_.shaderSubdirectory(), "fs_textured_cutout");
    bgfx::ShaderHandle vs_camera_sphere = loadShader(
        shader_root, backend_.shaderSubdirectory(), "vs_world_camera_sphere");
    bgfx::ShaderHandle fs_camera_sphere = loadShader(
        shader_root, backend_.shaderSubdirectory(), "fs_textured_cutout");
    bgfx::ShaderHandle vs_eye = loadShader(shader_root, backend_.shaderSubdirectory(), "vs_world");
    bgfx::ShaderHandle fs_eye = loadShader(shader_root, backend_.shaderSubdirectory(), "fs_pokemon_eye");
    bgfx::ShaderHandle vs_eye_mask = loadShader(shader_root, backend_.shaderSubdirectory(), "vs_world");
    bgfx::ShaderHandle fs_eye_mask = loadShader(shader_root, backend_.shaderSubdirectory(), "fs_eye_sclera_mask");
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs) ||
        !bgfx::isValid(vs_camera_sphere) || !bgfx::isValid(fs_camera_sphere) ||
        !bgfx::isValid(vs_eye) || !bgfx::isValid(fs_eye) ||
        !bgfx::isValid(vs_eye_mask) || !bgfx::isValid(fs_eye_mask)) {
        if (bgfx::isValid(vs)) bgfx::destroy(vs);
        if (bgfx::isValid(fs)) bgfx::destroy(fs);
        if (bgfx::isValid(vs_camera_sphere)) bgfx::destroy(vs_camera_sphere);
        if (bgfx::isValid(fs_camera_sphere)) bgfx::destroy(fs_camera_sphere);
        if (bgfx::isValid(vs_eye)) bgfx::destroy(vs_eye);
        if (bgfx::isValid(fs_eye)) bgfx::destroy(fs_eye);
        if (bgfx::isValid(vs_eye_mask)) bgfx::destroy(vs_eye_mask);
        if (bgfx::isValid(fs_eye_mask)) bgfx::destroy(fs_eye_mask);
        last_error_ = "Could not load attend shaders";
        return false;
    }
    program_ = bgfx::createProgram(vs, fs, true);
    camera_sphere_program_ = bgfx::createProgram(vs_camera_sphere, fs_camera_sphere, true);
    eye_program_ = bgfx::createProgram(vs_eye, fs_eye, true);
    eye_sclera_mask_program_ = bgfx::createProgram(vs_eye_mask, fs_eye_mask, true);
    if (!bgfx::isValid(program_) || !bgfx::isValid(camera_sphere_program_) ||
        !bgfx::isValid(eye_program_) || !bgfx::isValid(eye_sclera_mask_program_)) {
        last_error_ = "Could not create attend shader program";
        return false;
    }
    return true;
}

bool AttendBgfxRenderer::Impl::createWhiteTexture() {
    const std::uint32_t white = 0xffffffffu;
    const bgfx::Memory* mem = bgfx::copy(&white, sizeof(white));
    white_texture_.handle = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, mem);
    return white_texture_.valid();
}

bool AttendBgfxRenderer::Impl::uploadMesh(
    MeshResource& mesh,
    const std::vector<Vertex>& vertices,
    const std::vector<std::uint32_t>& indices) {
    if (vertices.empty() || indices.empty()) return true;
    const bgfx::Memory* vb_mem = bgfx::copy(vertices.data(), static_cast<std::uint32_t>(vertices.size() * sizeof(Vertex)));
    const bgfx::Memory* ib_mem = bgfx::copy(indices.data(), static_cast<std::uint32_t>(indices.size() * sizeof(std::uint32_t)));
    mesh.vbh = bgfx::createVertexBuffer(vb_mem, layout_);
    mesh.ibh = bgfx::createIndexBuffer(ib_mem, BGFX_BUFFER_INDEX32);
    mesh.vertex_count = static_cast<std::uint32_t>(vertices.size());
    mesh.dynamic = false;
    return mesh.valid();
}

bool AttendBgfxRenderer::Impl::uploadDynamicMesh(
    MeshResource& mesh,
    const std::vector<Vertex>& vertices,
    const std::vector<std::uint32_t>& indices) {
    if (vertices.empty() || indices.empty()) return true;
    const bgfx::Memory* ib_mem = bgfx::copy(indices.data(), static_cast<std::uint32_t>(indices.size() * sizeof(std::uint32_t)));
    mesh.dvbh = bgfx::createDynamicVertexBuffer(static_cast<std::uint32_t>(vertices.size()), layout_, BGFX_BUFFER_NONE);
    mesh.ibh = bgfx::createIndexBuffer(ib_mem, BGFX_BUFFER_INDEX32);
    mesh.vertex_count = static_cast<std::uint32_t>(vertices.size());
    mesh.dynamic = true;
    if (!mesh.valid()) return false;
    const bgfx::Memory* vb_mem = bgfx::copy(vertices.data(), static_cast<std::uint32_t>(vertices.size() * sizeof(Vertex)));
    bgfx::update(mesh.dvbh, 0, vb_mem);
    return true;
}

} // namespace pr::gameplay::attend::rendering
