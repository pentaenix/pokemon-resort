#pragma once

#include "gameplay/attend/rendering/AttendBgfxRenderer.hpp"

#include "core/config/Json.hpp"
#include "gameplay/attend/rendering/AttendPokemonModel.hpp"
#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/data/GlbModelLoader.hpp"

#include <SDL_image.h>
#include <SDL_ttf.h>
#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <utility>
#include <vector>

namespace pr::gameplay::attend::rendering {

namespace bgfx_backend = pr::gameplay::world3d::rendering::bgfx_backend;
namespace fs = std::filesystem;

constexpr float kPi = 3.1415926535f;

struct Vertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float nx = 0.0f;
    float ny = 1.0f;
    float nz = 0.0f;
    std::uint32_t abgr = 0xffffffffu;
    float u = 0.0f;
    float v = 0.0f;
};

std::uint32_t packAbgr(float r, float g, float b, float a = 1.0f);
std::uint8_t byteChannel(float v);
bool insideRoundedRect(int x, int y, int w, int h, int radius);
void setRgba(std::vector<std::uint8_t>& pixels, int w, int x, int y, const Color4& color);
Color4 mixColor(Color4 a, Color4 b, float t);
void blendRgba(
    std::vector<std::uint8_t>& pixels,
    int w,
    int x,
    int y,
    std::uint8_t r,
    std::uint8_t g,
    std::uint8_t b,
    std::uint8_t a);
std::string overlayStyleKey(const AttendOverlayButtonConfig& style, const std::string& label, int w, int h);
std::string cornerButtonStyleKey(const AttendBgfxCornerButton& button);
bool insideCornerButtonShape(int x, int y, int w, int h, bool left, bool top, int extension);
int inwardBoundaryDistance(int x, int y, int w, int h, bool left, bool top, int extension, int max_distance);
Color3 mix(Color3 a, Color3 b, float t);
Color3 sampleGradient(const std::vector<GradientStop>& stops, float at);
bool containsAnySubstring(const std::string& value, const std::vector<std::string>& needles);
std::string lowercaseAscii(std::string value);
bool containsAscii(std::string value, const char* needle);
int jsonIntOr(const JsonValue* value, int fallback);
bool jsonBoolOr(const JsonValue* value, bool fallback);
pr::gameplay::world3d::WorldViewportConfig loadSharedWorldViewportConfig(const std::string& project_root);
void normalize3(float& x, float& y, float& z);
void identity(float (&m)[16]);
void placementMatrix(float x, float y, float z, float yaw_deg, float pitch_deg, float scale, float (&m)[16]);
std::uint64_t samplerFlags(int wrap_s = 10497, int wrap_t = 10497);
std::uint64_t smoothSamplerFlags(int wrap_s = 10497, int wrap_t = 10497);
std::uint64_t samplerFlagsFromGfWrap(int wrap_s, int wrap_t);
std::uint64_t opaqueState();
std::uint64_t blendState();
std::uint64_t overlayState();
std::uint64_t eyeScleraStencilState();
std::uint64_t backdropState(bool blend);
int expressionFrameOr(const AttendSceneConfig& config, const char* key, int fallback);
int mouthExpressionFrameOr(const AttendSceneConfig& config, const char* key, int fallback);
bool materialUsesEyeExpressionFrames(const AttendPokemonMaterial* material);
bool materialUsesMouthExpressionFrames(const AttendPokemonMaterial* material);
bool materialIsEyeScleraMask(const AttendPokemonMaterial* material);
bool materialIsSeparateEyeIris(const AttendPokemonMaterial* material);
bool pokemonPrimitivePreviewVisible(
    const AttendPokemonModel& model,
    const AttendPokemonPrimitive& primitive);
bool pokemonPrimitivePreviewVisible(
    const AttendPokemonModel& model,
    const AttendPokemonPrimitive& primitive,
    const std::vector<AttendTextureVariantOption>& form_variants,
    int form_variant_index);
bool stringListContains(const std::vector<std::string>& values, const std::string& needle);
Vertex transformStaticVertex(
    const pr::gameplay::world3d::data::GlbVertex& src,
    float x,
    float y,
    float z,
    float yaw_deg,
    float scale);
Vertex transformAttendVertex(
    const AttendPokemonVertex& src,
    float x,
    float y,
    float z,
    float yaw_deg,
    float scale);
Vertex pokemonVertexForMaterial(
    const AttendPokemonVertex& src,
    const AttendPokemonMaterial* material,
    int eye_expression_frame,
    int mouth_expression_frame);
bgfx::ShaderHandle loadShader(const fs::path& shader_root, const std::string& shader_subdir, const char* name);

class AttendBgfxRenderer::Impl {
public:
    Impl(std::string project_root, AttendSceneConfig config);
    ~Impl();

    bool initialize(SDL_Window* window, int width, int height, const std::string& bgfx_preference, void* sdl_metal_view);
    void shutdown();
    bool valid() const;
    std::string lastError() const;
    void setPetting(bool petting);
    void setPetContact(float vertical_bias);
    void setFaceLook(float x, float y);
    void setViewportLook(float x, float y);
    void setFreeCamera(bool enabled, AttendFreeCameraPose pose);
    bool currentCameraPose(AttendFreeCameraPose& out) const;
    void setFaceView(bool face_view);
    bool faceViewTransitionActive() const;
    bool canTriggerReaction(const std::string& reaction_id, double interaction_seconds) const;
    bool canTriggerSemanticAnimation(const std::string& animation_semantic) const;
    void triggerReaction(std::string reaction_id, double interaction_seconds);
    void triggerSemanticAnimation(
        std::string animation_semantic,
        double duration_seconds,
        double fade_in_seconds,
        double fade_out_seconds,
        std::string eye_expression,
        std::string mouth_expression);
    double wakeFromIdleAnimation();
    bool isIdleSleeping() const;
    void blendOutIdleAnimation(double scene_time_seconds, double fade_out_seconds);
    bool faceViewAvailable() const;
    SDL_Rect pokemonPointerRect() const;
    void setWeatherMode(int index);
    void setTextureVariant(int index);
    void setFormVariant(int index);
    void setOverlayButtons(std::vector<AttendBgfxOverlayButton> buttons, int logical_w, int logical_h);
    void setCornerButtons(std::vector<AttendBgfxCornerButton> buttons, int logical_w, int logical_h);
    void setProfilePlate(AttendBgfxProfilePlate plate, int logical_w, int logical_h);
    int weatherModeCount() const;
    int textureVariantIndex() const;
    int textureVariantCount() const;
    std::string textureVariantLabel(int index) const;
    int formVariantIndex() const;
    int formVariantCount() const;
    std::string formVariantId(int index) const;
    std::string formVariantLabel(int index) const;
    void render(double scene_time_seconds, int width, int height);
    void queueScreenshot(const std::string& output_path);

private:
    struct TextureResource {
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        int width = 0;
        int height = 0;
        bool has_zero_alpha = false;
        bool has_partial_alpha = false;
        bool valid() const { return bgfx::isValid(handle); }
        void destroy() {
            if (bgfx::isValid(handle)) bgfx::destroy(handle);
            handle = BGFX_INVALID_HANDLE;
            width = 0;
            height = 0;
            has_zero_alpha = false;
            has_partial_alpha = false;
        }
    };

    struct MaterialResource {
        std::string name;
        TextureResource texture;
        TextureResource eye_mask_texture;
        float base_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        bool visible = true;
        bool blend = false;
        bool mask_cutout = false;
        bool pokemon_eye = false;
        bool eye_sclera_mask = false;
        bool separate_eye_iris = false;
        float alpha_cutoff = 0.5f;
        std::uint64_t sampler_flags = samplerFlags();
        std::vector<int> texture_variant_materials;
        std::vector<std::pair<std::string, int>> form_variant_materials;
    };

    struct OverlayButtonTexture {
        TextureResource texture;
        std::string key;
        void destroy() {
            texture.destroy();
            key.clear();
        }
    };

    struct MeshResource {
        bgfx::VertexBufferHandle vbh = BGFX_INVALID_HANDLE;
        bgfx::DynamicVertexBufferHandle dvbh = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle ibh = BGFX_INVALID_HANDLE;
        struct Range {
            std::uint32_t start = 0;
            std::uint32_t count = 0;
            int material = -1;
            std::vector<std::string> visible_for_forms;
        };
        std::vector<Range> ranges;
        std::vector<MaterialResource> materials;
        std::uint32_t vertex_count = 0;
        bool dynamic = false;
        bool valid() const { return (bgfx::isValid(vbh) || bgfx::isValid(dvbh)) && bgfx::isValid(ibh); }
        void destroy() {
            if (bgfx::isValid(vbh)) bgfx::destroy(vbh);
            if (bgfx::isValid(dvbh)) bgfx::destroy(dvbh);
            if (bgfx::isValid(ibh)) bgfx::destroy(ibh);
            vbh = BGFX_INVALID_HANDLE;
            dvbh = BGFX_INVALID_HANDLE;
            ibh = BGFX_INVALID_HANDLE;
            for (MaterialResource& material : materials) {
                material.texture.destroy();
                material.eye_mask_texture.destroy();
            }
            ranges.clear();
            materials.clear();
            vertex_count = 0;
            dynamic = false;
        }
    };

    struct PixelSceneTarget {
        bgfx::FrameBufferHandle frame_buffer = BGFX_INVALID_HANDLE;
        int width = 0;
        int height = 0;

        bool valid() const { return bgfx::isValid(frame_buffer); }

        void destroy() {
            if (bgfx::isValid(frame_buffer)) {
                bgfx::destroy(frame_buffer);
                frame_buffer = BGFX_INVALID_HANDLE;
            }
            width = 0;
            height = 0;
        }
    };

    std::string project_root_;
    AttendSceneConfig config_;
    pr::gameplay::world3d::WorldViewportConfig world_viewport_;
    bgfx_backend::BgfxBackend backend_;
    bool initialized_ = false;
    std::string last_error_;
    bgfx::VertexLayout layout_{};
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle eye_program_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle eye_sclera_mask_program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle tex_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle eye_mask_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle tint_cutoff_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle color_adjust_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle texture_blur_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_dir_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_params_uniform_ = BGFX_INVALID_HANDLE;
    TextureResource white_texture_;
    std::vector<OverlayButtonTexture> overlay_button_textures_;
    std::vector<OverlayButtonTexture> corner_button_textures_;
    OverlayButtonTexture profile_plate_texture_;
    OverlayButtonTexture profile_plate_text_texture_;
    MeshResource floor_mesh_;
    std::vector<MeshResource> floor_extension_meshes_;
    MeshResource wall_mesh_;
    MeshResource pokemon_mesh_;
    AttendPokemonModel floor_model_;
    const AttendPokemonAnimation* floor_animation_ = nullptr;
    std::vector<std::vector<AttendPokemonVertex>> floor_skinned_primitives_;
    std::vector<Vertex> floor_frame_vertices_;
    bool floor_animated_ = false;
    bool pokemon_bounds_valid_ = false;
    bool camera_bounds_valid_ = false;
    float pokemon_world_height_ = 0.0f;
    float pokemon_min_x_ = 0.0f;
    float pokemon_min_y_ = 0.0f;
    float pokemon_min_z_ = 0.0f;
    float pokemon_max_x_ = 0.0f;
    float pokemon_max_y_ = 0.0f;
    float pokemon_max_z_ = 0.0f;
    float camera_world_height_ = 0.0f;
    float camera_min_x_ = 0.0f;
    float camera_min_y_ = 0.0f;
    float camera_min_z_ = 0.0f;
    float camera_max_x_ = 0.0f;
    float camera_max_y_ = 0.0f;
    float camera_max_z_ = 0.0f;
    AttendPokemonModel pokemon_model_;
    const AttendPokemonAnimation* pokemon_animation_ = nullptr;
    const AttendPokemonAnimation* pokemon_eye_close_animation_ = nullptr;
    const AttendPokemonAnimation* reaction_animation_ = nullptr;
    std::vector<std::size_t> pokemon_draw_order_;
    bool petting_ = false;
    bool was_petting_ = false;
    float pet_eye_close_amount_ = 0.0f;
    float pet_contact_target_ = 0.0f;
    float pet_contact_bias_ = 0.0f;
    double last_pet_update_seconds_ = -1.0;
    double pet_started_seconds_ = -1.0;
    double pet_eye_close_cooldown_until_seconds_ = 0.0;
    std::mt19937 blink_rng_{0x50525944u};
    float random_blink_amount_ = 0.0f;
    double next_random_blink_seconds_ = -1.0;
    double random_blink_started_seconds_ = -1.0;
    int normal_eye_expression_frame_ = 0;
    int closed_eye_expression_frame_ = 4;
    int reaction_eye_expression_frame_ = 0;
    int current_eye_expression_frame_ = 0;
    int normal_mouth_expression_frame_ = 0;
    int reaction_mouth_expression_frame_ = 0;
    int current_mouth_expression_frame_ = 0;
    bool has_eye_expression_frames_ = false;
    bool has_mouth_expression_frames_ = false;
    std::string pending_reaction_id_;
    double pending_reaction_interaction_seconds_ = 0.0;
    std::string pending_semantic_animation_;
    std::string pending_semantic_eye_expression_;
    std::string pending_semantic_mouth_expression_;
    double pending_semantic_duration_seconds_ = 0.0;
    double pending_semantic_fade_in_seconds_ = 0.2;
    double pending_semantic_fade_out_seconds_ = 0.3;
    bool pending_semantic_reverse_ = false;
    bool pending_wake_from_sleep_ = false;
    bool pending_sleep_loop_after_start_ = false;
    double pending_sleep_loop_duration_seconds_ = 0.0;
    double pending_sleep_loop_fade_in_seconds_ = 0.0;
    double pending_sleep_loop_fade_out_seconds_ = 0.0;
    std::string pending_sleep_loop_eye_expression_;
    std::string pending_sleep_loop_mouth_expression_;
    bool reaction_active_ = false;
    bool reaction_from_semantic_ = false;
    bool reaction_reverse_ = false;
    bool semantic_sleeping_ = false;
    double reaction_start_seconds_ = 0.0;
    double reaction_duration_seconds_ = 0.0;
    double reaction_fade_in_seconds_ = 0.0;
    double reaction_fade_out_seconds_ = 0.0;
    double reaction_eye_linger_until_seconds_ = -1.0;
    float face_target_x_ = 0.0f;
    float face_target_y_ = 0.0f;
    float face_look_x_ = 0.0f;
    float face_look_y_ = 0.0f;
    float viewport_look_target_x_ = 0.0f;
    float viewport_look_target_y_ = 0.0f;
    float viewport_look_x_ = 0.0f;
    float viewport_look_y_ = 0.0f;
    double last_viewport_look_update_seconds_ = -1.0;
    bool face_view_ = false;
    float face_view_blend_ = 0.0f;
    double last_face_view_transition_update_seconds_ = -1.0;
    bool freecam_enabled_ = false;
    AttendFreeCameraPose freecam_pose_{};
    bool last_camera_pose_valid_ = false;
    AttendFreeCameraPose last_camera_pose_{};
    int texture_variant_index_ = 0;
    int form_variant_index_ = 0;
    int floor_weather_index_ = 0;
    std::vector<AttendBgfxOverlayButton> overlay_buttons_;
    int overlay_logical_w_ = 1280;
    int overlay_logical_h_ = 800;
    std::vector<AttendBgfxCornerButton> corner_buttons_;
    int corner_logical_w_ = 1280;
    int corner_logical_h_ = 800;
    AttendBgfxProfilePlate profile_plate_;
    int profile_plate_logical_w_ = 1280;
    int profile_plate_logical_h_ = 800;
    SDL_Rect presentation_rect_{0, 0, 1280, 800};
    double last_face_update_seconds_ = -1.0;
    std::vector<std::vector<AttendPokemonVertex>> skinned_primitives_;
    std::vector<Vertex> pokemon_frame_vertices_;
    std::vector<std::size_t> pokemon_frame_vertex_primitives_;
    std::vector<std::uint32_t> pokemon_primitive_vertex_offsets_;
    SDL_Rect last_pokemon_pointer_rect_{0, 0, 0, 0};
    PixelSceneTarget pixel_scene_target_;

    bool createPrograms();
    bool createWhiteTexture();
    bool buildFloor();
    bool buildWall();
    bool buildPokemon();
    bool buildAnimatedFloor();
    bool ensurePixelSceneTarget(int width, int height);
    bool buildStaticGlbMesh(
        MeshResource& mesh,
        const std::string& path,
        float x,
        float y,
        float z,
        float yaw_degrees,
        float scale);
    bool uploadMesh(MeshResource& mesh, const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices);
    bool uploadDynamicMesh(MeshResource& mesh, const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices);
    TextureResource decodeTexture(const std::vector<std::uint8_t>& bytes, const char* debug_name);
    TextureResource buildPokemonEyeTexture(
        const std::vector<std::uint8_t>& base_bytes,
        const std::vector<std::uint8_t>& lym_bytes,
        const char* debug_name);
    bool ensureOverlayButtonTexture(std::size_t index);
    bool ensureCornerButtonTexture(std::size_t index);
    bool ensureProfilePlateTexture(float texture_scale, bool text_only, bool include_text);
    void scheduleNextRandomBlink(double scene_time_seconds);
    void updateRandomBlink(double scene_time_seconds);
    void updatePetControls(double scene_time_seconds);
    void updateFaceControls(double scene_time_seconds);
    void updateViewportLook(double scene_time_seconds);
    void updateFaceViewTransition(double scene_time_seconds);
    void updateFloorAnimation(double scene_time_seconds);
    void updatePokemonAnimation(double scene_time_seconds);
    void startPendingReaction(double scene_time_seconds);
    void startPendingSemanticAnimation(double scene_time_seconds);
    void startPendingWakeFromSleep(double scene_time_seconds);
    void startPendingSleepLoop(double scene_time_seconds);
    float reactionWeight(double scene_time_seconds) const;
    const AttendPokemonAnimation* resolveSemanticAnimation(const std::string& semantic) const;
    int eyeExpressionFrame(const std::string& semantic, int fallback) const;
    int mouthExpressionFrame(const std::string& semantic, int fallback) const;
    const AttendInteractionAdapterConfig::ReactionCombo* reactionCombo(const std::string& id) const;
    float petReadyCueWeight(double scene_time_seconds, const AttendInteractionAdapterConfig::ReactionCombo& combo) const;
    bool floorMaterialVisible(const MaterialResource* material) const;
    bool pokemonPrimitiveVisibleForHit(std::size_t primitive_index) const;
    void updatePokemonBoundsFromVertices(const std::vector<Vertex>& vertices);
    void captureCameraBoundsFromCurrentPose();
    void cameraForFrame(float look_x, float look_y, bx::Vec3& eye, bx::Vec3& at) const;
    void updatePokemonPointerRect(const float* pokemon_matrix, const float* view, const float* proj, int width, int height);
    void submitMesh(const MeshResource& mesh, const float* matrix, bool force_blend = false, bool backdrop = false, bool floor = false);
    void submitPokemonShadow();
    void submitPixelSceneToBackbuffer(
        int framebuffer_w,
        int framebuffer_h,
        int source_w,
        int source_h,
        std::uint8_t view_id);
    void submitProfilePlate(
        int framebuffer_w,
        int framebuffer_h,
        bgfx::FrameBufferHandle target,
        std::uint8_t view_id,
        bool text_only = false,
        bool include_text = true);
    void submitOverlayButtons(int framebuffer_w, int framebuffer_h, bgfx::FrameBufferHandle target, std::uint8_t view_id);
    void submitCornerButtons(
        int framebuffer_w,
        int framebuffer_h,
        bgfx::FrameBufferHandle target,
        std::uint8_t view_id);
};

} // namespace pr::gameplay::attend::rendering
