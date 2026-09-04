#include "gameplay/world3d/rendering/bgfx/OverworldBgfxRenderer.hpp"

#include "gameplay/world3d/rendering/bgfx/BillboardBgfxDrawer.hpp"
#include "gameplay/world3d/rendering/BillboardPlacement.hpp"
#include "gameplay/world3d/rendering/CharacterTextureCache.hpp"
#include "gameplay/world3d/rendering/PixelScale.hpp"
#include "gameplay/world3d/rendering/SpriteShadowDecal.hpp"
#include "gameplay/world3d/data/GlbModelLoader.hpp"
#include "gameplay/world3d/data/RtpksTilePackageLoader.hpp"
#include "gameplay/world3d/doors/DoorAnimationPolicy.hpp"
#include "gameplay/world3d/interiors/DefaultRoomGeometry.hpp"
#include "gameplay/world3d/interiors/InteriorFloorCutout.hpp"
#include "gameplay/world3d/aquarium/rendering/AquariumFloorCutouts.hpp"
#include "gameplay/world3d/aquarium/rendering/AquariumPokemonBgfxRenderer.hpp"
#include "gameplay/world3d/aquarium/rendering/AquariumConstructionBgfxRenderer.hpp"
#include "gameplay/world3d/aquarium/rendering/PlayerAquariumBgfxRenderer.hpp"
#include "gameplay/world3d/rendering/InteriorDefaultRoom.hpp"
#include "gameplay/world3d/rendering/InteriorRenderPolicy.hpp"
#include "gameplay/world3d/rendering/bgfx/BgfxBackend.hpp"
#include "gameplay/world3d/rendering/bgfx/RampTerrainRender.hpp"
#include "gameplay/world3d/terrain/TerrainSurface.hpp"
#include "core/assets/Font.hpp"
#include "ui/overlay/OverlaySliceLayout.hpp"

#include <SDL_image.h>
#include <SDL_ttf.h>

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace pr::gameplay::world3d::rendering::bgfx_backend {

namespace {

enum : int {
    kSpecialFlat = 0,
    kSpecialRampNorth = 2,
    kSpecialRampEast = 3,
    kSpecialRampSouth = 4,
    kSpecialRampWest = 5,
    kSpecialConvexNE = 6,
    kSpecialConvexSE = 7,
    kSpecialConvexSW = 8,
    kSpecialConvexNW = 9,
    kSpecialConcaveNE = 10,
    kSpecialConcaveSE = 11,
    kSpecialConcaveSW = 12,
    kSpecialConcaveNW = 13,
};

constexpr float kPlayerBillboardDepthPriorityBias = -0.25f;

std::uint32_t packAbgr(float r, float g, float b, float a = 1.0f) {
    const auto c = [](float v) -> std::uint32_t {
        return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return (c(a) << 24U) | (c(b) << 16U) | (c(g) << 8U) | c(r);
}

bgfx::ShaderHandle loadShader(const std::filesystem::path& shader_root, const std::string& shader_subdir, const char* name) {
    const std::filesystem::path path = shader_root / shader_subdir / (std::string(name) + ".sc.bin");
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "[OverworldBgfx] Missing shader: " << path << std::endl;
        return BGFX_INVALID_HANDLE;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size <= 0) return BGFX_INVALID_HANDLE;
    const bgfx::Memory* mem = bgfx::alloc(static_cast<std::uint32_t>(size + 1));
    in.read(reinterpret_cast<char*>(mem->data), size);
    mem->data[size] = '\0';
    bgfx::ShaderHandle shader = bgfx::createShader(mem);
    if (bgfx::isValid(shader)) {
        bgfx::setName(shader, name);
    }
    return shader;
}

int terrainWidth(const SceneConfig& scene) {
    if (!scene.terrain.heights.empty() && !scene.terrain.heights.front().empty()) {
        return static_cast<int>(scene.terrain.heights.front().size());
    }
    return std::max(1, scene.grid.width);
}

int terrainHeight(const SceneConfig& scene) {
    if (!scene.terrain.heights.empty()) {
        return static_cast<int>(scene.terrain.heights.size());
    }
    return std::max(1, scene.grid.height);
}

void identity(float (&m)[16]) {
    std::fill(std::begin(m), std::end(m), 0.0f);
    m[0] = 1.0f;
    m[5] = 1.0f;
    m[10] = 1.0f;
    m[15] = 1.0f;
}

void cameraViewMatrix(const camera::Gen4FollowCamera::Pose& pose, float (&m)[16]) {
    m[0] = pose.right.x;
    m[1] = pose.up.x;
    m[2] = pose.forward.x;
    m[3] = 0.0f;
    m[4] = pose.right.y;
    m[5] = pose.up.y;
    m[6] = pose.forward.y;
    m[7] = 0.0f;
    m[8] = pose.right.z;
    m[9] = pose.up.z;
    m[10] = pose.forward.z;
    m[11] = 0.0f;
    m[12] = -((pose.right.x * pose.position.x) + (pose.right.y * pose.position.y) + (pose.right.z * pose.position.z));
    m[13] = -((pose.up.x * pose.position.x) + (pose.up.y * pose.position.y) + (pose.up.z * pose.position.z));
    m[14] = -((pose.forward.x * pose.position.x) + (pose.forward.y * pose.position.y) + (pose.forward.z * pose.position.z));
    m[15] = 1.0f;
}

void placementMatrix(float x, float y, float z, float yaw_deg, float scale, float (&m)[16]) {
    identity(m);
    const float yaw = yaw_deg * (3.1415926535f / 180.0f);
    const float c = std::cos(yaw) * scale;
    const float s = std::sin(yaw) * scale;
    m[0] = c;
    m[2] = -s;
    m[5] = scale;
    m[8] = s;
    m[10] = c;
    m[12] = x;
    m[13] = y;
    m[14] = z;
}

MaterialClass materialClassFor(const data::GlbMaterial& material) {
    if (material.render_class == data::GlbMaterial::RenderClass::UniformDecal) {
        return MaterialClass::TrueBlend;
    }
    if (material.alpha_mode == data::GlbMaterial::AlphaMode::Mask) return MaterialClass::MaskCutout;
    if (material.alpha_mode == data::GlbMaterial::AlphaMode::Blend) return MaterialClass::TrueBlend;
    if (material.base_color[3] < 0.999f) return MaterialClass::TrueBlend;
    return MaterialClass::Opaque;
}

std::uint64_t samplerFlags(
    const std::string& wrap_s = "repeat",
    const std::string& wrap_t = "repeat",
    const std::string& min_filter = "nearest",
    const std::string& mag_filter = "nearest") {
    std::uint64_t flags = BGFX_SAMPLER_MIP_POINT;
    if (min_filter != "linear") flags |= BGFX_SAMPLER_MIN_POINT;
    if (mag_filter != "linear") flags |= BGFX_SAMPLER_MAG_POINT;
    if (wrap_s == "clamp") flags |= BGFX_SAMPLER_U_CLAMP;
    else if (wrap_s == "mirror") flags |= BGFX_SAMPLER_U_MIRROR;
    if (wrap_t == "clamp") flags |= BGFX_SAMPLER_V_CLAMP;
    else if (wrap_t == "mirror") flags |= BGFX_SAMPLER_V_MIRROR;
    return flags;
}

float wrappedUvLerp(float a, float b, float amount, float period) {
    float delta = b - a;
    if (period > 0.0f) delta -= std::round(delta / period) * period;
    return a + delta * amount;
}

struct AnimationCursor {
    int frame = 0;
    int next_frame = 0;
    float fraction = 0.0f;
};

AnimationCursor animationCursor(double raw_sample, int frame_count, bool loop) {
    frame_count = std::max(1, frame_count);
    raw_sample = std::max(0.0, raw_sample);
    const std::int64_t raw_frame = static_cast<std::int64_t>(std::floor(raw_sample));
    AnimationCursor cursor;
    cursor.frame = loop
        ? static_cast<int>(raw_frame % frame_count)
        : std::min(frame_count - 1, static_cast<int>(raw_frame));
    cursor.next_frame = loop
        ? (cursor.frame + 1) % frame_count
        : std::min(frame_count - 1, cursor.frame + 1);
    cursor.fraction = static_cast<float>(raw_sample - std::floor(raw_sample));
    return cursor;
}

double waveSampleWithWait(
    double animation_time_ms,
    double timebase_hz,
    int frame_count,
    bool loop,
    double speed,
    double wait_seconds) {
    speed = std::max(0.0, speed);
    if (speed <= 0.0 || timebase_hz <= 0.0) return 0.0;
    if (!loop || wait_seconds <= 0.0) {
        return animation_time_ms * timebase_hz * speed / 1000.0;
    }
    const double motion_seconds = static_cast<double>(std::max(1, frame_count)) /
        (timebase_hz * speed);
    const double cycle_seconds = motion_seconds + wait_seconds;
    const double phase_seconds = std::fmod(animation_time_ms / 1000.0, cycle_seconds);
    if (phase_seconds >= motion_seconds) {
        return static_cast<double>(std::max(1, frame_count) - 1);
    }
    return phase_seconds * timebase_hz * speed;
}

bool isWaterMaterial(const std::string& name, const std::string& layer_role) {
    std::string key = name + " " + layer_role;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return key.find("water") != std::string::npos ||
           key.find("shoreline") != std::string::npos ||
           key.find("shore") != std::string::npos ||
           key.find("sea_") != std::string::npos ||
           key.find("mizu") != std::string::npos ||
           key.find("kawa") != std::string::npos ||
           key.find("zanami") != std::string::npos ||
           key.find("simi") != std::string::npos ||
           key.find("puddle") != std::string::npos ||
           key.find("numa") != std::string::npos ||
           key.find("ike") != std::string::npos;
}

std::uint64_t stateFor(MaterialClass pass) {
    const std::uint64_t base = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS;
    if (pass == MaterialClass::TrueBlend) {
        return base | BGFX_STATE_BLEND_ALPHA;
    }
    return base | BGFX_STATE_WRITE_Z;
}

std::uint64_t stateForDepthOnly() {
    return BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
}

std::uint32_t shadowStencilTestOnly() {
    return BGFX_STENCIL_TEST_EQUAL |
           BGFX_STENCIL_FUNC_REF(0) |
           BGFX_STENCIL_FUNC_RMASK(0xff) |
           BGFX_STENCIL_OP_FAIL_S_KEEP |
           BGFX_STENCIL_OP_FAIL_Z_KEEP |
           BGFX_STENCIL_OP_PASS_Z_KEEP;
}

std::uint32_t shadowStencilMark() {
    return BGFX_STENCIL_TEST_EQUAL |
           BGFX_STENCIL_FUNC_REF(0) |
           BGFX_STENCIL_FUNC_RMASK(0xff) |
           BGFX_STENCIL_OP_FAIL_S_KEEP |
           BGFX_STENCIL_OP_FAIL_Z_KEEP |
           BGFX_STENCIL_OP_PASS_Z_INCRSAT;
}

} // namespace

class OverworldBgfxRenderer::Impl {
public:
    Impl(std::string project_root, SceneConfig scene, CharacterSpriteDefinition character)
        : project_root_(std::move(project_root)), scene_(std::move(scene)),
          authored_floor_cutouts_(scene_.interior.floor_cutouts),
          character_(std::move(character)) {}
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    ~Impl() { shutdown(); }

    bool initialize(
        SDL_Window* window,
        int width,
        int height,
        const std::string& bgfx_preference,
        void* sdl_metal_view);
    void shutdown();
    bool valid() const { return initialized_ && backend_.valid(); }
    std::string lastError() const { return last_error_; }
    void setStaticMapChunks(std::vector<OverworldBgfxRenderer::StaticMapChunk> chunks);
    void setAquariumPokemonActors(std::vector<aquarium::AquariumPokemonActor> actors);
    void setAquariumTankLights(
        std::vector<aquarium::AquariumTankRuntime> tanks,
        const aquarium::AquariumTankLightingConfig& lighting);
    void setAquariumConstructionVisual(
        aquarium::construction::AquariumConstructionVisual visual);
    bool replacePlayerAquariumTanks(
        const std::vector<aquarium::construction::PlayerTankRuntime>& tanks,
        std::string* error);
    bool stagePlayerAquariumTanks(
        const std::vector<aquarium::construction::PlayerTankRuntime>& tanks,
        std::string* error);
    bool publishStagedPlayerAquariumTanks();
    void discardStagedPlayerAquariumTanks();
    std::size_t playerAquariumResourceCount() const;
    void setSceneLighting(float brightness, const std::array<float, 3>& tint) {
        scene_.lighting_brightness = std::max(0.0f, brightness);
        scene_.lighting_tint_r = tint[0];
        scene_.lighting_tint_g = tint[1];
        scene_.lighting_tint_b = tint[2];
        if (!aquarium_tank_lighting_.enabled) {
            player_aquarium_renderer_.setLighting(brightness, tint);
        }
        refreshBillboardDrawer();
    }
    void setPlayerVisible(bool visible) { player_visible_ = visible; }
    void setInteriorWallCameraClip(camera::Vec3 center, float radius_world) {
        interior_wall_clip_[0] = center.x;
        interior_wall_clip_[1] = center.y;
        interior_wall_clip_[2] = center.z;
        interior_wall_clip_[3] = std::max(0.0f, radius_world);
    }
    void setTextboxOverlay(dialogue::OverworldTextboxConfig config, bool visible, std::string text);
    void setAttendButtonOverlay(std::string icon_path, SDL_Rect logical_rect, bool visible);
    void setBlackIrisTransition(float logical_x, float logical_y, float closed_amount,
        bool visible, int circle_segments, float max_radius_scale);
    double playDoorTileAnimation(
        const std::string& map_id,
        const std::string& layer_id,
        int tile_x,
        int tile_y,
        bool reverse);
    void render(
        const camera::Gen4FollowCamera& camera,
        const camera::Vec3& player_pos,
        const SDL_Rect& player_source_rect,
        const std::string& player_activity_id,
        bool player_use_run_texture,
        bool player_draw_shadow,
        const terrain::ActorTerrainBinding& player_binding,
        int logical_w,
        int logical_h,
        int framebuffer_w,
        int framebuffer_h,
        const std::vector<rendering::CharacterBillboardDraw>& character_draws,
        const std::vector<rendering::TextureBillboardDraw>& texture_draws,
        const std::string& debug_frame_counter_label);
    OverworldBgfxRenderer::EmbeddedViewportTexture renderEmbeddedViewport(
        const camera::Gen4FollowCamera& camera,
        const camera::Vec3& player_pos,
        const SDL_Rect& player_source_rect,
        const std::string& player_activity_id,
        bool player_use_run_texture,
        bool player_draw_shadow,
        const terrain::ActorTerrainBinding& player_binding,
        int logical_w,
        int logical_h,
        int framebuffer_w,
        int framebuffer_h,
        const std::vector<rendering::CharacterBillboardDraw>& character_draws,
        const std::vector<rendering::TextureBillboardDraw>& texture_draws,
        const OverworldBgfxRenderer::EmbeddedViewportOptions& options);
    void queueScreenshot(const std::string& output_path);

private:
    struct Vertex {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        std::uint32_t abgr = 0xffffffffu;
        float u = 0.0f;
        float v = 0.0f;
        float nx = 0.0f;
        float ny = 1.0f;
        float nz = 0.0f;
    };

    struct TextureGpuResource {
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        int width = 0;
        int height = 0;
        bool has_zero_alpha = false;
        bool has_partial_alpha = false;
        bool destroy();
        bool valid() const { return bgfx::isValid(handle); }
    };

    struct CharacterGpuTextures {
        TextureGpuResource color;
        TextureGpuResource white;
        TextureGpuResource run_color;
        TextureGpuResource run_white;
        std::unordered_map<std::string, TextureGpuResource> activity_color;
        std::unordered_map<std::string, TextureGpuResource> activity_white;
    };

    struct MaterialGpuResource {
        struct ImageKeyframe {
            int frame = 0;
            TextureGpuResource texture;
        };

        TextureGpuResource texture;
        std::vector<TextureGpuResource> animation_frames;
        int animation_frame_time_ms = 0;
        float animation_timebase_hz = 0.0f;
        bool animation_step = false;
        int animation_frame_count = 0;
        int animation_image_frame_count = 0;
        bool animation_loop = true;
        bool water_animation = false;
        bool shoreline_animation = false;
        bool shoreline_seam_cover = false;
        int shoreline_cycle_frame_count = 1;
        std::vector<std::array<float, 2>> animation_uv_offsets;
        std::vector<ImageKeyframe> animation_image_keyframes;
        std::uint64_t sampler_flags = samplerFlags();
        float uv_wrap_period[2] = {1.0f, 1.0f};
        bool world_uv = false;
        float u_per_tile[2] = {0.0f, 0.0f};
        float v_per_tile[2] = {0.0f, 0.0f};
        MaterialClass material_class = MaterialClass::Opaque;
        float base_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float alpha_cutoff = 0.5f;
        bool depth_prepass = false;
        int render_order = 0;
        int source_material_id = -1;
    };

    struct MaterialRange {
        struct VertexClip {
            std::string name;
            int frame_time_ms = 100;
            std::vector<std::vector<Vertex>> frames;
        };

        std::uint32_t start_index = 0;
        std::uint32_t index_count = 0;
        int material = -1;
        MaterialClass material_class = MaterialClass::Opaque;
        bool depth_prepass = false;
        int render_order = 0;
        bool trigger_phase = false;
        bool trigger_active = false;
        bool trigger_reverse = false;
        std::int64_t trigger_started_ms = 0;
        std::string trigger_layer_id;
        int trigger_tile_x = 0;
        int trigger_tile_y = 0;
        std::uint32_t vertex_start = 0;
        std::uint32_t vertex_count = 0;
        std::string trigger_clip_name;
        std::string trigger_open_clip;
        std::string trigger_close_clip;
        bool trigger_close_reverses = true;
        std::vector<VertexClip> vertex_clips;
    };

    struct MeshGpuResource {
        bgfx::VertexBufferHandle vbh = BGFX_INVALID_HANDLE;
        bgfx::DynamicVertexBufferHandle dynamic_vbh = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle ibh = BGFX_INVALID_HANDLE;
        std::vector<MaterialRange> ranges;
        std::vector<MaterialGpuResource> materials;
        std::vector<Vertex> source_vertices;
        std::vector<Vertex> animated_vertices;
        bool owns_material_textures = true;
        bool destroy();
        bool valid() const { return (bgfx::isValid(vbh) || bgfx::isValid(dynamic_vbh)) && bgfx::isValid(ibh); }
    };

    struct ModelGpuResource {
        MeshGpuResource mesh;
        std::string placement_id;
        bool aquarium_tank_lit = false;
        float model_matrix[16]{};
        data::GlbMesh animation_mesh;
        std::vector<data::GlbVertex> source_vertices;
        std::vector<Vertex> animated_vertices;
    };

    struct StaticChunkGpuResource {
        SceneConfig scene;
        float origin_x = 0.0f;
        float origin_y = 0.0f;
        float origin_z = 0.0f;
        MeshGpuResource terrain_flat_top_mesh;
        MeshGpuResource terrain_slope_top_mesh;
        MeshGpuResource terrain_wall_mesh;
        MeshGpuResource tile_layer_mesh;
        std::vector<ModelGpuResource> models;
        float world_matrix[16]{};

        void destroy() {
            terrain_flat_top_mesh.destroy();
            terrain_slope_top_mesh.destroy();
            terrain_wall_mesh.destroy();
            tile_layer_mesh.destroy();
            for (ModelGpuResource& model : models) {
                model.mesh.destroy();
            }
            models.clear();
        }
    };

    struct PixelWorldTarget {
        bgfx::FrameBufferHandle frame_buffer = BGFX_INVALID_HANDLE;
        int width = 0;
        int height = 0;
        bool destroy();
        bool valid() const { return bgfx::isValid(frame_buffer); }
    };

    struct RenderOptions {
        bool embedded_viewport = false;
        bool override_animation_clock = false;
        bool animations_enabled = true;
        double animation_time_seconds = 0.0;
    };

    std::string project_root_;
    SceneConfig scene_;
    std::vector<InteriorFloorCutoutConfig> authored_floor_cutouts_;
    std::optional<std::vector<InteriorFloorCutoutConfig>> staged_previous_floor_cutouts_;
    CharacterSpriteDefinition character_;
    BgfxBackend backend_;
    bool initialized_ = false;
    std::string last_error_;

    bgfx::VertexLayout layout_{};
    bgfx::ProgramHandle world_program_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle wall_clip_program_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle billboard_program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle tex_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle tint_cutoff_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle color_adjust_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle texture_blur_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uv_offset_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_dir_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_params_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle camera_clip_uniform_ = BGFX_INVALID_HANDLE;
    TextureGpuResource white_texture_;
    TextureGpuResource shadow_texture_;
    TextureGpuResource textbox_texture_;
    TextureGpuResource textbox_text_texture_;
    TextureGpuResource attend_button_texture_;
    dialogue::OverworldTextboxConfig textbox_config_{};
    std::string textbox_texture_path_;
    std::string textbox_text_;
    std::string cached_textbox_text_;
    int cached_textbox_wrap_width_ = 0;
    FontHandle textbox_font_;
    std::string attend_button_texture_path_;
    SDL_Rect attend_button_logical_rect_{};
    bool attend_button_visible_ = false;
    float iris_logical_x_ = 0.0f;
    float iris_logical_y_ = 0.0f;
    float iris_closed_amount_ = 0.0f;
    float iris_max_radius_scale_ = 1.15f;
    int iris_segments_ = 64;
    bool iris_visible_ = false;
    bool textbox_overlay_visible_ = false;
    bool textbox_load_warned_ = false;
    mutable std::unordered_map<std::string, CharacterGpuTextures> character_textures_;
    mutable std::unordered_map<std::string, TextureGpuResource> effect_textures_;
    MeshGpuResource terrain_flat_top_mesh_;
    MeshGpuResource terrain_slope_top_mesh_;
    MeshGpuResource terrain_wall_mesh_;
    MeshGpuResource tile_layer_mesh_;
    std::vector<ModelGpuResource> models_;
    std::optional<data::RtpksTilePackage> tile_package_;
    std::vector<OverworldBgfxRenderer::StaticMapChunk> pending_static_chunks_;
    std::vector<StaticChunkGpuResource> static_chunks_;
    aquarium::rendering::AquariumPokemonBgfxRenderer aquarium_pokemon_renderer_;
    aquarium::rendering::AquariumConstructionBgfxRenderer aquarium_construction_renderer_;
    aquarium::rendering::PlayerAquariumBgfxRenderer player_aquarium_renderer_;
    bool player_visible_ = true;
    float interior_wall_clip_[4]{};
    std::vector<aquarium::AquariumPokemonActor> aquarium_pokemon_actors_;
    std::vector<aquarium::AquariumTankRuntime> aquarium_tank_lights_;
    aquarium::AquariumTankLightingConfig aquarium_tank_lighting_{};
    PixelWorldTarget pixel_world_target_;
    bool override_animation_clock_ = false;
    bool animations_enabled_ = true;
    double animation_time_seconds_ = 0.0;

    bool createPrograms();
    bool loadTilePackage();
    bool buildTerrain();
    bool stagePlayerAquariumFloorCutouts(
        const std::vector<aquarium::construction::PlayerTankRuntime>& tanks,
        std::string* error);
    void discardStagedPlayerAquariumFloorCutouts();
    bool buildTileLayers();
    bool buildModels();
    void updateModelAnimation(ModelGpuResource& model) const;
    void updateDoorTileAnimations(MeshGpuResource& mesh) const;
    bool buildStaticMapChunks();
    bool buildPlayerTexture();
    bool buildShadowTexture();
    void destroyPrograms();

    const CharacterGpuTextures& texturesForCharacter(const CharacterSpriteDefinition& character) const;
    const TextureGpuResource& textureForKey(
        const std::string& cache_key,
        const std::vector<std::uint8_t>& png_bytes,
        const std::string& fallback_path,
        const char* debug_name) const;

    TextureGpuResource createTextureFromRgba(
        const std::uint8_t* pixels,
        int width,
        int height,
        const char* debug_name) const;
    TextureGpuResource decodeImageBytes(
        const std::vector<std::uint8_t>& bytes,
        const std::string& fallback_path,
        const char* debug_name,
        bool flip_vertically = false) const;
    bool ensurePixelWorldTarget(int width, int height);
    bool ensureTextboxTexture();
    bool ensureTextboxTextTexture(int wrap_width);
    bool ensureAttendButtonTexture();
    OverworldBgfxRenderer::EmbeddedViewportTexture renderInternal(
        const camera::Gen4FollowCamera& camera,
        const camera::Vec3& player_pos,
        const SDL_Rect& player_source_rect,
        const std::string& player_activity_id,
        bool player_use_run_texture,
        bool player_draw_shadow,
        const terrain::ActorTerrainBinding& player_binding,
        int logical_w,
        int logical_h,
        int framebuffer_w,
        int framebuffer_h,
        const std::vector<rendering::CharacterBillboardDraw>& character_draws,
        const std::vector<rendering::TextureBillboardDraw>& texture_draws,
        const std::string& debug_frame_counter_label,
        const RenderOptions& options);

    void submitMesh(
        const MeshGpuResource& mesh,
        const float* model_matrix,
        bgfx::ProgramHandle program,
        MaterialClass pass,
        float alpha_cutoff,
        std::uint64_t state,
        bgfx::ViewId view_id = 0,
        bool ordered_materials = false,
        const float* camera_clip = nullptr,
        bool use_tank_lighting = false) const;
    void submitAquariumTankLightSpills(bgfx::ViewId view_id) const;
    void submitAlphaDepthPrepass(
        const MeshGpuResource& mesh,
        const float* model_matrix,
        bgfx::ProgramHandle program,
        MaterialClass pass,
        bgfx::ViewId view_id) const;
    void submitPixelWorldToBackbuffer(int framebuffer_w, int framebuffer_h, int source_w, int source_h) const;
    void submitTextboxOverlay(
        int framebuffer_w,
        int framebuffer_h,
        const SDL_Rect& viewport_dst,
        int base_viewport_w,
        int base_viewport_h) const;
    void submitAttendButtonOverlay(int framebuffer_w, int framebuffer_h, int logical_w, int logical_h) const;
    void submitBlackIrisTransition(int framebuffer_w, int framebuffer_h, int logical_w, int logical_h) const;
    void submitOverlaySlice(
        bgfx::ViewId view_id,
        bgfx::TextureHandle texture,
        int texture_w,
        int texture_h,
        const SDL_Rect& src,
        const SDL_Rect& dst) const;
    void submitProjectedCharacterShadows(
        const camera::Gen4FollowCamera& camera,
        const std::vector<rendering::CharacterBillboardDraw>& characters) const;
    void appendProjectedShadowForScene(
        const SceneConfig& scene,
        float origin_x,
        float origin_y,
        float origin_z,
        const rendering::BillboardPlacement& placement,
        const camera::Vec3& shadow_right,
        const camera::Vec3& shadow_forward,
        const camera::Vec3& view_bias,
        float half_w,
        float half_h,
        std::vector<Vertex>& vertices,
        std::vector<std::uint16_t>& indices) const;
    void refreshBillboardDrawer();
    std::optional<BillboardBgfxDrawer> billboard_drawer_;
};

bool OverworldBgfxRenderer::Impl::TextureGpuResource::destroy() {
    if (bgfx::isValid(handle)) {
        bgfx::destroy(handle);
        handle = BGFX_INVALID_HANDLE;
    }
    width = 0;
    height = 0;
    has_zero_alpha = false;
    has_partial_alpha = false;
    return true;
}

bool OverworldBgfxRenderer::Impl::MeshGpuResource::destroy() {
    if (bgfx::isValid(vbh)) {
        bgfx::destroy(vbh);
        vbh = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(dynamic_vbh)) {
        bgfx::destroy(dynamic_vbh);
        dynamic_vbh = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(ibh)) {
        bgfx::destroy(ibh);
        ibh = BGFX_INVALID_HANDLE;
    }
    if (owns_material_textures) {
        for (MaterialGpuResource& material : materials) {
            material.texture.destroy();
            for (TextureGpuResource& frame : material.animation_frames) frame.destroy();
            material.animation_frames.clear();
            for (MaterialGpuResource::ImageKeyframe& keyframe : material.animation_image_keyframes) {
                keyframe.texture.destroy();
            }
            material.animation_image_keyframes.clear();
        }
    }
    ranges.clear();
    materials.clear();
    source_vertices.clear();
    animated_vertices.clear();
    owns_material_textures = true;
    return true;
}

bool OverworldBgfxRenderer::Impl::PixelWorldTarget::destroy() {
    if (bgfx::isValid(frame_buffer)) {
        bgfx::destroy(frame_buffer);
        frame_buffer = BGFX_INVALID_HANDLE;
    }
    width = 0;
    height = 0;
    return true;
}

OverworldBgfxRenderer::OverworldBgfxRenderer(
    std::string project_root,
    SceneConfig scene,
    CharacterSpriteDefinition character)
    : impl_(std::make_unique<Impl>(std::move(project_root), std::move(scene), std::move(character))) {}

OverworldBgfxRenderer::~OverworldBgfxRenderer() = default;

bool OverworldBgfxRenderer::initialize(
    SDL_Window* window,
    int width,
    int height,
    const std::string& bgfx_preference,
    void* sdl_metal_view) {
    return impl_ && impl_->initialize(window, width, height, bgfx_preference, sdl_metal_view);
}

void OverworldBgfxRenderer::shutdown() {
    if (impl_) impl_->shutdown();
}

std::string OverworldBgfxRenderer::lastError() const {
    return impl_ ? impl_->lastError() : std::string{};
}

bool OverworldBgfxRenderer::valid() const {
    return impl_ && impl_->valid();
}

void OverworldBgfxRenderer::setStaticMapChunks(std::vector<StaticMapChunk> chunks) {
    if (impl_) {
        impl_->setStaticMapChunks(std::move(chunks));
    }
}

void OverworldBgfxRenderer::setAquariumPokemonActors(
    std::vector<aquarium::AquariumPokemonActor> actors) {
    if (impl_) impl_->setAquariumPokemonActors(std::move(actors));
}

void OverworldBgfxRenderer::setAquariumTankLights(
    std::vector<aquarium::AquariumTankRuntime> tanks,
    const aquarium::AquariumTankLightingConfig& lighting) {
    if (impl_) impl_->setAquariumTankLights(std::move(tanks), lighting);
}

void OverworldBgfxRenderer::setAquariumConstructionVisual(
    aquarium::construction::AquariumConstructionVisual visual) {
    if (impl_) impl_->setAquariumConstructionVisual(std::move(visual));
}

bool OverworldBgfxRenderer::replacePlayerAquariumTanks(
    const std::vector<aquarium::construction::PlayerTankRuntime>& tanks,
    std::string* error) {
    return impl_ && impl_->replacePlayerAquariumTanks(tanks, error);
}

bool OverworldBgfxRenderer::stagePlayerAquariumTanks(
    const std::vector<aquarium::construction::PlayerTankRuntime>& tanks,
    std::string* error) {
    return impl_ && impl_->stagePlayerAquariumTanks(tanks, error);
}

bool OverworldBgfxRenderer::publishStagedPlayerAquariumTanks() {
    return impl_ && impl_->publishStagedPlayerAquariumTanks();
}

void OverworldBgfxRenderer::discardStagedPlayerAquariumTanks() {
    if (impl_) impl_->discardStagedPlayerAquariumTanks();
}

std::size_t OverworldBgfxRenderer::playerAquariumResourceCount() const {
    return impl_ ? impl_->playerAquariumResourceCount() : 0U;
}

void OverworldBgfxRenderer::setSceneLighting(
    float brightness, const std::array<float, 3>& tint) {
    if (impl_) impl_->setSceneLighting(brightness, tint);
}

void OverworldBgfxRenderer::setPlayerVisible(bool visible) {
    if (impl_) impl_->setPlayerVisible(visible);
}

void OverworldBgfxRenderer::setInteriorWallCameraClip(
    camera::Vec3 center,
    float radius_world) {
    if (impl_) impl_->setInteriorWallCameraClip(center, radius_world);
}

void OverworldBgfxRenderer::setTextboxOverlay(
    dialogue::OverworldTextboxConfig config, bool visible, std::string text) {
    if (impl_) {
        impl_->setTextboxOverlay(std::move(config), visible, std::move(text));
    }
}

void OverworldBgfxRenderer::setAttendButtonOverlay(
    std::string icon_path, SDL_Rect logical_rect, bool visible) {
    if (impl_) impl_->setAttendButtonOverlay(std::move(icon_path), logical_rect, visible);
}

void OverworldBgfxRenderer::setBlackIrisTransition(
    float logical_x, float logical_y, float closed_amount,
    bool visible, int circle_segments, float max_radius_scale) {
    if (impl_) impl_->setBlackIrisTransition(logical_x, logical_y, closed_amount,
        visible, circle_segments, max_radius_scale);
}

double OverworldBgfxRenderer::playDoorTileAnimation(
    const std::string& map_id,
    const std::string& layer_id,
    int tile_x,
    int tile_y,
    bool reverse) {
    return impl_ ? impl_->playDoorTileAnimation(map_id, layer_id, tile_x, tile_y, reverse) : 0.0;
}

void OverworldBgfxRenderer::render(
    const camera::Gen4FollowCamera& camera,
    const camera::Vec3& player_pos,
    const SDL_Rect& player_source_rect,
    const std::string& player_activity_id,
    bool player_use_run_texture,
    bool player_draw_shadow,
    const terrain::ActorTerrainBinding& player_binding,
    int logical_w,
    int logical_h,
    int framebuffer_w,
    int framebuffer_h,
    const std::vector<rendering::CharacterBillboardDraw>& character_draws,
    const std::vector<rendering::TextureBillboardDraw>& texture_draws,
    const std::string& debug_frame_counter_label) {
    if (impl_) {
        impl_->render(
            camera,
            player_pos,
            player_source_rect,
            player_activity_id,
            player_use_run_texture,
            player_draw_shadow,
            player_binding,
            logical_w,
            logical_h,
            framebuffer_w,
            framebuffer_h,
            character_draws,
            texture_draws,
            debug_frame_counter_label);
    }
}

OverworldBgfxRenderer::EmbeddedViewportTexture OverworldBgfxRenderer::renderEmbeddedViewport(
    const camera::Gen4FollowCamera& camera,
    const camera::Vec3& player_pos,
    const SDL_Rect& player_source_rect,
    const std::string& player_activity_id,
    bool player_use_run_texture,
    bool player_draw_shadow,
    const terrain::ActorTerrainBinding& player_binding,
    int logical_w,
    int logical_h,
    int framebuffer_w,
    int framebuffer_h,
    const std::vector<rendering::CharacterBillboardDraw>& character_draws,
    const std::vector<rendering::TextureBillboardDraw>& texture_draws,
    const EmbeddedViewportOptions& options) {
    if (!impl_) return {};
    return impl_->renderEmbeddedViewport(
        camera,
        player_pos,
        player_source_rect,
        player_activity_id,
        player_use_run_texture,
        player_draw_shadow,
        player_binding,
        logical_w,
        logical_h,
        framebuffer_w,
        framebuffer_h,
        character_draws,
        texture_draws,
        options);
}

void OverworldBgfxRenderer::queueScreenshot(const std::string& output_path) {
    if (impl_) {
        impl_->queueScreenshot(output_path);
    }
}

void OverworldBgfxRenderer::Impl::queueScreenshot(const std::string& output_path) {
    backend_.queueScreenshot(output_path);
}

void OverworldBgfxRenderer::Impl::setStaticMapChunks(std::vector<OverworldBgfxRenderer::StaticMapChunk> chunks) {
    pending_static_chunks_ = std::move(chunks);
}

void OverworldBgfxRenderer::Impl::setAquariumPokemonActors(
    std::vector<aquarium::AquariumPokemonActor> actors) {
    aquarium_pokemon_actors_ = std::move(actors);
    if (initialized_) aquarium_pokemon_renderer_.setActors(aquarium_pokemon_actors_);
}

void OverworldBgfxRenderer::Impl::setAquariumTankLights(
    std::vector<aquarium::AquariumTankRuntime> tanks,
    const aquarium::AquariumTankLightingConfig& lighting) {
    aquarium_tank_lights_ = std::move(tanks);
    aquarium_tank_lighting_ = lighting;
    player_aquarium_renderer_.setLighting(
        lighting.enabled ? lighting.brightness : scene_.lighting_brightness,
        lighting.enabled
            ? lighting.tint
            : std::array<float, 3>{
                scene_.lighting_tint_r, scene_.lighting_tint_g, scene_.lighting_tint_b});

    std::unordered_set<std::string> tank_ids;
    for (const auto& tank : aquarium_tank_lights_) tank_ids.insert(tank.placement_id);
    for (auto& model : models_) {
        model.aquarium_tank_lit = tank_ids.contains(model.placement_id);
    }
}

void OverworldBgfxRenderer::Impl::setAquariumConstructionVisual(
    aquarium::construction::AquariumConstructionVisual visual) {
    aquarium_construction_renderer_.setVisual(std::move(visual));
}

bool OverworldBgfxRenderer::Impl::replacePlayerAquariumTanks(
    const std::vector<aquarium::construction::PlayerTankRuntime>& tanks,
    std::string* error) {
    if (!initialized_) {
        scene_.interior.floor_cutouts = aquarium::rendering::playerAquariumFloorCutouts(
            authored_floor_cutouts_, tanks);
        return player_aquarium_renderer_.replaceTanks(tanks, error);
    }
    if (!stagePlayerAquariumTanks(tanks, error)) return false;
    return publishStagedPlayerAquariumTanks();
}

bool OverworldBgfxRenderer::Impl::stagePlayerAquariumTanks(
    const std::vector<aquarium::construction::PlayerTankRuntime>& tanks,
    std::string* error) {
    discardStagedPlayerAquariumFloorCutouts();
    if (!player_aquarium_renderer_.stageTanks(tanks, error)) return false;
    if (!stagePlayerAquariumFloorCutouts(tanks, error)) {
        player_aquarium_renderer_.discardStagedTanks();
        return false;
    }
    return true;
}

bool OverworldBgfxRenderer::Impl::publishStagedPlayerAquariumTanks() {
    if (!player_aquarium_renderer_.publishStagedTanks()) {
        discardStagedPlayerAquariumFloorCutouts();
        return false;
    }
    staged_previous_floor_cutouts_.reset();
    return true;
}

void OverworldBgfxRenderer::Impl::discardStagedPlayerAquariumTanks() {
    player_aquarium_renderer_.discardStagedTanks();
    discardStagedPlayerAquariumFloorCutouts();
}

std::size_t OverworldBgfxRenderer::Impl::playerAquariumResourceCount() const {
    return player_aquarium_renderer_.resourceCount();
}

bool OverworldBgfxRenderer::Impl::stagePlayerAquariumFloorCutouts(
    const std::vector<aquarium::construction::PlayerTankRuntime>& tanks,
    std::string* error) {
    staged_previous_floor_cutouts_ = scene_.interior.floor_cutouts;
    scene_.interior.floor_cutouts = aquarium::rendering::playerAquariumFloorCutouts(
        authored_floor_cutouts_, tanks);
    if (buildTerrain()) return true;
    scene_.interior.floor_cutouts = *staged_previous_floor_cutouts_;
    staged_previous_floor_cutouts_.reset();
    const bool restored = buildTerrain();
    if (error) {
        *error = restored
            ? "Could not upload aquarium floor cutout geometry"
            : "Could not restore aquarium room floor after cutout upload failure";
    }
    return false;
}

void OverworldBgfxRenderer::Impl::discardStagedPlayerAquariumFloorCutouts() {
    if (!staged_previous_floor_cutouts_) return;
    scene_.interior.floor_cutouts = std::move(*staged_previous_floor_cutouts_);
    staged_previous_floor_cutouts_.reset();
    if (initialized_ && !buildTerrain()) {
        std::cerr << "[AquariumConstruction] event=floor_cutout_restore_failed\n";
    }
}

void OverworldBgfxRenderer::Impl::setTextboxOverlay(
    dialogue::OverworldTextboxConfig config, bool visible, std::string text) {
    if (config.sprite_sheet_path != textbox_texture_path_) {
        textbox_texture_.destroy();
        textbox_texture_path_.clear();
        textbox_load_warned_ = false;
    }
    if (config.text_font_path != textbox_config_.text_font_path ||
        config.text_font_size_px != textbox_config_.text_font_size_px) {
        textbox_font_.reset();
        textbox_text_texture_.destroy();
        cached_textbox_text_.clear();
        cached_textbox_wrap_width_ = 0;
    }
    textbox_config_ = std::move(config);
    textbox_overlay_visible_ = visible;
    textbox_text_ = std::move(text);
}

void OverworldBgfxRenderer::Impl::setAttendButtonOverlay(
    std::string icon_path, SDL_Rect logical_rect, bool visible) {
    if (icon_path != attend_button_texture_path_) {
        attend_button_texture_.destroy();
        attend_button_texture_path_ = std::move(icon_path);
    }
    attend_button_logical_rect_ = logical_rect;
    attend_button_visible_ = visible;
}

void OverworldBgfxRenderer::Impl::setBlackIrisTransition(
    float logical_x, float logical_y, float closed_amount,
    bool visible, int circle_segments, float max_radius_scale) {
    iris_logical_x_ = logical_x;
    iris_logical_y_ = logical_y;
    iris_closed_amount_ = std::clamp(closed_amount, 0.0f, 1.0f);
    iris_visible_ = visible;
    iris_segments_ = std::clamp(circle_segments, 16, 192);
    iris_max_radius_scale_ = std::max(1.0f, max_radius_scale);
}

double OverworldBgfxRenderer::Impl::playDoorTileAnimation(
    const std::string& map_id,
    const std::string& layer_id,
    int tile_x,
    int tile_y,
    bool reverse) {
    SceneConfig* target_scene = nullptr;
    MeshGpuResource* target_mesh = nullptr;
    if (map_id.empty() || map_id == scene_.id) {
        target_scene = &scene_;
        target_mesh = &tile_layer_mesh_;
    } else {
        const auto chunk = std::find_if(static_chunks_.begin(), static_chunks_.end(), [&](const StaticChunkGpuResource& item) {
            return item.scene.id == map_id;
        });
        if (chunk != static_chunks_.end()) {
            target_scene = &chunk->scene;
            target_mesh = &chunk->tile_layer_mesh;
        }
    }
    if (!target_scene || !target_mesh) return 0.0;

    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    double duration_seconds = 0.0;
    for (MaterialRange& range : target_mesh->ranges) {
        if (!range.trigger_phase || range.trigger_layer_id != layer_id ||
            range.trigger_tile_x != tile_x || range.trigger_tile_y != tile_y ||
            range.material < 0 || range.material >= static_cast<int>(target_mesh->materials.size())) continue;
        range.trigger_active = true;
        range.trigger_clip_name = reverse && !range.trigger_close_clip.empty()
            ? range.trigger_close_clip
            : range.trigger_open_clip;
        range.trigger_started_ms = now_ms;
        const auto vertex_clip = std::find_if(
            range.vertex_clips.begin(), range.vertex_clips.end(), [&](const MaterialRange::VertexClip& clip) {
                return clip.name == range.trigger_clip_name;
            });
        range.trigger_reverse = reverse && (
            range.trigger_close_reverses || vertex_clip == range.vertex_clips.end());
        if (vertex_clip != range.vertex_clips.end()) {
            duration_seconds = std::max(
                duration_seconds,
                static_cast<double>(vertex_clip->frames.size() * std::max(16, vertex_clip->frame_time_ms)) / 1000.0);
        }
        const MaterialGpuResource& material = target_mesh->materials[static_cast<std::size_t>(range.material)];
        const int frames = std::max({
            1,
            material.animation_frame_count,
            static_cast<int>(material.animation_frames.size())});
        const int frame_ms = material.animation_frame_time_ms > 0 ? material.animation_frame_time_ms : 100;
        duration_seconds = std::max(duration_seconds, static_cast<double>(frames * frame_ms) / 1000.0);
    }
    return duration_seconds;
}

void OverworldBgfxRenderer::Impl::updateDoorTileAnimations(MeshGpuResource& mesh) const {
    if (!bgfx::isValid(mesh.dynamic_vbh) || mesh.source_vertices.empty()) return;
    mesh.animated_vertices = mesh.source_vertices;
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    for (const MaterialRange& range : mesh.ranges) {
        if (!range.trigger_phase || range.vertex_clips.empty()) continue;
        auto selected = std::find_if(
            range.vertex_clips.begin(), range.vertex_clips.end(), [&](const MaterialRange::VertexClip& clip) {
                return clip.name == range.trigger_clip_name;
            });
        if (selected == range.vertex_clips.end()) selected = range.vertex_clips.begin();
        if (selected->frames.empty()) continue;
        const std::int64_t elapsed = override_animation_clock_
            ? (animations_enabled_ && range.trigger_active
                ? static_cast<std::int64_t>(std::max(0.0, animation_time_seconds_) * 1000.0)
                : 0)
            : (range.trigger_active
                ? std::max<std::int64_t>(0, now_ms - range.trigger_started_ms)
                : 0);
        std::size_t frame_index = std::min<std::size_t>(
            selected->frames.size() - 1,
            static_cast<std::size_t>(elapsed / std::max(16, selected->frame_time_ms)));
        if (range.trigger_reverse) frame_index = selected->frames.size() - 1 - frame_index;
        const std::vector<Vertex>& frame = selected->frames[frame_index];
        const std::size_t count = std::min<std::size_t>(
            {frame.size(), range.vertex_count, mesh.animated_vertices.size() - std::min<std::size_t>(range.vertex_start, mesh.animated_vertices.size())});
        for (std::size_t index = 0; index < count; ++index) {
            Vertex& target = mesh.animated_vertices[range.vertex_start + index];
            target.x = frame[index].x;
            target.y = frame[index].y;
            target.z = frame[index].z;
        }
    }
    bgfx::update(mesh.dynamic_vbh, 0, bgfx::copy(
        mesh.animated_vertices.data(),
        static_cast<std::uint32_t>(mesh.animated_vertices.size() * sizeof(Vertex))));
}

bool OverworldBgfxRenderer::Impl::initialize(
    SDL_Window* window,
    int width,
    int height,
    const std::string& bgfx_preference,
    void* sdl_metal_view) {
    const auto initialize_started_at = std::chrono::steady_clock::now();
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
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .end();

    tex_uniform_ = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
    tint_cutoff_uniform_ = bgfx::createUniform("u_tintCutoff", bgfx::UniformType::Vec4);
    color_adjust_uniform_ = bgfx::createUniform("u_colorAdjust", bgfx::UniformType::Vec4);
    texture_blur_uniform_ = bgfx::createUniform("u_textureBlur", bgfx::UniformType::Vec4);
    uv_offset_uniform_ = bgfx::createUniform("u_uvOffset", bgfx::UniformType::Vec4);
    light_dir_uniform_ = bgfx::createUniform("u_lightDir", bgfx::UniformType::Vec4);
    light_params_uniform_ = bgfx::createUniform("u_lightParams", bgfx::UniformType::Vec4);
    camera_clip_uniform_ = bgfx::createUniform("u_cameraClip", bgfx::UniformType::Vec4);

    std::uint8_t white[4] = {255, 255, 255, 255};
    white_texture_ = createTextureFromRgba(white, 1, 1, "world3d-white");
    if (!white_texture_.valid()) {
        last_error_ = "Could not create white texture";
        shutdown();
        return false;
    }

    if (!createPrograms() || !loadTilePackage() || !buildTerrain() || !buildTileLayers() ||
        !buildModels() || !buildStaticMapChunks() || !buildPlayerTexture() || !buildShadowTexture()) {
        shutdown();
        return false;
    }

    aquarium_pokemon_renderer_.initialize(
        layout_, world_program_, tex_uniform_, tint_cutoff_uniform_, color_adjust_uniform_,
        texture_blur_uniform_, uv_offset_uniform_, light_dir_uniform_, light_params_uniform_);
    aquarium_pokemon_renderer_.setActors(aquarium_pokemon_actors_);
    player_aquarium_renderer_.initialize(
        layout_, world_program_, white_texture_.handle, tex_uniform_, tint_cutoff_uniform_,
        color_adjust_uniform_, texture_blur_uniform_, uv_offset_uniform_,
        light_dir_uniform_, light_params_uniform_);
    player_aquarium_renderer_.setLighting(
        aquarium_tank_lighting_.enabled
            ? aquarium_tank_lighting_.brightness
            : scene_.lighting_brightness,
        aquarium_tank_lighting_.enabled
            ? aquarium_tank_lighting_.tint
            : std::array<float, 3>{
                scene_.lighting_tint_r, scene_.lighting_tint_g, scene_.lighting_tint_b});
    aquarium_construction_renderer_.initialize(
        layout_, world_program_, white_texture_.handle, tex_uniform_, tint_cutoff_uniform_,
        color_adjust_uniform_, texture_blur_uniform_, uv_offset_uniform_,
        light_dir_uniform_, light_params_uniform_);

    refreshBillboardDrawer();
    initialized_ = true;
    std::cerr << "[OverworldBgfx] Ready terrain="
              << (terrain_flat_top_mesh_.valid() || terrain_slope_top_mesh_.valid() || terrain_wall_mesh_.valid() ? "yes" : "no")
              << " tiles=" << (tile_layer_mesh_.valid() ? "yes" : "no")
              << " models=" << models_.size()
              << " staticChunks=" << static_chunks_.size()
              << " playerTexture=" << character_textures_[character_.texture_path].color.width << "x"
              << character_textures_[character_.texture_path].color.height
              << " initMs=" << std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - initialize_started_at).count()
              << '\n';
    return initialized_;
}

void OverworldBgfxRenderer::Impl::shutdown() {
    aquarium_construction_renderer_.shutdown();
    player_aquarium_renderer_.shutdown();
    aquarium_pokemon_renderer_.shutdown();
    pixel_world_target_.destroy();
    terrain_flat_top_mesh_.destroy();
    terrain_slope_top_mesh_.destroy();
    terrain_wall_mesh_.destroy();
    tile_layer_mesh_.destroy();
    for (ModelGpuResource& model : models_) {
        model.mesh.destroy();
    }
    models_.clear();
    for (StaticChunkGpuResource& chunk : static_chunks_) {
        chunk.destroy();
    }
    static_chunks_.clear();
    tile_package_.reset();
    shadow_texture_.destroy();
    for (auto& entry : character_textures_) {
        entry.second.color.destroy();
        entry.second.white.destroy();
        entry.second.run_color.destroy();
        entry.second.run_white.destroy();
        for (auto& activity : entry.second.activity_color) {
            activity.second.destroy();
        }
        for (auto& activity : entry.second.activity_white) {
            activity.second.destroy();
        }
    }
    character_textures_.clear();
    for (auto& entry : effect_textures_) {
        entry.second.destroy();
    }
    effect_textures_.clear();
    textbox_texture_.destroy();
    textbox_text_texture_.destroy();
    attend_button_texture_.destroy();
    textbox_texture_path_.clear();
    textbox_font_.reset();
    cached_textbox_text_.clear();
    cached_textbox_wrap_width_ = 0;
    attend_button_texture_path_.clear();
    white_texture_.destroy();
    destroyPrograms();
    if (bgfx::isValid(tex_uniform_)) {
        bgfx::destroy(tex_uniform_);
        tex_uniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(tint_cutoff_uniform_)) {
        bgfx::destroy(tint_cutoff_uniform_);
        tint_cutoff_uniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(color_adjust_uniform_)) {
        bgfx::destroy(color_adjust_uniform_);
        color_adjust_uniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(texture_blur_uniform_)) {
        bgfx::destroy(texture_blur_uniform_);
        texture_blur_uniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(uv_offset_uniform_)) {
        bgfx::destroy(uv_offset_uniform_);
        uv_offset_uniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(light_dir_uniform_)) {
        bgfx::destroy(light_dir_uniform_);
        light_dir_uniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(light_params_uniform_)) {
        bgfx::destroy(light_params_uniform_);
        light_params_uniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(camera_clip_uniform_)) {
        bgfx::destroy(camera_clip_uniform_);
        camera_clip_uniform_ = BGFX_INVALID_HANDLE;
    }
    backend_.shutdown();
    initialized_ = false;
}

bool OverworldBgfxRenderer::Impl::createPrograms() {
    const std::filesystem::path shader_root = backend_.shaderDirectory();
    const std::string shader_subdir = backend_.shaderSubdirectory();
    bgfx::ShaderHandle vs_world = loadShader(shader_root, shader_subdir, "vs_world");
    bgfx::ShaderHandle vs_wall = loadShader(shader_root, shader_subdir, "vs_world");
    bgfx::ShaderHandle vs_billboard = loadShader(shader_root, shader_subdir, "vs_billboard");
    bgfx::ShaderHandle fs = loadShader(shader_root, shader_subdir, "fs_textured_cutout");
    bgfx::ShaderHandle fs_wall = loadShader(shader_root, shader_subdir, "fs_textured_wall_clip");
    bgfx::ShaderHandle fs_billboard = loadShader(shader_root, shader_subdir, "fs_textured_cutout");
    if (!bgfx::isValid(vs_world) || !bgfx::isValid(vs_wall) ||
        !bgfx::isValid(vs_billboard) || !bgfx::isValid(fs) ||
        !bgfx::isValid(fs_wall) || !bgfx::isValid(fs_billboard)) {
        last_error_ = "Could not load bgfx shader binaries from " + shader_root.string();
        if (bgfx::isValid(vs_world)) bgfx::destroy(vs_world);
        if (bgfx::isValid(vs_wall)) bgfx::destroy(vs_wall);
        if (bgfx::isValid(vs_billboard)) bgfx::destroy(vs_billboard);
        if (bgfx::isValid(fs)) bgfx::destroy(fs);
        if (bgfx::isValid(fs_wall)) bgfx::destroy(fs_wall);
        if (bgfx::isValid(fs_billboard)) bgfx::destroy(fs_billboard);
        return false;
    }
    world_program_ = bgfx::createProgram(vs_world, fs, true);
    wall_clip_program_ = bgfx::createProgram(vs_wall, fs_wall, true);
    billboard_program_ = bgfx::createProgram(vs_billboard, fs_billboard, true);
    if (!bgfx::isValid(world_program_) || !bgfx::isValid(wall_clip_program_) ||
        !bgfx::isValid(billboard_program_)) {
        last_error_ = "Could not create bgfx shader programs";
        return false;
    }
    return true;
}

void OverworldBgfxRenderer::Impl::destroyPrograms() {
    if (bgfx::isValid(world_program_)) {
        bgfx::destroy(world_program_);
        world_program_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(wall_clip_program_)) {
        bgfx::destroy(wall_clip_program_);
        wall_clip_program_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(billboard_program_)) {
        bgfx::destroy(billboard_program_);
        billboard_program_ = BGFX_INVALID_HANDLE;
    }
}

OverworldBgfxRenderer::Impl::TextureGpuResource OverworldBgfxRenderer::Impl::createTextureFromRgba(
    const std::uint8_t* pixels,
    int width,
    int height,
    const char* debug_name) const {
    TextureGpuResource out;
    if (!pixels || width <= 0 || height <= 0) return out;
    const std::uint32_t size = static_cast<std::uint32_t>(width * height * 4);
    for (int i = 0; i < width * height; ++i) {
        const std::uint8_t alpha = pixels[(i * 4) + 3];
        if (alpha == 0) out.has_zero_alpha = true;
        if (alpha > 0 && alpha < 255) out.has_partial_alpha = true;
    }
    const bgfx::Memory* mem = bgfx::copy(pixels, size);
    out.handle = bgfx::createTexture2D(
        static_cast<std::uint16_t>(width),
        static_cast<std::uint16_t>(height),
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        0,
        mem);
    out.width = width;
    out.height = height;
    if (bgfx::isValid(out.handle)) {
        bgfx::setName(out.handle, debug_name);
    }
    return out;
}

OverworldBgfxRenderer::Impl::TextureGpuResource OverworldBgfxRenderer::Impl::decodeImageBytes(
    const std::vector<std::uint8_t>& bytes,
    const std::string& fallback_path,
    const char* debug_name,
    bool flip_vertically) const {
    SDL_Surface* surface = nullptr;
    if (!bytes.empty()) {
        SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
        if (rw) surface = IMG_Load_RW(rw, 1);
    }
    if (!surface && !fallback_path.empty()) {
        surface = IMG_Load(fallback_path.c_str());
    }
    if (!surface) return {};

    SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(surface);
    if (!converted) return {};
    const auto* source = static_cast<const std::uint8_t*>(converted->pixels);
    const std::size_t row_bytes = static_cast<std::size_t>(converted->w) * 4U;
    std::vector<std::uint8_t> packed;
    const bool needs_repack = converted->pitch != static_cast<int>(row_bytes) || flip_vertically;
    if (needs_repack) {
        packed.resize(row_bytes * static_cast<std::size_t>(converted->h));
        for (int y = 0; y < converted->h; ++y) {
            const int source_y = flip_vertically ? converted->h - 1 - y : y;
            std::memcpy(
                packed.data() + static_cast<std::size_t>(y) * row_bytes,
                source + static_cast<std::size_t>(source_y) * static_cast<std::size_t>(converted->pitch),
                row_bytes);
        }
        source = packed.data();
    }
    TextureGpuResource out = createTextureFromRgba(source, converted->w, converted->h, debug_name);
    SDL_FreeSurface(converted);
    return out;
}

bool OverworldBgfxRenderer::Impl::ensurePixelWorldTarget(int width, int height) {
    width = std::max(1, width);
    height = std::max(1, height);
    if (pixel_world_target_.valid() && pixel_world_target_.width == width && pixel_world_target_.height == height) {
        return true;
    }

    pixel_world_target_.destroy();
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
        last_error_ = "Could not create pixel-perfect world render target";
        return false;
    }

    bgfx::Attachment attachments[2];
    attachments[0].init(color);
    attachments[1].init(depth);
    pixel_world_target_.frame_buffer = bgfx::createFrameBuffer(2, attachments, true);
    if (!pixel_world_target_.valid()) {
        bgfx::destroy(color);
        bgfx::destroy(depth);
        last_error_ = "Could not create pixel-perfect world framebuffer";
        return false;
    }
    pixel_world_target_.width = width;
    pixel_world_target_.height = height;
    std::cerr << "[OverworldBgfx] Pixel-perfect world target "
              << width << "x" << height << '\n';
    return true;
}

bool OverworldBgfxRenderer::Impl::ensureTextboxTexture() {
    if (textbox_texture_.valid()) {
        return true;
    }
    if (!dialogue::overworldTextboxEnabled(textbox_config_)) {
        return false;
    }

    std::filesystem::path path(textbox_config_.sprite_sheet_path);
    if (!path.is_absolute()) {
        path = std::filesystem::path(project_root_) / path;
    }
    textbox_texture_ = decodeImageBytes({}, path.string(), "overworld-textbox");
    if (!textbox_texture_.valid()) {
        if (!textbox_load_warned_) {
            std::cerr << "[OverworldBgfx] Could not load textbox sprite sheet: "
                      << path << '\n';
            textbox_load_warned_ = true;
        }
        return false;
    }
    textbox_texture_path_ = textbox_config_.sprite_sheet_path;
    return true;
}

bool OverworldBgfxRenderer::Impl::ensureTextboxTextTexture(int wrap_width) {
    wrap_width = std::max(1, wrap_width);
    if (textbox_text_.empty()) {
        textbox_text_texture_.destroy();
        cached_textbox_text_.clear();
        cached_textbox_wrap_width_ = wrap_width;
        return false;
    }
    if (textbox_text_texture_.valid() && cached_textbox_text_ == textbox_text_ &&
        cached_textbox_wrap_width_ == wrap_width) {
        return true;
    }
    if (!textbox_font_) {
        try {
            textbox_font_ = loadFont(
                textbox_config_.text_font_path,
                textbox_config_.text_font_size_px,
                project_root_);
        } catch (const std::exception& ex) {
            std::cerr << "[OverworldBgfx] Could not load textbox font: " << ex.what() << '\n';
            return false;
        }
    }

    const SDL_Color color{32, 32, 32, 255};
    SDL_Surface* rendered = TTF_RenderUTF8_Solid(
        textbox_font_.get(), textbox_text_.c_str(), color);
    if (!rendered) return false;
    SDL_Surface* rgba = SDL_ConvertSurfaceFormat(rendered, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(rendered);
    if (!rgba) return false;

    textbox_text_texture_.destroy();
    textbox_text_texture_ = createTextureFromRgba(
        static_cast<const std::uint8_t*>(rgba->pixels), rgba->w, rgba->h, "overworld-textbox-text");
    SDL_FreeSurface(rgba);
    cached_textbox_text_ = textbox_text_;
    cached_textbox_wrap_width_ = wrap_width;
    return textbox_text_texture_.valid();
}

bool OverworldBgfxRenderer::Impl::ensureAttendButtonTexture() {
    if (attend_button_texture_.valid()) return true;
    if (!attend_button_visible_ || attend_button_texture_path_.empty()) return false;
    std::filesystem::path path(attend_button_texture_path_);
    if (!path.is_absolute()) path = std::filesystem::path(project_root_) / path;
    attend_button_texture_ = decodeImageBytes({}, path.string(), "overworld-attend-button");
    return attend_button_texture_.valid();
}

bool OverworldBgfxRenderer::Impl::loadTilePackage() {
    tile_package_.reset();
    if (scene_.tile_package.path.empty() || scene_.tile_layers.layers.empty()) {
        return true;
    }
    std::vector<int> used_tile_ids;
    std::unordered_set<int> unique_tile_ids;
    for (const TileLayerConfig& layer : scene_.tile_layers.layers) {
        if (!layer.visible) continue;
        for (const auto& row : layer.cells) {
            for (const int tile_id : row) {
                if (tile_id >= 0 && unique_tile_ids.insert(tile_id).second) {
                    used_tile_ids.push_back(tile_id);
                }
            }
        }
    }
    if (used_tile_ids.empty()) return true;

    std::string error;
    data::RtpksTilePackage package = data::loadRtpksTilePackageForTiles(
        scene_.tile_package.path, used_tile_ids, &error);
    if (package.tiles.empty()) {
        std::cerr << "[OverworldBgfx] RTPKS unavailable: "
                  << (error.empty() ? scene_.tile_package.path : error)
                  << std::endl;
        return true;
    }
    tile_package_ = std::move(package);
    return true;
}

bool OverworldBgfxRenderer::Impl::buildTileLayers() {
    tile_layer_mesh_.destroy();
    if (!tile_package_ || scene_.tile_layers.layers.empty()) {
        return true;
    }

    struct Bucket {
        std::vector<Vertex> vertices;
        std::vector<std::uint32_t> indices;
        MaterialClass material_class = MaterialClass::MaskCutout;
        int material_index = 0;
        bool trigger_phase = false;
        std::string trigger_layer_id;
        int trigger_tile_x = 0;
        int trigger_tile_y = 0;
        std::string trigger_open_clip;
        std::string trigger_close_clip;
        bool trigger_close_reverses = true;
        std::vector<MaterialRange::VertexClip> vertex_clips;
    };

    // A pack can contain thousands of authored materials while a map only uses a
    // handful of tiles. Uploading the entire pack exhausts bgfx's texture handles
    // before player/model textures are created, especially when animated materials
    // contribute several frames each. Resolve the material closure of visible,
    // placed tiles first and only upload those resources.
    std::unordered_set<int> used_material_ids;
    for (const TileLayerConfig& layer : scene_.tile_layers.layers) {
        if (!layer.visible) continue;
        for (const auto& row : layer.cells) {
            for (const int tile_id : row) {
                if (tile_id < 0) continue;
                const data::RtpksTileMesh* tile = tile_package_->tileById(tile_id);
                if (!tile) continue;
                for (const data::RtpksMaterialRange& range : tile->material_ranges) {
                    used_material_ids.insert(range.material_id);
                }
            }
        }
    }

    std::unordered_map<int, int> material_slot_by_id;
    tile_layer_mesh_.materials.reserve(used_material_ids.size());
    int shoreline_cycle_frame_count = 1;
    for (const data::RtpksMaterial& src : tile_package_->materials) {
        if (used_material_ids.contains(src.material_id) && src.layer_role.rfind("shoreline", 0) == 0) {
            shoreline_cycle_frame_count = std::max(
                shoreline_cycle_frame_count,
                src.animation_frame_count);
        }
    }
    for (const data::RtpksMaterial& src : tile_package_->materials) {
        if (!used_material_ids.contains(src.material_id)) continue;
        MaterialGpuResource material;
        material.source_material_id = src.material_id;
        material.base_color[3] = std::clamp(static_cast<float>(src.alpha) / 31.0f, 0.0f, 1.0f);
        if (!src.image_bytes.empty()) {
            // Admin's THREE.TextureLoader presents RTPKS PNGs with a vertical
            // upload flip. Match that texture origin here; flipping UVs instead
            // would also reverse signed Gen 5 material-motion offsets.
            material.texture = decodeImageBytes(
                src.image_bytes,
                "",
                src.name.empty() ? "rtpks-tile" : src.name.c_str(),
                true);
        }
        material.animation_frame_time_ms = src.animation_frame_time_ms;
        material.animation_timebase_hz = src.animation_timebase_hz;
        material.animation_step = src.animation_step;
        material.animation_frame_count = src.animation_frame_count;
        material.animation_image_frame_count = src.animation_image_frame_count;
        material.animation_loop = src.animation_loop;
        material.water_animation = isWaterMaterial(src.name, src.layer_role);
        material.shoreline_animation = src.layer_role.rfind("shoreline", 0) == 0;
        material.shoreline_seam_cover =
            src.layer_role == "shoreline-crest" ||
            src.layer_role == "shoreline-underlay";
        material.shoreline_cycle_frame_count = shoreline_cycle_frame_count;
        material.animation_uv_offsets = src.animation_uv_offsets;
        material.sampler_flags = samplerFlags(src.wrap_s, src.wrap_t, src.min_filter, src.mag_filter);
        material.uv_wrap_period[0] = src.wrap_s == "clamp" ? 0.0f : src.wrap_s == "mirror" ? 2.0f : 1.0f;
        material.uv_wrap_period[1] = src.wrap_t == "clamp" ? 0.0f : src.wrap_t == "mirror" ? 2.0f : 1.0f;
        material.world_uv = src.world_uv;
        material.u_per_tile[0] = src.u_per_tile[0];
        material.u_per_tile[1] = src.u_per_tile[1];
        material.v_per_tile[0] = src.v_per_tile[0];
        material.v_per_tile[1] = src.v_per_tile[1];
        material.render_order = src.render_order;
        for (const std::vector<std::uint8_t>& frame_bytes : src.animation_frame_bytes) {
            TextureGpuResource frame = decodeImageBytes(
                frame_bytes,
                "",
                src.name.empty() ? "rtpks-animation" : src.name.c_str(),
                true);
            if (frame.valid()) material.animation_frames.push_back(std::move(frame));
        }
        for (const data::RtpksMaterialImageKeyframe& src_keyframe : src.animation_image_keyframes) {
            TextureGpuResource texture = decodeImageBytes(
                src_keyframe.image_bytes,
                "",
                src.name.empty() ? "rtpks-motion-keyframe" : src.name.c_str(),
                true);
            if (texture.valid()) {
                MaterialGpuResource::ImageKeyframe keyframe;
                keyframe.frame = src_keyframe.frame;
                keyframe.texture = std::move(texture);
                material.animation_image_keyframes.push_back(std::move(keyframe));
            }
        }
        if (material.base_color[3] < 0.999f || material.texture.has_partial_alpha) {
            material.material_class = MaterialClass::TrueBlend;
            material.alpha_cutoff = 0.0f;
            material.depth_prepass = material.base_color[3] >= 0.999f && material.texture.has_zero_alpha;
        } else if (material.texture.has_zero_alpha) {
            material.material_class = MaterialClass::MaskCutout;
            material.alpha_cutoff = 0.5f;
            material.depth_prepass = true;
        } else {
            material.material_class = MaterialClass::Opaque;
            material.alpha_cutoff = 0.0f;
        }
        const int slot = static_cast<int>(tile_layer_mesh_.materials.size());
        material_slot_by_id[src.material_id] = slot;
        tile_layer_mesh_.materials.push_back(std::move(material));
    }
    if (tile_layer_mesh_.materials.empty()) {
        tile_layer_mesh_.materials.push_back(MaterialGpuResource{});
    }

    std::vector<Bucket> buckets(tile_layer_mesh_.materials.size());
    for (std::size_t i = 0; i < buckets.size(); ++i) {
        buckets[i].material_class = tile_layer_mesh_.materials[i].material_class;
        buckets[i].material_index = static_cast<int>(i);
    }

    const float tile_size = std::max(1.0f, scene_.grid.tile_size);
    constexpr float kLayerLift = 0.004f;
    const auto read_float = [](const std::vector<float>& values, std::size_t index, float fallback) {
        return index < values.size() ? values[index] : fallback;
    };
    const auto mesh_vertical_range = [&](const data::RtpksTileMesh& mesh) {
        float min_z = std::numeric_limits<float>::max();
        float max_z = -std::numeric_limits<float>::max();
        const auto scan = [&](const std::vector<float>& positions) {
            for (std::size_t i = 2; i < positions.size(); i += 3U) {
                min_z = std::min(min_z, positions[i]);
                max_z = std::max(max_z, positions[i]);
            }
        };
        scan(mesh.triangles);
        scan(mesh.quads);
        if (min_z == std::numeric_limits<float>::max()) {
            return 0.0f;
        }
        return max_z - min_z;
    };
    const auto vertex_color = [&](const std::vector<float>& colors, std::size_t base, float alpha) {
        return packAbgr(
            read_float(colors, base + 0U, 1.0f),
            read_float(colors, base + 1U, 1.0f),
            read_float(colors, base + 2U, 1.0f),
            alpha);
    };
    const auto tile_special_at = [this](int tx, int ty) -> int {
        if (ty < 0 || ty >= static_cast<int>(scene_.terrain.specials.size())) return kSpecialFlat;
        const auto& row = scene_.terrain.specials[static_cast<std::size_t>(ty)];
        if (tx < 0 || tx >= static_cast<int>(row.size())) return kSpecialFlat;
        return static_cast<int>(row[static_cast<std::size_t>(tx)]);
    };
    const auto ramp_axis = [](int special, int& dx, int& dy) {
        dx = 0;
        dy = 0;
        if (special == kSpecialRampNorth) {
            dy = -1;
        } else if (special == kSpecialRampEast) {
            dx = 1;
        } else if (special == kSpecialRampSouth) {
            dy = 1;
        } else if (special == kSpecialRampWest) {
            dx = -1;
        }
    };
    const auto cardinal_ramp_progress = [&](int tx, int ty, float u, float v) -> std::optional<float> {
        const int special = tile_special_at(tx, ty);
        if (special < kSpecialRampNorth || special > kSpecialRampWest) return std::nullopt;

        int dx = 0;
        int dy = 0;
        ramp_axis(special, dx, dy);
        int start_x = tx;
        int start_y = ty;
        int index = 0;
        while (tile_special_at(start_x - dx, start_y - dy) == special) {
            start_x -= dx;
            start_y -= dy;
            ++index;
        }
        int count = 1;
        int end_x = start_x;
        int end_y = start_y;
        while (tile_special_at(end_x + dx, end_y + dy) == special) {
            end_x += dx;
            end_y += dy;
            ++count;
        }

        float local = 0.0f;
        if (special == kSpecialRampNorth) {
            local = 1.0f - v;
        } else if (special == kSpecialRampEast) {
            local = u;
        } else if (special == kSpecialRampSouth) {
            local = v;
        } else if (special == kSpecialRampWest) {
            local = 1.0f - u;
        }
        return std::clamp((static_cast<float>(index) + std::clamp(local, 0.0f, 1.0f)) / static_cast<float>(std::max(1, count)),
                          0.0f,
                          1.0f);
    };
    const auto make_vertex = [&](const data::RtpksTileMesh& mesh,
                                   const std::vector<float>& positions,
                                   const std::vector<float>& uvs,
                                   const std::vector<float>& colors,
                                   std::size_t vertex_index,
                                   int tile_x,
                                   int tile_y,
                                   float base_y,
                                   float layer_lift,
                                   bool conform_to_terrain,
                                   const MaterialGpuResource& material,
                                   float seam_center_x,
                                   float seam_center_y,
                                   float seam_overlap_tiles) {
        const std::size_t pi = vertex_index * 3U;
        const std::size_t ui = vertex_index * 2U;
        float local_x = read_float(positions, pi + 0U, 0.0f) + mesh.x_offset;
        float local_y = read_float(positions, pi + 1U, 0.0f) + mesh.y_offset;
        if (seam_overlap_tiles > 0.0f) {
            constexpr float kCenterEpsilon = 0.00001f;
            if (local_x < seam_center_x - kCenterEpsilon) local_x -= seam_overlap_tiles;
            else if (local_x > seam_center_x + kCenterEpsilon) local_x += seam_overlap_tiles;
            if (local_y < seam_center_y - kCenterEpsilon) local_y -= seam_overlap_tiles;
            else if (local_y > seam_center_y + kCenterEpsilon) local_y += seam_overlap_tiles;
        }
        const float local_z = read_float(positions, pi + 2U, 0.0f);
        const float world_x = static_cast<float>(tile_x) * tile_size + local_x * tile_size;
        const float world_z = static_cast<float>(tile_y) * tile_size + (static_cast<float>(mesh.height) - local_y) * tile_size;
        const int sample_tx = tile_x + std::clamp(
            static_cast<int>(std::floor(local_x)),
            0,
            std::max(0, mesh.width - 1));
        const int sample_ty = tile_y + std::clamp(
            static_cast<int>(std::floor(static_cast<float>(mesh.height) - local_y)),
            0,
            std::max(0, mesh.height - 1));
        const float y = conform_to_terrain
            ? terrain::heightAtWorldPositionOnTile(scene_, world_x, world_z, sample_tx, sample_ty, true) + local_z * tile_size + layer_lift
            : base_y + local_z * tile_size + layer_lift;
        std::uint32_t color = vertex_color(colors, pi, material.base_color[3]);
        if (conform_to_terrain) {
            const float u = std::clamp((world_x - static_cast<float>(sample_tx) * tile_size) / tile_size, 0.0f, 1.0f);
            const float v = std::clamp((world_z - static_cast<float>(sample_ty) * tile_size) / tile_size, 0.0f, 1.0f);
            if (scene_.terrain.textured_ramp_readability_enabled) {
                if (const std::optional<float> progress = cardinal_ramp_progress(sample_tx, sample_ty, u, v)) {
                    color = ramp_terrain_render::rampProgressColor(
                        color,
                        *progress,
                        scene_.terrain.textured_ramp_low_shade,
                        scene_.terrain.textured_ramp_high_shade,
                        scene_.terrain.textured_ramp_band_count,
                        scene_.terrain.textured_ramp_band_strength,
                        scene_.terrain.textured_ramp_band_softness);
                }
            }
        }
        const float source_u = read_float(uvs, ui + 0U, 0.0f) + (material.world_uv
            ? static_cast<float>(tile_x) * material.u_per_tile[0] + static_cast<float>(tile_y) * material.u_per_tile[1]
            : 0.0f);
        const float source_v = read_float(uvs, ui + 1U, 0.0f) + (material.world_uv
            ? static_cast<float>(tile_x) * material.v_per_tile[0] + static_cast<float>(tile_y) * material.v_per_tile[1]
            : 0.0f);
        return Vertex{
            world_x,
            y,
            world_z,
            color,
            source_u,
            source_v};
    };
    const auto append_tri = [&](Bucket& bucket,
                                const data::RtpksTileMesh& mesh,
                                int tri_index,
                                int tile_x,
                                int tile_y,
                                float base_y,
                                float layer_lift,
                                bool conform_to_terrain,
                                const MaterialGpuResource& material,
                                float seam_center_x,
                                float seam_center_y,
                                float seam_overlap_tiles) {
        const std::uint32_t base = static_cast<std::uint32_t>(bucket.vertices.size());
        const std::size_t first = static_cast<std::size_t>(std::max(0, tri_index)) * 3U;
        for (std::size_t corner = 0; corner < 3U; ++corner) {
            bucket.vertices.push_back(make_vertex(mesh, mesh.triangles, mesh.tex_coords_tri, mesh.colors_tri,
                first + corner, tile_x, tile_y, base_y, layer_lift, conform_to_terrain, material,
                seam_center_x, seam_center_y, seam_overlap_tiles));
        }
        if (!mesh.vertex_animations.empty()) {
            if (bucket.vertex_clips.empty()) {
                for (const data::RtpksVertexAnimationClip& source : mesh.vertex_animations) {
                    MaterialRange::VertexClip clip;
                    clip.name = source.name;
                    clip.frame_time_ms = source.frame_time_ms;
                    clip.frames.resize(source.frames.size());
                    bucket.vertex_clips.push_back(std::move(clip));
                }
            }
            for (std::size_t clip_index = 0; clip_index < mesh.vertex_animations.size(); ++clip_index) {
                const data::RtpksVertexAnimationClip& source = mesh.vertex_animations[clip_index];
                for (std::size_t frame_index = 0; frame_index < source.frames.size(); ++frame_index) {
                    for (std::size_t corner = 0; corner < 3U; ++corner) {
                        bucket.vertex_clips[clip_index].frames[frame_index].push_back(make_vertex(
                            mesh, source.frames[frame_index], mesh.tex_coords_tri, mesh.colors_tri,
                            first + corner, tile_x, tile_y, base_y, layer_lift, conform_to_terrain, material,
                            seam_center_x, seam_center_y, seam_overlap_tiles));
                    }
                }
            }
        }
        bucket.indices.insert(bucket.indices.end(), {base, base + 1U, base + 2U});
    };
    const auto append_quad = [&](Bucket& bucket,
                                 const data::RtpksTileMesh& mesh,
                                 int quad_index,
                                 int tile_x,
                                 int tile_y,
                                 float base_y,
                                 float layer_lift,
                                 bool conform_to_terrain,
                                 const MaterialGpuResource& material,
                                 float seam_center_x,
                                 float seam_center_y,
                                 float seam_overlap_tiles) {
        const std::uint32_t base = static_cast<std::uint32_t>(bucket.vertices.size());
        const std::size_t first = static_cast<std::size_t>(std::max(0, quad_index)) * 4U;
        for (std::size_t corner = 0; corner < 4U; ++corner) {
            bucket.vertices.push_back(make_vertex(mesh, mesh.quads, mesh.tex_coords_quad, mesh.colors_quad,
                first + corner, tile_x, tile_y, base_y, layer_lift, conform_to_terrain, material,
                seam_center_x, seam_center_y, seam_overlap_tiles));
        }
        bucket.indices.insert(bucket.indices.end(), {base, base + 1U, base + 2U, base, base + 2U, base + 3U});
    };
    const auto append_tile = [&](int tile_id, int x, int y, std::size_t layer_index, const std::string& layer_id) -> bool {
        if (tile_id < 0) return false;
        const data::RtpksTileMesh* mesh = tile_package_->tileById(tile_id);
        if (!mesh) return false;
        float corners[4]{};
        terrain::fillTileCornerHeights(scene_, x, y, corners);
        const float floor_base_y = std::min(std::min(corners[0], corners[1]), std::min(corners[2], corners[3]));
        const bool conform_to_terrain = mesh_vertical_range(*mesh) <= 0.02f;
        const float base_y = conform_to_terrain ? 0.0f : floor_base_y;
        // Keep even the base RTPKS layer above the fallback heightfield. A zero
        // lift made coplanar terrain tiles depth-fight or disappear by backend.
        const float layer_lift = static_cast<float>(layer_index + 1U) * kLayerLift;
        for (const data::RtpksMaterialRange& range : mesh->material_ranges) {
            const auto slot_it = material_slot_by_id.find(range.material_id);
            const int slot = slot_it == material_slot_by_id.end() ? 0 : slot_it->second;
            const std::size_t material_index = static_cast<std::size_t>(std::clamp(slot, 0, static_cast<int>(tile_layer_mesh_.materials.size()) - 1));
            MaterialGpuResource& material = tile_layer_mesh_.materials[material_index];
            Bucket* bucket = nullptr;
            if (mesh->triggerable_door) {
                buckets.push_back(Bucket{});
                Bucket& placed = buckets.back();
                placed.material_class = material.material_class;
                placed.material_index = static_cast<int>(material_index);
                placed.trigger_phase = true;
                placed.trigger_layer_id = layer_id;
                placed.trigger_tile_x = x;
                placed.trigger_tile_y = y;
                placed.trigger_open_clip = mesh->door_open_animation;
                placed.trigger_close_clip = mesh->door_close_clip;
                placed.trigger_close_reverses = mesh->door_close_animation != "named" || mesh->door_close_clip.empty();
                bucket = &placed;
            } else {
                bucket = &buckets[material_index];
            }
            float seam_center_x = 0.0f;
            float seam_center_y = 0.0f;
            float seam_overlap_tiles = 0.0f;
            if (material.shoreline_seam_cover && scene_.water_shoreline_seam_overlap_pixels > 0.0f) {
                float min_x = std::numeric_limits<float>::max();
                float min_y = std::numeric_limits<float>::max();
                float max_x = std::numeric_limits<float>::lowest();
                float max_y = std::numeric_limits<float>::lowest();
                const auto include_vertex = [&](const std::vector<float>& positions, std::size_t vertex_index) {
                    const std::size_t pi = vertex_index * 3U;
                    const float local_x = read_float(positions, pi + 0U, 0.0f) + mesh->x_offset;
                    const float local_y = read_float(positions, pi + 1U, 0.0f) + mesh->y_offset;
                    min_x = std::min(min_x, local_x);
                    min_y = std::min(min_y, local_y);
                    max_x = std::max(max_x, local_x);
                    max_y = std::max(max_y, local_y);
                };
                for (int i = 0; i < range.tri_count; ++i) {
                    const std::size_t first = static_cast<std::size_t>(range.tri_start + i) * 3U;
                    include_vertex(mesh->triangles, first + 0U);
                    include_vertex(mesh->triangles, first + 1U);
                    include_vertex(mesh->triangles, first + 2U);
                }
                for (int i = 0; i < range.quad_count; ++i) {
                    const std::size_t first = static_cast<std::size_t>(range.quad_start + i) * 4U;
                    include_vertex(mesh->quads, first + 0U);
                    include_vertex(mesh->quads, first + 1U);
                    include_vertex(mesh->quads, first + 2U);
                    include_vertex(mesh->quads, first + 3U);
                }
                if (min_x <= max_x && min_y <= max_y) {
                    seam_center_x = (min_x + max_x) * 0.5f;
                    seam_center_y = (min_y + max_y) * 0.5f;
                    seam_overlap_tiles = scene_.water_shoreline_seam_overlap_pixels /
                        static_cast<float>(std::max(1, scene_.pixel_scale.map_pixels_per_tile));
                }
            }
            for (int i = 0; i < range.tri_count; ++i) {
                append_tri(*bucket, *mesh, range.tri_start + i, x, y, base_y, layer_lift, conform_to_terrain, material, seam_center_x, seam_center_y, seam_overlap_tiles);
            }
            for (int i = 0; i < range.quad_count; ++i) {
                append_quad(*bucket, *mesh, range.quad_start + i, x, y, base_y, layer_lift, conform_to_terrain, material, seam_center_x, seam_center_y, seam_overlap_tiles);
            }
        }
        return true;
    };

    int placed_tiles = 0;
    for (std::size_t layer_index = 0; layer_index < scene_.tile_layers.layers.size(); ++layer_index) {
        const TileLayerConfig& layer = scene_.tile_layers.layers[layer_index];
        if (!layer.visible) continue;
        for (int y = 0; y < static_cast<int>(layer.cells.size()); ++y) {
            const auto& row = layer.cells[static_cast<std::size_t>(y)];
            for (int x = 0; x < static_cast<int>(row.size()); ++x) {
                if (append_tile(row[static_cast<std::size_t>(x)], x, y, layer_index, layer.id)) ++placed_tiles;
            }
        }
    }

    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    for (const Bucket& bucket : buckets) {
        if (bucket.vertices.empty() || bucket.indices.empty()) continue;
        const std::size_t material_index = static_cast<std::size_t>(std::clamp(
            bucket.material_index, 0, static_cast<int>(tile_layer_mesh_.materials.size()) - 1));
        const std::uint32_t vertex_base = static_cast<std::uint32_t>(vertices.size());
        const std::uint32_t index_start = static_cast<std::uint32_t>(indices.size());
        vertices.insert(vertices.end(), bucket.vertices.begin(), bucket.vertices.end());
        for (std::uint32_t index : bucket.indices) {
            indices.push_back(vertex_base + index);
        }
        tile_layer_mesh_.ranges.push_back(MaterialRange{
            index_start,
            static_cast<std::uint32_t>(bucket.indices.size()),
            static_cast<int>(material_index),
            bucket.material_class,
            tile_layer_mesh_.materials[material_index].depth_prepass,
            tile_layer_mesh_.materials[material_index].render_order,
            bucket.trigger_phase,
            false,
            false,
            0,
            bucket.trigger_layer_id,
            bucket.trigger_tile_x,
            bucket.trigger_tile_y});
        MaterialRange& placed_range = tile_layer_mesh_.ranges.back();
        placed_range.vertex_start = vertex_base;
        placed_range.vertex_count = static_cast<std::uint32_t>(bucket.vertices.size());
        placed_range.trigger_open_clip = bucket.trigger_open_clip;
        placed_range.trigger_close_clip = bucket.trigger_close_clip;
        placed_range.trigger_close_reverses = bucket.trigger_close_reverses;
        placed_range.vertex_clips = bucket.vertex_clips;
    }
    std::stable_sort(
        tile_layer_mesh_.ranges.begin(),
        tile_layer_mesh_.ranges.end(),
        [](const MaterialRange& lhs, const MaterialRange& rhs) {
            return lhs.render_order < rhs.render_order;
        });

    if (!vertices.empty() && !indices.empty()) {
        const bgfx::Memory* ib_mem = bgfx::copy(indices.data(), static_cast<std::uint32_t>(indices.size() * sizeof(std::uint32_t)));
        const bool has_vertex_animation = std::any_of(
            tile_layer_mesh_.ranges.begin(), tile_layer_mesh_.ranges.end(),
            [](const MaterialRange& range) { return !range.vertex_clips.empty(); });
        if (has_vertex_animation) {
            tile_layer_mesh_.source_vertices = vertices;
            tile_layer_mesh_.animated_vertices = vertices;
            tile_layer_mesh_.dynamic_vbh = bgfx::createDynamicVertexBuffer(
                static_cast<std::uint32_t>(vertices.size()), layout_);
            if (bgfx::isValid(tile_layer_mesh_.dynamic_vbh)) {
                bgfx::update(tile_layer_mesh_.dynamic_vbh, 0, bgfx::copy(
                    vertices.data(), static_cast<std::uint32_t>(vertices.size() * sizeof(Vertex))));
            }
        } else {
            const bgfx::Memory* vb_mem = bgfx::copy(vertices.data(), static_cast<std::uint32_t>(vertices.size() * sizeof(Vertex)));
            tile_layer_mesh_.vbh = bgfx::createVertexBuffer(vb_mem, layout_);
        }
        tile_layer_mesh_.ibh = bgfx::createIndexBuffer(ib_mem, BGFX_BUFFER_INDEX32);
        if (!tile_layer_mesh_.valid()) {
            last_error_ = "Could not upload RTPKS tile layer mesh";
            return false;
        }
    }
    std::cerr << "[OverworldBgfx] RTPKS tiles=" << placed_tiles
              << " package=" << scene_.tile_package.path << std::endl;
    return true;
}

bool OverworldBgfxRenderer::Impl::buildTerrain() {
    terrain_flat_top_mesh_.destroy();
    terrain_slope_top_mesh_.destroy();
    terrain_wall_mesh_.destroy();
    if (!shouldRenderFallbackTerrain(scene_)) {
        return true;
    }

    std::vector<Vertex> flat_top_vertices;
    std::vector<std::uint32_t> flat_top_indices;
    std::vector<Vertex> slope_top_vertices;
    std::vector<std::uint32_t> slope_top_indices;
    std::vector<Vertex> wall_vertices;
    std::vector<std::uint32_t> wall_indices;

    const int grid_w = terrainWidth(scene_);
    const int grid_h = terrainHeight(scene_);
    const float tile_size = std::max(1.0f, scene_.grid.tile_size);
    flat_top_vertices.reserve(static_cast<std::size_t>(grid_w * grid_h * 4));
    flat_top_indices.reserve(static_cast<std::size_t>(grid_w * grid_h * 6));
    slope_top_vertices.reserve(static_cast<std::size_t>(grid_w * grid_h));
    slope_top_indices.reserve(static_cast<std::size_t>(grid_w * grid_h));
    wall_vertices.reserve(static_cast<std::size_t>(grid_w * grid_h * 4));
    wall_indices.reserve(static_cast<std::size_t>(grid_w * grid_h * 12));

    const auto tile_special = [this](int tx, int ty) -> int {
        if (ty < 0 || ty >= static_cast<int>(scene_.terrain.specials.size())) return kSpecialFlat;
        const auto& row = scene_.terrain.specials[static_cast<std::size_t>(ty)];
        if (tx < 0 || tx >= static_cast<int>(row.size())) return kSpecialFlat;
        return static_cast<int>(row[static_cast<std::size_t>(tx)]);
    };
    const auto tile_height = [this](int tx, int ty) -> int {
        if (ty < 0 || ty >= static_cast<int>(scene_.terrain.heights.size())) return 0;
        const auto& row = scene_.terrain.heights[static_cast<std::size_t>(ty)];
        if (tx < 0 || tx >= static_cast<int>(row.size())) return 0;
        return static_cast<int>(row[static_cast<std::size_t>(tx)]);
    };
    const auto tile_covers_cell = [this](int tx, int ty) -> bool {
        if (!tile_package_ || scene_.tile_layers.layers.empty()) return false;
        for (const TileLayerConfig& layer : scene_.tile_layers.layers) {
            if (!layer.visible) continue;
            for (int ay = 0; ay <= ty && ay < static_cast<int>(layer.cells.size()); ++ay) {
                const auto& row = layer.cells[static_cast<std::size_t>(ay)];
                for (int ax = 0; ax <= tx && ax < static_cast<int>(row.size()); ++ax) {
                    const int tile_id = row[static_cast<std::size_t>(ax)];
                    if (tile_id < 0) continue;
                    const data::RtpksTileMesh* tile = tile_package_->tileById(tile_id);
                    const int tw = tile ? std::max(1, tile->width) : 1;
                    const int th = tile ? std::max(1, tile->height) : 1;
                    if (tx >= ax && tx < ax + tw && ty >= ay && ty < ay + th) {
                        return true;
                    }
                }
            }
        }
        return false;
    };
    const auto is_slope_special = [](int special) {
        return special >= kSpecialRampNorth && special <= kSpecialConcaveNW;
    };
    const auto fill_corners = [&](int x, int y, float (&c)[4]) {
        terrain::fillTileCornerHeights(scene_, x, y, c);
    };
    const auto push_quad = [](std::vector<Vertex>& vertices,
                              std::vector<std::uint32_t>& indices,
                              float x0, float y0, float z0,
                              float x1, float y1, float z1,
                              float x2, float y2, float z2,
                              float x3, float y3, float z3,
                              std::uint32_t color) {
        const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
        vertices.push_back(Vertex{x0, y0, z0, color, 0.0f, 0.0f});
        vertices.push_back(Vertex{x1, y1, z1, color, 1.0f, 0.0f});
        vertices.push_back(Vertex{x2, y2, z2, color, 1.0f, 1.0f});
        vertices.push_back(Vertex{x3, y3, z3, color, 0.0f, 1.0f});
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    };
    const auto push_floor_quad = [](std::vector<Vertex>& vertices,
                                    std::vector<std::uint32_t>& indices,
                                    const interiors::DefaultRoomFloorClip& clip,
                                    float y00, float y10, float y11, float y01,
                                    std::uint32_t color) {
        const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
        vertices.push_back(Vertex{clip.x0, y00, clip.z0, color, clip.u0, clip.v0});
        vertices.push_back(Vertex{clip.x1, y10, clip.z0, color, clip.u1, clip.v0});
        vertices.push_back(Vertex{clip.x1, y11, clip.z1, color, clip.u1, clip.v1});
        vertices.push_back(Vertex{clip.x0, y01, clip.z1, color, clip.u0, clip.v1});
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    };
    const auto push_floor_triangle = [](std::vector<Vertex>& vertices,
                                        std::vector<std::uint32_t>& indices,
                                        const interiors::FloorCutoutTriangle& triangle,
                                        float y0, float y1, float y2,
                                        std::uint32_t color) {
        const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
        vertices.push_back(Vertex{
            triangle[0].x, y0, triangle[0].z, color, triangle[0].u, triangle[0].v});
        vertices.push_back(Vertex{
            triangle[1].x, y1, triangle[1].z, color, triangle[1].u, triangle[1].v});
        vertices.push_back(Vertex{
            triangle[2].x, y2, triangle[2].z, color, triangle[2].u, triangle[2].v});
        indices.insert(indices.end(), {base, base + 1, base + 2});
    };
    const bool default_interior_room = shouldRenderDefaultInteriorRoom(scene_);
    const auto& room = scene_.interior.default_room;
    const std::uint32_t floor_a = ramp_terrain_render::packTerrainColor(
        default_interior_room ? room.floor_color_a : scene_.terrain.floor_color_a);
    const std::uint32_t floor_b = ramp_terrain_render::packTerrainColor(
        default_interior_room ? room.floor_color_b : scene_.terrain.floor_color_b);
    const std::uint32_t first_non_base_a = ramp_terrain_render::packTerrainColor(scene_.terrain.first_non_base_floor_color_a);
    const std::uint32_t first_non_base_b = ramp_terrain_render::packTerrainColor(scene_.terrain.first_non_base_floor_color_b);
    const std::uint32_t ramp_a = ramp_terrain_render::packTerrainColor(scene_.terrain.ramp_color_a);
    const std::uint32_t ramp_b = ramp_terrain_render::packTerrainColor(scene_.terrain.ramp_color_b);
    for (int y = 0; y < grid_h; ++y) {
        for (int x = 0; x < grid_w; ++x) {
            const auto clip = interiors::clipDefaultRoomFloorCell(scene_, x, y, tile_size);
            const int special = tile_special(x, y);
            const bool slope = is_slope_special(special);
            std::vector<Vertex>& top_vertices = slope ? slope_top_vertices : flat_top_vertices;
            std::vector<std::uint32_t>& top_indices = slope ? slope_top_indices : flat_top_indices;
            const bool checker = ((x + y) & 1) != 0;
            std::uint32_t color = checker ? floor_b : floor_a;
            if (scene_.terrain.floor_height_recolor_enabled && tile_height(x, y) == 1) {
                color = checker ? first_non_base_b : first_non_base_a;
            }
            if (slope && scene_.terrain.ramp_recolor_enabled) {
                color = checker ? ramp_b : ramp_a;
            }
            if (!tile_covers_cell(x, y)) {
                if (default_interior_room && !scene_.interior.floor_cutouts.empty()) {
                    for (const auto& triangle :
                         interiors::clipFloorCellAgainstCutouts(scene_, x, y, tile_size)) {
                        push_floor_triangle(
                            top_vertices, top_indices, triangle,
                            terrain::heightAtWorldPositionOnTile(
                                scene_, triangle[0].x, triangle[0].z, x, y, true),
                            terrain::heightAtWorldPositionOnTile(
                                scene_, triangle[1].x, triangle[1].z, x, y, true),
                            terrain::heightAtWorldPositionOnTile(
                                scene_, triangle[2].x, triangle[2].z, x, y, true),
                            color);
                    }
                } else {
                    push_floor_quad(
                        top_vertices, top_indices, clip,
                        terrain::heightAtWorldPositionOnTile(scene_, clip.x0, clip.z0, x, y, true),
                        terrain::heightAtWorldPositionOnTile(scene_, clip.x1, clip.z0, x, y, true),
                        terrain::heightAtWorldPositionOnTile(scene_, clip.x1, clip.z1, x, y, true),
                        terrain::heightAtWorldPositionOnTile(scene_, clip.x0, clip.z1, x, y, true),
                        color);
                }
            }
        }
    }

    for (const auto& quad : interiors::buildDefaultRoomFloorApron(scene_, tile_size)) {
        const auto& p = quad.points;
        const std::uint32_t color =
            ((quad.source_tile_x + quad.source_tile_y) & 1) != 0 ? floor_b : floor_a;
        const std::uint32_t base = static_cast<std::uint32_t>(flat_top_vertices.size());
        flat_top_vertices.push_back(Vertex{p[0].x, p[0].y, p[0].z, color, 1.0f, 0.0f});
        flat_top_vertices.push_back(Vertex{p[1].x, p[1].y, p[1].z, color, 0.0f, 0.0f});
        flat_top_vertices.push_back(Vertex{p[2].x, p[2].y, p[2].z, color, 0.0f, 1.0f});
        flat_top_vertices.push_back(Vertex{p[3].x, p[3].y, p[3].z, color, 1.0f, 1.0f});
        flat_top_indices.insert(flat_top_indices.end(),
            {base, base + 1, base + 2, base, base + 2, base + 3});
    }

    const auto add_wall_if_drop = [&](float xa, float za, float ya0, float ya1,
                                      float xb, float zb, float yb0, float yb1,
                                      std::uint32_t color) {
        const float high = std::min(ya0, ya1);
        const float low = std::min(yb0, yb1);
        if (high <= low) return;
        push_quad(wall_vertices, wall_indices, xa, low, za, xb, low, zb, xb, high, zb, xa, high, za, color);
    };
    const std::uint32_t wall_ns = ramp_terrain_render::packTerrainColor(
        default_interior_room ? room.wall_color_ns : scene_.terrain.wall_color_ns);
    const std::uint32_t wall_ew = ramp_terrain_render::packTerrainColor(
        default_interior_room ? room.wall_color_ew : scene_.terrain.wall_color_ew);
    for (int y = 0; y < grid_h; ++y) {
        for (int x = 0; x < grid_w; ++x) {
            const float x0 = static_cast<float>(x) * tile_size;
            const float z0 = static_cast<float>(y) * tile_size;
            const float x1 = x0 + tile_size;
            const float z1 = z0 + tile_size;
            float c[4]{};
            fill_corners(x, y, c);
            if (x + 1 < grid_w) {
                float n[4]{};
                fill_corners(x + 1, y, n);
                add_wall_if_drop(x1, z0, c[1], c[2], x1, z1, n[0], n[3], wall_ew);
                add_wall_if_drop(x1, z0, n[0], n[3], x1, z1, c[1], c[2], wall_ew);
            }
            if (y + 1 < grid_h) {
                float n[4]{};
                fill_corners(x, y + 1, n);
                add_wall_if_drop(x0, z1, c[3], c[2], x1, z1, n[0], n[1], wall_ns);
                add_wall_if_drop(x0, z1, n[0], n[1], x1, z1, c[3], c[2], wall_ns);
            }
        }
    }

    if (default_interior_room) {
        const std::uint32_t trim = ramp_terrain_render::packTerrainColor(room.trim_color);
        const std::uint32_t baseboard =
            ramp_terrain_render::packTerrainColor(room.baseboard_color);
        const std::uint32_t top_cap =
            ramp_terrain_render::packTerrainColor(room.top_cap_color);
        const auto push_wall_segment = [&](std::string_view edge,
                                           float ax, float az, float ay,
                                           float bx, float bz, float by,
                                           float height_tiles,
                                           std::uint32_t body_color) {
            const float height = std::max(0.0f, height_tiles) * tile_size;
            if (height <= 0.001f) return;
            const auto [normal_x, normal_z] =
                interiors::wallOutwardNormal(edge);
            const auto line = interiors::placeDefaultRoomWallLine(
                scene_, edge, tile_size, ax, az, bx, bz);
            ax = line.ax;
            az = line.az;
            bx = line.bx;
            bz = line.bz;
            const float band = std::min(
                std::max(0.0f, room.trim_height_tiles) * tile_size,
                height * 0.35f);
            const float body_bottom_a = ay + band;
            const float body_bottom_b = by + band;
            const float body_top_a = ay + height - band;
            const float body_top_b = by + height - band;
            if (band > 0.001f) {
                push_quad(wall_vertices, wall_indices,
                    ax, ay, az, bx, by, bz, bx, body_bottom_b, bz, ax, body_bottom_a, az,
                    baseboard);
            }
            if (body_top_a > body_bottom_a + 0.001f ||
                body_top_b > body_bottom_b + 0.001f) {
                push_quad(wall_vertices, wall_indices,
                    ax, body_bottom_a, az, bx, body_bottom_b, bz,
                    bx, body_top_b, bz, ax, body_top_a, az, body_color);
            }
            if (band > 0.001f) {
                push_quad(wall_vertices, wall_indices,
                    ax, body_top_a, az, bx, body_top_b, bz,
                    bx, by + height, bz, ax, ay + height, az, trim);
            }
            const float cap_depth =
                std::max(0.0f, room.top_cap_depth_tiles) * tile_size;
            if (room.black_top_cap && cap_depth > 0.001f) {
                push_quad(wall_vertices, wall_indices,
                    ax, ay + height, az,
                    bx, by + height, bz,
                    bx + normal_x * cap_depth, by + height, bz + normal_z * cap_depth,
                    ax + normal_x * cap_depth, ay + height, az + normal_z * cap_depth,
                    top_cap);
            }
        };
        const auto push_boundary_segment = [&](std::string_view edge,
                                                bool opening,
                                                float ax, float az, float ay,
                                                float bx, float bz, float by,
                                                float height_tiles,
                                                std::uint32_t body_color) {
            if (!opening) {
                push_wall_segment(
                    edge, ax, az, ay, bx, bz, by, height_tiles, body_color);
                return;
            }
            const float clearance_tiles =
                defaultInteriorOpeningHeightTiles(scene_, edge);
            const float lintel_height_tiles =
                std::max(0.0f, height_tiles - clearance_tiles);
            if (lintel_height_tiles <= 0.001f) return;
            const float clearance = clearance_tiles * tile_size;
            push_wall_segment(
                edge, ax, az, ay + clearance, bx, bz, by + clearance,
                lintel_height_tiles, body_color);
        };
        const float lower_facade_depth =
            std::max(0.0f, room.lower_facade_depth_tiles) * tile_size;
        if (lower_facade_depth > 0.001f) {
            const std::uint32_t lower_facade =
                ramp_terrain_render::packTerrainColor(room.lower_facade_color);
            for (int x = 0; x < grid_w; ++x) {
                float south[4]{};
                fill_corners(x, grid_h - 1, south);
                const auto line = interiors::placeDefaultRoomWallLine(
                    scene_, "south", tile_size,
                    (x + 1) * tile_size, grid_h * tile_size,
                    x * tile_size, grid_h * tile_size);
                push_quad(wall_vertices, wall_indices,
                    line.ax, south[2] - lower_facade_depth, line.az,
                    line.bx, south[3] - lower_facade_depth, line.bz,
                    line.bx, south[3], line.bz,
                    line.ax, south[2], line.az,
                    lower_facade);
            }
        }
        for (int x = 0; x < grid_w; ++x) {
            float north[4]{};
            fill_corners(x, 0, north);
            push_boundary_segment("north",
                defaultInteriorOpeningCovers(scene_, "north", x),
                x * tile_size, 0.0f, north[0],
                (x + 1) * tile_size, 0.0f, north[1],
                defaultInteriorWallHeightTiles(scene_, "north"), wall_ns);
            float south[4]{};
            fill_corners(x, grid_h - 1, south);
            push_boundary_segment("south",
                defaultInteriorOpeningCovers(scene_, "south", x),
                (x + 1) * tile_size, grid_h * tile_size, south[2],
                x * tile_size, grid_h * tile_size, south[3],
                defaultInteriorWallHeightTiles(scene_, "south"), wall_ns);
        }
        for (int y = 0; y < grid_h; ++y) {
            float west[4]{};
            fill_corners(0, y, west);
            push_boundary_segment("west",
                defaultInteriorOpeningCovers(scene_, "west", y),
                0.0f, (y + 1) * tile_size, west[3],
                0.0f, y * tile_size, west[0],
                defaultInteriorWallHeightTiles(scene_, "west"), wall_ew);
            float east[4]{};
            fill_corners(grid_w - 1, y, east);
            push_boundary_segment("east",
                defaultInteriorOpeningCovers(scene_, "east", y),
                grid_w * tile_size, y * tile_size, east[1],
                grid_w * tile_size, (y + 1) * tile_size, east[2],
                defaultInteriorWallHeightTiles(scene_, "east"), wall_ew);
        }
    }

    if (flat_top_vertices.empty() && slope_top_vertices.empty() && wall_vertices.empty()) {
        return true;
    }

    const auto upload_mesh = [this](MeshGpuResource& mesh,
                                    const std::vector<Vertex>& vertices,
                                    const std::vector<std::uint32_t>& indices) -> bool {
        if (vertices.empty() || indices.empty()) {
            return true;
        }
        const bgfx::Memory* vb_mem = bgfx::copy(vertices.data(), static_cast<std::uint32_t>(vertices.size() * sizeof(Vertex)));
        const bgfx::Memory* ib_mem = bgfx::copy(indices.data(), static_cast<std::uint32_t>(indices.size() * sizeof(std::uint32_t)));
        mesh.vbh = bgfx::createVertexBuffer(vb_mem, layout_);
        mesh.ibh = bgfx::createIndexBuffer(ib_mem, BGFX_BUFFER_INDEX32);
        mesh.ranges.push_back(MaterialRange{0, static_cast<std::uint32_t>(indices.size()), 0, MaterialClass::Opaque});
        mesh.materials.push_back(MaterialGpuResource{});
        return mesh.valid();
    };
    const bool uploaded =
        upload_mesh(terrain_flat_top_mesh_, flat_top_vertices, flat_top_indices) &&
        upload_mesh(terrain_slope_top_mesh_, slope_top_vertices, slope_top_indices) &&
        upload_mesh(terrain_wall_mesh_, wall_vertices, wall_indices);
    if (!uploaded) {
        return false;
    }
    return true;
}

bool OverworldBgfxRenderer::Impl::buildModels() {
    models_.clear();

    for (const ModelPlacementConfig& placement : scene_.models) {
        if (placement.glb_path.empty()) continue;
        std::string error;
        data::GlbMesh glb = data::loadGlbModel(placement.glb_path, &error);
        if (!glb.valid) {
            std::cerr << "[OverworldBgfx] Skipping model '" << placement.id << "': " << error << std::endl;
            continue;
        }

        ModelGpuResource model;
        model.placement_id = placement.id;
        placementMatrix(placement.x, placement.y, placement.z, placement.yaw_deg, placement.scale, model.model_matrix);

        model.mesh.materials.resize(glb.materials.size());
        for (std::size_t i = 0; i < glb.materials.size(); ++i) {
            const data::GlbMaterial& src = glb.materials[i];
            MaterialGpuResource& dst = model.mesh.materials[i];
            dst.material_class = materialClassFor(src);
            dst.alpha_cutoff = src.alpha_cutoff;
            std::copy(std::begin(src.base_color), std::end(src.base_color), std::begin(dst.base_color));
            if (src.has_texture) {
                dst.texture = decodeImageBytes(src.image_bytes, "", placement.id.c_str());
            }
            if (dst.texture.has_partial_alpha || dst.base_color[3] < 0.999f) {
                dst.material_class = MaterialClass::TrueBlend;
                dst.alpha_cutoff = 0.0f;
            } else if (dst.texture.has_zero_alpha && dst.material_class == MaterialClass::Opaque) {
                dst.material_class = MaterialClass::MaskCutout;
                dst.alpha_cutoff = std::max(dst.alpha_cutoff, 0.5f);
            }
        }

        std::vector<Vertex> vertices;
        std::vector<std::uint32_t> indices;
        std::vector<data::GlbVertex> source_vertices;
        vertices.reserve(glb.triangles.size() * 3);
        indices.reserve(glb.triangles.size() * 3);
        if (!glb.animations.empty()) source_vertices.reserve(glb.triangles.size() * 3);

        for (std::size_t mat_index = 0; mat_index < glb.materials.size(); ++mat_index) {
            const std::uint32_t range_start = static_cast<std::uint32_t>(indices.size());
            for (const data::GlbTriangle& tri : glb.triangles) {
                if (tri.material != static_cast<int>(mat_index)) continue;
                const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
                vertices.push_back(Vertex{tri.a.x, tri.a.y, tri.a.z,
                    packAbgr(data::compositeGlbVertexColor(tri.a.r, glb.materials[mat_index]),
                        data::compositeGlbVertexColor(tri.a.g, glb.materials[mat_index]),
                        data::compositeGlbVertexColor(tri.a.b, glb.materials[mat_index]),
                        data::compositeGlbAlpha(tri.a, glb.materials[mat_index])), tri.a.u, tri.a.v});
                vertices.push_back(Vertex{tri.b.x, tri.b.y, tri.b.z,
                    packAbgr(data::compositeGlbVertexColor(tri.b.r, glb.materials[mat_index]),
                        data::compositeGlbVertexColor(tri.b.g, glb.materials[mat_index]),
                        data::compositeGlbVertexColor(tri.b.b, glb.materials[mat_index]),
                        data::compositeGlbAlpha(tri.b, glb.materials[mat_index])), tri.b.u, tri.b.v});
                vertices.push_back(Vertex{tri.c.x, tri.c.y, tri.c.z,
                    packAbgr(data::compositeGlbVertexColor(tri.c.r, glb.materials[mat_index]),
                        data::compositeGlbVertexColor(tri.c.g, glb.materials[mat_index]),
                        data::compositeGlbVertexColor(tri.c.b, glb.materials[mat_index]),
                        data::compositeGlbAlpha(tri.c, glb.materials[mat_index])), tri.c.u, tri.c.v});
                if (!glb.animations.empty()) {
                    source_vertices.insert(source_vertices.end(), {tri.a, tri.b, tri.c});
                }
                indices.insert(indices.end(), {base, base + 1, base + 2});
            }
            const std::uint32_t count = static_cast<std::uint32_t>(indices.size()) - range_start;
            if (count > 0) {
                model.mesh.ranges.push_back(MaterialRange{
                    range_start,
                    count,
                    static_cast<int>(mat_index),
                    model.mesh.materials[mat_index].material_class});
            }
        }

        if (vertices.empty() || indices.empty()) continue;

        const bgfx::Memory* ib_mem = bgfx::copy(indices.data(), static_cast<std::uint32_t>(indices.size() * sizeof(std::uint32_t)));
        if (glb.animations.empty()) {
            const bgfx::Memory* vb_mem = bgfx::copy(vertices.data(), static_cast<std::uint32_t>(vertices.size() * sizeof(Vertex)));
            model.mesh.vbh = bgfx::createVertexBuffer(vb_mem, layout_);
        } else {
            model.mesh.dynamic_vbh = bgfx::createDynamicVertexBuffer(
                static_cast<std::uint32_t>(vertices.size()), layout_);
            if (bgfx::isValid(model.mesh.dynamic_vbh)) {
                bgfx::update(model.mesh.dynamic_vbh, 0, bgfx::copy(
                    vertices.data(), static_cast<std::uint32_t>(vertices.size() * sizeof(Vertex))));
            }
            glb.triangles.clear();
            glb.materials.clear();
            model.animation_mesh = std::move(glb);
            model.source_vertices = std::move(source_vertices);
            model.animated_vertices = std::move(vertices);
        }
        model.mesh.ibh = bgfx::createIndexBuffer(ib_mem, BGFX_BUFFER_INDEX32);

        if (model.mesh.valid()) {
            models_.push_back(std::move(model));
        } else {
            last_error_ = "Could not upload GLB mesh for " + placement.id;
            model.mesh.destroy();
            return false;
        }
    }
    return true;
}

void OverworldBgfxRenderer::Impl::updateModelAnimation(ModelGpuResource& model) const {
    if (!bgfx::isValid(model.mesh.dynamic_vbh) || model.source_vertices.empty()) return;
    const double time_seconds = override_animation_clock_
        ? (animations_enabled_ ? std::max(0.0, animation_time_seconds_) : 0.0) *
            static_cast<double>(scene_.environment_animation_speed)
        : std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count() *
            static_cast<double>(scene_.environment_animation_speed);
    const std::vector<std::vector<float>> weights =
        data::sampleGlbMorphWeights(model.animation_mesh, time_seconds);
    const std::vector<std::array<float, 4>> rotations =
        data::sampleGlbNodeRotations(model.animation_mesh, time_seconds);
    if (weights.empty() && rotations.empty()) return;
    for (std::size_t i = 0; i < model.source_vertices.size(); ++i) {
        const std::array<float, 3> position = data::sampleGlbAnimatedPosition(
            model.animation_mesh, model.source_vertices[i], weights, rotations);
        model.animated_vertices[i].x = position[0];
        model.animated_vertices[i].y = position[1];
        model.animated_vertices[i].z = position[2];
    }
    bgfx::update(model.mesh.dynamic_vbh, 0, bgfx::copy(
        model.animated_vertices.data(),
        static_cast<std::uint32_t>(model.animated_vertices.size() * sizeof(Vertex))));
}

bool OverworldBgfxRenderer::Impl::buildStaticMapChunks() {
    for (StaticChunkGpuResource& chunk : static_chunks_) {
        chunk.destroy();
    }
    static_chunks_.clear();
    if (pending_static_chunks_.empty()) {
        return true;
    }

    const SceneConfig primary_scene = scene_;
    std::optional<data::RtpksTilePackage> primary_tile_package = std::move(tile_package_);
    MeshGpuResource primary_flat_top = terrain_flat_top_mesh_;
    MeshGpuResource primary_slope_top = terrain_slope_top_mesh_;
    MeshGpuResource primary_wall = terrain_wall_mesh_;
    MeshGpuResource primary_tiles = tile_layer_mesh_;
    std::vector<ModelGpuResource> primary_models = std::move(models_);

    terrain_flat_top_mesh_ = MeshGpuResource{};
    terrain_slope_top_mesh_ = MeshGpuResource{};
    terrain_wall_mesh_ = MeshGpuResource{};
    tile_layer_mesh_ = MeshGpuResource{};
    models_.clear();

    const auto restore_primary = [&]() {
        scene_ = primary_scene;
        tile_package_ = std::move(primary_tile_package);
        terrain_flat_top_mesh_ = primary_flat_top;
        terrain_slope_top_mesh_ = primary_slope_top;
        terrain_wall_mesh_ = primary_wall;
        tile_layer_mesh_ = primary_tiles;
        models_ = std::move(primary_models);
    };

    for (const OverworldBgfxRenderer::StaticMapChunk& request : pending_static_chunks_) {
        scene_ = request.scene;
        tile_package_.reset();
        terrain_flat_top_mesh_ = MeshGpuResource{};
        terrain_slope_top_mesh_ = MeshGpuResource{};
        terrain_wall_mesh_ = MeshGpuResource{};
        tile_layer_mesh_ = MeshGpuResource{};
        models_.clear();

        if (!loadTilePackage() || !buildTerrain() || !buildTileLayers() || !buildModels()) {
            restore_primary();
            return false;
        }

        StaticChunkGpuResource chunk;
        chunk.scene = request.scene;
        chunk.origin_x = request.origin_x;
        chunk.origin_y = request.origin_y;
        chunk.origin_z = request.origin_z;
        placementMatrix(
            request.origin_x,
            request.origin_y,
            request.origin_z,
            0.0f,
            1.0f,
            chunk.world_matrix);
        chunk.terrain_flat_top_mesh = terrain_flat_top_mesh_;
        chunk.terrain_slope_top_mesh = terrain_slope_top_mesh_;
        chunk.terrain_wall_mesh = terrain_wall_mesh_;
        chunk.tile_layer_mesh = tile_layer_mesh_;
        chunk.models = std::move(models_);
        for (ModelGpuResource& model : chunk.models) {
            model.model_matrix[12] += request.origin_x;
            model.model_matrix[13] += request.origin_y;
            model.model_matrix[14] += request.origin_z;
        }
        static_chunks_.push_back(std::move(chunk));

        terrain_flat_top_mesh_ = MeshGpuResource{};
        terrain_slope_top_mesh_ = MeshGpuResource{};
        terrain_wall_mesh_ = MeshGpuResource{};
        tile_layer_mesh_ = MeshGpuResource{};
        models_.clear();
    }

    restore_primary();
    return true;
}

bool OverworldBgfxRenderer::Impl::buildPlayerTexture() {
    const CharacterGpuTextures& textures = texturesForCharacter(character_);
    if (!textures.color.valid()) {
        last_error_ = "Could not load player billboard texture";
        return false;
    }
    return true;
}

bool OverworldBgfxRenderer::Impl::buildShadowTexture() {
    int sw = 0;
    int sh = 0;
    const std::vector<std::uint8_t> pixels = buildSpriteShadowRgba(scene_.sprite_shadow, sw, sh);
    shadow_texture_ = createTextureFromRgba(pixels.data(), sw, sh, "sprite-shadow");
    return shadow_texture_.valid();
}

const OverworldBgfxRenderer::Impl::CharacterGpuTextures& OverworldBgfxRenderer::Impl::texturesForCharacter(
    const CharacterSpriteDefinition& character) const {
    auto it = character_textures_.find(character.texture_path);
    if (it != character_textures_.end() && it->second.color.valid()) {
        return it->second;
    }

    CharacterGpuTextures loaded{};
    loaded.color = decodeImageBytes(character.texture_png_bytes, character.texture_path, "character-billboard");
    const RgbaImage white_rgba = buildWhiteSilhouetteFromPngBytes(character.texture_png_bytes);
    if (white_rgba.valid()) {
        loaded.white = createTextureFromRgba(
            white_rgba.pixels.data(),
            white_rgba.width,
            white_rgba.height,
            "character-billboard-white");
    }
    if (!loaded.white.valid()) {
        loaded.white = loaded.color;
    }
    if (!character.run_texture_png_bytes.empty()) {
        loaded.run_color = decodeImageBytes(
            character.run_texture_png_bytes,
            character.texture_path + "|run",
            "character-billboard-run");
        const RgbaImage run_white_rgba = buildWhiteSilhouetteFromPngBytes(character.run_texture_png_bytes);
        if (run_white_rgba.valid()) {
            loaded.run_white = createTextureFromRgba(
                run_white_rgba.pixels.data(),
                run_white_rgba.width,
                run_white_rgba.height,
                "character-billboard-run-white");
        }
        if (!loaded.run_white.valid()) {
            loaded.run_white = loaded.run_color;
        }
    }
    for (const auto& [activity_id, png_bytes] : character.activity_texture_png_bytes) {
        if (activity_id.empty() || png_bytes.empty()) {
            continue;
        }
        TextureGpuResource color = decodeImageBytes(
            png_bytes,
            character.texture_path + "|activity|" + activity_id,
            "character-billboard-activity");
        if (!color.valid()) {
            continue;
        }
        loaded.activity_color[activity_id] = color;
        const RgbaImage activity_white_rgba = buildWhiteSilhouetteFromPngBytes(png_bytes);
        if (activity_white_rgba.valid()) {
            loaded.activity_white[activity_id] = createTextureFromRgba(
                activity_white_rgba.pixels.data(),
                activity_white_rgba.width,
                activity_white_rgba.height,
                "character-billboard-activity-white");
        }
        if (!loaded.activity_white[activity_id].valid()) {
            loaded.activity_white[activity_id] = loaded.activity_color[activity_id];
        }
    }
    if (loaded.color.valid()) {
        character_textures_[character.texture_path] = loaded;
        return character_textures_[character.texture_path];
    }

    static CharacterGpuTextures empty{};
    return empty;
}

const OverworldBgfxRenderer::Impl::TextureGpuResource& OverworldBgfxRenderer::Impl::textureForKey(
    const std::string& cache_key,
    const std::vector<std::uint8_t>& png_bytes,
    const std::string& fallback_path,
    const char* debug_name) const {
    if (cache_key == "__procedural_white_pixel") {
        return white_texture_;
    }
    auto it = effect_textures_.find(cache_key);
    if (it != effect_textures_.end() && it->second.valid()) {
        return it->second;
    }
    TextureGpuResource loaded = decodeImageBytes(png_bytes, fallback_path, debug_name);
    if (loaded.valid()) {
        effect_textures_[cache_key] = loaded;
        return effect_textures_[cache_key];
    }
    return white_texture_;
}

void OverworldBgfxRenderer::Impl::submitMesh(
    const MeshGpuResource& mesh,
    const float* model_matrix,
    bgfx::ProgramHandle program,
    MaterialClass pass,
    float,
    std::uint64_t state,
    bgfx::ViewId view_id,
    bool ordered_materials,
    const float* camera_clip,
    bool use_tank_lighting) const {
    if (!mesh.valid()) return;
    for (const MaterialRange& range : mesh.ranges) {
        if (range.index_count == 0) continue;
        if (ordered_materials) {
            if (range.render_order <= 0) continue;
        } else if (range.render_order > 0 || range.material_class != pass) {
            continue;
        }
        const MaterialClass effective_pass = ordered_materials ? range.material_class : pass;
        const MaterialGpuResource* material = nullptr;
        if (range.material >= 0 && range.material < static_cast<int>(mesh.materials.size())) {
            material = &mesh.materials[static_cast<std::size_t>(range.material)];
        }
        const TextureGpuResource* selected_texture = material && material->texture.valid() ? &material->texture : &white_texture_;
        float uv_offset[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const double animation_time_ms = override_animation_clock_
            ? (range.trigger_phase
                ? (animations_enabled_ && range.trigger_active
                    ? std::max(0.0, animation_time_seconds_) * 1000.0
                    : 0.0)
                : (animations_enabled_ ? std::max(0.0, animation_time_seconds_) * 1000.0 : 0.0) *
                    static_cast<double>(scene_.environment_animation_speed))
            : (range.trigger_phase
                ? (range.trigger_active
                    ? static_cast<double>(std::max<std::int64_t>(0, now_ms - range.trigger_started_ms))
                    : 0.0)
                : static_cast<double>(now_ms) * static_cast<double>(scene_.environment_animation_speed));
        if (material && !material->animation_frames.empty() && material->animation_frame_time_ms > 0) {
            const AnimationCursor frame_cursor = animationCursor(
                doors::animationSample(
                    range.trigger_phase,
                    animation_time_ms / static_cast<double>(material->animation_frame_time_ms),
                    static_cast<int>(material->animation_frames.size())),
                static_cast<int>(material->animation_frames.size()),
                range.trigger_phase ? false : material->animation_loop);
            const std::size_t frame_index = static_cast<std::size_t>(range.trigger_phase && range.trigger_reverse
                ? static_cast<int>(material->animation_frames.size()) - 1 - frame_cursor.frame
                : frame_cursor.frame);
            if (material->animation_frames[frame_index].valid()) selected_texture = &material->animation_frames[frame_index];
        }
        if (material && material->animation_frame_time_ms > 0 && material->animation_frame_count > 0) {
            const double timebase_hz = material->animation_timebase_hz > 0.0f
                ? static_cast<double>(material->animation_timebase_hz)
                : 1000.0 / static_cast<double>(material->animation_frame_time_ms);
            const double scroll_speed = material->water_animation
                ? static_cast<double>(scene_.water_scroll_speed)
                : 1.0;
            const double forward_raw_sample = doors::animationSample(
                range.trigger_phase,
                animation_time_ms * timebase_hz * scroll_speed / 1000.0,
                material->animation_frame_count);
            const double scroll_raw_sample = range.trigger_phase && range.trigger_reverse
                ? std::max(0.0, static_cast<double>(material->animation_frame_count - 1) - forward_raw_sample)
                : forward_raw_sample;
            const double wave_raw_sample = material->shoreline_animation
                ? waveSampleWithWait(
                    animation_time_ms,
                    timebase_hz,
                    material->shoreline_cycle_frame_count,
                    doors::animationLoops(range.trigger_phase, material->animation_loop),
                    static_cast<double>(scene_.water_wave_speed),
                    static_cast<double>(scene_.water_wave_wait_seconds))
                : scroll_raw_sample;
            const AnimationCursor scroll_cursor = animationCursor(
                scroll_raw_sample,
                material->animation_frame_count,
                doors::animationLoops(range.trigger_phase, material->animation_loop));
            const AnimationCursor wave_cursor = animationCursor(
                wave_raw_sample,
                material->animation_frame_count,
                doors::animationLoops(range.trigger_phase, material->animation_loop));
            if (!material->animation_uv_offsets.empty()) {
                const auto& scroll_offset = material->animation_uv_offsets[
                    static_cast<std::size_t>(scroll_cursor.frame) % material->animation_uv_offsets.size()];
                const auto& next_scroll_offset = material->animation_uv_offsets[
                    static_cast<std::size_t>(scroll_cursor.next_frame) % material->animation_uv_offsets.size()];
                const auto& wave_offset = material->animation_uv_offsets[
                    static_cast<std::size_t>(wave_cursor.frame) % material->animation_uv_offsets.size()];
                const auto& next_wave_offset = material->animation_uv_offsets[
                    static_cast<std::size_t>(wave_cursor.next_frame) % material->animation_uv_offsets.size()];
                const bool smooth_uv = !material->animation_step ||
                    (material->water_animation && scene_.water_smooth_uv_motion);
                const float scroll_amount = smooth_uv ? scroll_cursor.fraction : 0.0f;
                const float wave_amount = smooth_uv ? wave_cursor.fraction : 0.0f;
                // Ambient Nitro material matrices use inverse sampling. A
                // scripted door instead advances into its transparent texture
                // region and clamps there, producing one inward slide.
                uv_offset[0] = doors::animationUvOffset(range.trigger_phase, wrappedUvLerp(
                    scroll_offset[0], next_scroll_offset[0], scroll_amount, material->uv_wrap_period[0]));
                uv_offset[1] = doors::animationUvOffset(range.trigger_phase, wrappedUvLerp(
                    wave_offset[1], next_wave_offset[1], wave_amount, material->uv_wrap_period[1]));
            }
            if (!material->animation_image_keyframes.empty()) {
                const int image_frame_count = std::max(1, material->animation_image_frame_count);
                const int image_frame = wave_cursor.frame % image_frame_count;
                const MaterialGpuResource::ImageKeyframe* selected_keyframe =
                    &material->animation_image_keyframes.front();
                for (const MaterialGpuResource::ImageKeyframe& keyframe : material->animation_image_keyframes) {
                    if (keyframe.frame > image_frame) break;
                    selected_keyframe = &keyframe;
                }
                if (selected_keyframe->texture.valid()) selected_texture = &selected_keyframe->texture;
            }
        }
        const TextureGpuResource& texture = *selected_texture;
        const bool tank_lit = use_tank_lighting && aquarium_tank_lighting_.enabled;
        const float br = std::max(0.0f, tank_lit
            ? aquarium_tank_lighting_.brightness
            : scene_.lighting_brightness);
        float tint[4] = {
            (tank_lit ? aquarium_tank_lighting_.tint[0] : scene_.lighting_tint_r) * br,
            (tank_lit ? aquarium_tank_lighting_.tint[1] : scene_.lighting_tint_g) * br,
            (tank_lit ? aquarium_tank_lighting_.tint[2] : scene_.lighting_tint_b) * br,
            effective_pass == MaterialClass::Opaque ? 0.0f : 0.5f,
        };
        if (material) {
            tint[0] *= material->base_color[0];
            tint[1] *= material->base_color[1];
            tint[2] *= material->base_color[2];
            tint[3] = effective_pass == MaterialClass::Opaque ? 0.0f : material->alpha_cutoff;
        }
        const float adjust[4] = {1.0f, 1.0f, 1.0f, 0.0f};
        const float texture_blur[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        const float light_dir[4] = {0.0f, 1.0f, 0.0f, 0.0f};
        const float light_params[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        bgfx::setTransform(model_matrix);
        if (bgfx::isValid(mesh.dynamic_vbh)) bgfx::setVertexBuffer(0, mesh.dynamic_vbh);
        else bgfx::setVertexBuffer(0, mesh.vbh);
        bgfx::setIndexBuffer(mesh.ibh, range.start_index, range.index_count);
        std::uint64_t texture_sampler_flags = material ? material->sampler_flags : samplerFlags();
        if (doors::clampsTextureEdges(range.trigger_phase)) {
            texture_sampler_flags |= BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        }
        bgfx::setTexture(0, tex_uniform_, texture.handle, texture_sampler_flags);
        bgfx::setUniform(tint_cutoff_uniform_, tint);
        bgfx::setUniform(color_adjust_uniform_, adjust);
        bgfx::setUniform(texture_blur_uniform_, texture_blur);
        bgfx::setUniform(uv_offset_uniform_, uv_offset);
        bgfx::setUniform(light_dir_uniform_, light_dir);
        bgfx::setUniform(light_params_uniform_, light_params);
        const float no_camera_clip[4]{};
        bgfx::setUniform(camera_clip_uniform_, camera_clip ? camera_clip : no_camera_clip);
        bgfx::setState(ordered_materials ? stateFor(effective_pass) : state);
        bgfx::submit(view_id, program);
    }
}

void OverworldBgfxRenderer::Impl::submitAlphaDepthPrepass(
    const MeshGpuResource& mesh,
    const float* model_matrix,
    bgfx::ProgramHandle program,
    MaterialClass pass,
    bgfx::ViewId view_id) const {
    if (!mesh.valid()) return;
    for (const MaterialRange& range : mesh.ranges) {
        if (range.material_class != pass || !range.depth_prepass || range.index_count == 0) continue;
        const MaterialGpuResource* material = nullptr;
        if (range.material >= 0 && range.material < static_cast<int>(mesh.materials.size())) {
            material = &mesh.materials[static_cast<std::size_t>(range.material)];
        }
        const TextureGpuResource* selected_texture = material && material->texture.valid() ? &material->texture : &white_texture_;
        float uv_offset[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const double animation_time_ms = override_animation_clock_
            ? (range.trigger_phase
                ? (animations_enabled_ && range.trigger_active
                    ? std::max(0.0, animation_time_seconds_) * 1000.0
                    : 0.0)
                : (animations_enabled_ ? std::max(0.0, animation_time_seconds_) * 1000.0 : 0.0) *
                    static_cast<double>(scene_.environment_animation_speed))
            : (range.trigger_phase
                ? (range.trigger_active
                    ? static_cast<double>(std::max<std::int64_t>(0, now_ms - range.trigger_started_ms))
                    : 0.0)
                : static_cast<double>(now_ms) * static_cast<double>(scene_.environment_animation_speed));
        if (material && !material->animation_frames.empty() && material->animation_frame_time_ms > 0) {
            const AnimationCursor frame_cursor = animationCursor(
                doors::animationSample(
                    range.trigger_phase,
                    animation_time_ms / static_cast<double>(material->animation_frame_time_ms),
                    static_cast<int>(material->animation_frames.size())),
                static_cast<int>(material->animation_frames.size()),
                range.trigger_phase ? false : material->animation_loop);
            const std::size_t frame_index = static_cast<std::size_t>(range.trigger_phase && range.trigger_reverse
                ? static_cast<int>(material->animation_frames.size()) - 1 - frame_cursor.frame
                : frame_cursor.frame);
            if (material->animation_frames[frame_index].valid()) selected_texture = &material->animation_frames[frame_index];
        }
        if (material && material->animation_frame_time_ms > 0 && material->animation_frame_count > 0) {
            const double timebase_hz = material->animation_timebase_hz > 0.0f
                ? static_cast<double>(material->animation_timebase_hz)
                : 1000.0 / static_cast<double>(material->animation_frame_time_ms);
            const double scroll_speed = material->water_animation
                ? static_cast<double>(scene_.water_scroll_speed)
                : 1.0;
            const double forward_raw_sample = doors::animationSample(
                range.trigger_phase,
                animation_time_ms * timebase_hz * scroll_speed / 1000.0,
                material->animation_frame_count);
            const double scroll_raw_sample = range.trigger_phase && range.trigger_reverse
                ? std::max(0.0, static_cast<double>(material->animation_frame_count - 1) - forward_raw_sample)
                : forward_raw_sample;
            const double wave_raw_sample = material->shoreline_animation
                ? waveSampleWithWait(
                    animation_time_ms,
                    timebase_hz,
                    material->shoreline_cycle_frame_count,
                    doors::animationLoops(range.trigger_phase, material->animation_loop),
                    static_cast<double>(scene_.water_wave_speed),
                    static_cast<double>(scene_.water_wave_wait_seconds))
                : scroll_raw_sample;
            const AnimationCursor scroll_cursor = animationCursor(
                scroll_raw_sample,
                material->animation_frame_count,
                doors::animationLoops(range.trigger_phase, material->animation_loop));
            const AnimationCursor wave_cursor = animationCursor(
                wave_raw_sample,
                material->animation_frame_count,
                doors::animationLoops(range.trigger_phase, material->animation_loop));
            if (!material->animation_uv_offsets.empty()) {
                const auto& scroll_offset = material->animation_uv_offsets[
                    static_cast<std::size_t>(scroll_cursor.frame) % material->animation_uv_offsets.size()];
                const auto& next_scroll_offset = material->animation_uv_offsets[
                    static_cast<std::size_t>(scroll_cursor.next_frame) % material->animation_uv_offsets.size()];
                const auto& wave_offset = material->animation_uv_offsets[
                    static_cast<std::size_t>(wave_cursor.frame) % material->animation_uv_offsets.size()];
                const auto& next_wave_offset = material->animation_uv_offsets[
                    static_cast<std::size_t>(wave_cursor.next_frame) % material->animation_uv_offsets.size()];
                const bool smooth_uv = !material->animation_step ||
                    (material->water_animation && scene_.water_smooth_uv_motion);
                const float scroll_amount = smooth_uv ? scroll_cursor.fraction : 0.0f;
                const float wave_amount = smooth_uv ? wave_cursor.fraction : 0.0f;
                uv_offset[0] = doors::animationUvOffset(range.trigger_phase, wrappedUvLerp(
                    scroll_offset[0], next_scroll_offset[0], scroll_amount, material->uv_wrap_period[0]));
                uv_offset[1] = doors::animationUvOffset(range.trigger_phase, wrappedUvLerp(
                    wave_offset[1], next_wave_offset[1], wave_amount, material->uv_wrap_period[1]));
            }
            if (!material->animation_image_keyframes.empty()) {
                const int image_frame_count = std::max(1, material->animation_image_frame_count);
                const int image_frame = wave_cursor.frame % image_frame_count;
                const MaterialGpuResource::ImageKeyframe* selected_keyframe =
                    &material->animation_image_keyframes.front();
                for (const MaterialGpuResource::ImageKeyframe& keyframe : material->animation_image_keyframes) {
                    if (keyframe.frame > image_frame) break;
                    selected_keyframe = &keyframe;
                }
                if (selected_keyframe->texture.valid()) selected_texture = &selected_keyframe->texture;
            }
        }
        const TextureGpuResource& texture = *selected_texture;
        const float cutoff = material
            ? std::max(material->alpha_cutoff, 1.0f / 255.0f)
            : 1.0f / 255.0f;
        const float tint[4] = {1.0f, 1.0f, 1.0f, cutoff};
        const float adjust[4] = {1.0f, 1.0f, 1.0f, 0.0f};
        const float texture_blur[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        const float light_dir[4] = {0.0f, 1.0f, 0.0f, 0.0f};
        const float light_params[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        bgfx::setTransform(model_matrix);
        if (bgfx::isValid(mesh.dynamic_vbh)) bgfx::setVertexBuffer(0, mesh.dynamic_vbh);
        else bgfx::setVertexBuffer(0, mesh.vbh);
        bgfx::setIndexBuffer(mesh.ibh, range.start_index, range.index_count);
        std::uint64_t texture_sampler_flags = material ? material->sampler_flags : samplerFlags();
        if (doors::clampsTextureEdges(range.trigger_phase)) {
            texture_sampler_flags |= BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        }
        bgfx::setTexture(0, tex_uniform_, texture.handle, texture_sampler_flags);
        bgfx::setUniform(tint_cutoff_uniform_, tint);
        bgfx::setUniform(color_adjust_uniform_, adjust);
        bgfx::setUniform(texture_blur_uniform_, texture_blur);
        bgfx::setUniform(uv_offset_uniform_, uv_offset);
        bgfx::setUniform(light_dir_uniform_, light_dir);
        bgfx::setUniform(light_params_uniform_, light_params);
        bgfx::setState(stateForDepthOnly());
        bgfx::submit(view_id, program);
    }
}

void OverworldBgfxRenderer::Impl::submitAquariumTankLightSpills(
    bgfx::ViewId view_id) const {
    if (!aquarium_tank_lighting_.enabled ||
        aquarium_tank_lighting_.spill_opacity <= 0.0f ||
        aquarium_tank_lighting_.spill_reach_tiles <= 0.0f ||
        aquarium_tank_lights_.empty() || !bgfx::isValid(world_program_)) {
        return;
    }

    constexpr std::uint16_t kSegments = 32;
    const std::uint32_t vertex_count =
        static_cast<std::uint32_t>(aquarium_tank_lights_.size()) * kSegments * 2U;
    const std::uint32_t index_count =
        static_cast<std::uint32_t>(aquarium_tank_lights_.size()) * kSegments * 6U;
    if (vertex_count > UINT16_MAX ||
        bgfx::getAvailTransientVertexBuffer(vertex_count, layout_) < vertex_count ||
        bgfx::getAvailTransientIndexBuffer(index_count) < index_count) {
        return;
    }

    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;
    if (!bgfx::allocTransientBuffers(&tvb, layout_, vertex_count, &tib, index_count)) {
        return;
    }

    auto* vertices = reinterpret_cast<Vertex*>(tvb.data);
    auto* indices = reinterpret_cast<std::uint16_t*>(tib.data);
    const float tile_size = std::max(1.0f, scene_.grid.tile_size);
    const float fallback_inner_round = tile_size * 0.10f;
    const float outer_round = aquarium_tank_lighting_.spill_reach_tiles * tile_size;
    const std::uint32_t inner_color = packAbgr(
        aquarium_tank_lighting_.spill_color[0],
        aquarium_tank_lighting_.spill_color[1],
        aquarium_tank_lighting_.spill_color[2],
        aquarium_tank_lighting_.spill_opacity);
    const std::uint32_t outer_color = packAbgr(
        aquarium_tank_lighting_.spill_color[0],
        aquarium_tank_lighting_.spill_color[1],
        aquarium_tank_lighting_.spill_color[2], 0.0f);
    constexpr float kPi = 3.14159265358979323846f;

    std::uint32_t vertex_cursor = 0;
    std::uint32_t index_cursor = 0;
    for (const auto& tank : aquarium_tank_lights_) {
        const float yaw = tank.yaw_degrees * kPi / 180.0f;
        const float yaw_cos = std::cos(yaw);
        const float yaw_sin = std::sin(yaw);
        const float half_width = std::max(0.0f, tank.half_width_world);
        const float half_depth = std::max(0.0f, tank.half_depth_world);
        const float inner_round = std::clamp(
            tank.light_corner_radius_world > 0.0f
                ? tank.light_corner_radius_world
                : fallback_inner_round,
            fallback_inner_round,
            std::max(fallback_inner_round, std::min(half_width, half_depth)));
        const float corner_center_x = std::max(0.0f, half_width - inner_round);
        const float corner_center_z = std::max(0.0f, half_depth - inner_round);
        const std::uint16_t base = static_cast<std::uint16_t>(vertex_cursor);
        for (std::uint16_t segment = 0; segment < kSegments; ++segment) {
            const float angle = 2.0f * kPi * static_cast<float>(segment) /
                static_cast<float>(kSegments);
            const float axis_x = std::cos(angle);
            const float axis_z = std::sin(angle);
            const float corner_x = std::copysign(corner_center_x, axis_x);
            const float corner_z = std::copysign(corner_center_z, axis_z);
            const auto write_vertex = [&](float radius, std::uint32_t color) {
                const float local_x = corner_x + axis_x * radius;
                const float local_z = corner_z + axis_z * radius;
                const float world_x = tank.world_center[0] + local_x * yaw_cos + local_z * yaw_sin;
                const float world_z = tank.world_center[2] - local_x * yaw_sin + local_z * yaw_cos;
                vertices[vertex_cursor++] = Vertex{
                    world_x, tank.floor_y_world + 0.08f, world_z,
                    color, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f};
            };
            write_vertex(inner_round, inner_color);
            write_vertex(inner_round + outer_round, outer_color);
        }
        for (std::uint16_t segment = 0; segment < kSegments; ++segment) {
            const std::uint16_t next = static_cast<std::uint16_t>((segment + 1U) % kSegments);
            const std::uint16_t inner = static_cast<std::uint16_t>(base + segment * 2U);
            const std::uint16_t outer = static_cast<std::uint16_t>(inner + 1U);
            const std::uint16_t next_inner = static_cast<std::uint16_t>(base + next * 2U);
            const std::uint16_t next_outer = static_cast<std::uint16_t>(next_inner + 1U);
            indices[index_cursor++] = inner;
            indices[index_cursor++] = outer;
            indices[index_cursor++] = next_outer;
            indices[index_cursor++] = inner;
            indices[index_cursor++] = next_outer;
            indices[index_cursor++] = next_inner;
        }
    }

    float model[16];
    identity(model);
    const float tint[4]{1.0f, 1.0f, 1.0f, 0.0f};
    const float adjust[4]{1.0f, 1.0f, 1.0f, 0.0f};
    const float zeros[4]{};
    const float light_dir[4]{0.0f, 1.0f, 0.0f, 0.0f};
    const float light_params[4]{1.0f, 0.0f, 0.0f, 0.0f};
    bgfx::setTransform(model);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setIndexBuffer(&tib);
    bgfx::setTexture(0, tex_uniform_, white_texture_.handle);
    bgfx::setUniform(tint_cutoff_uniform_, tint);
    bgfx::setUniform(color_adjust_uniform_, adjust);
    bgfx::setUniform(texture_blur_uniform_, zeros);
    bgfx::setUniform(uv_offset_uniform_, zeros);
    bgfx::setUniform(light_dir_uniform_, light_dir);
    bgfx::setUniform(light_params_uniform_, light_params);
    const float no_camera_clip[4]{};
    bgfx::setUniform(camera_clip_uniform_, no_camera_clip);
    bgfx::setState(
        BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS |
        BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE));
    bgfx::submit(view_id, world_program_);
}

void OverworldBgfxRenderer::Impl::submitPixelWorldToBackbuffer(
    int framebuffer_w,
    int framebuffer_h,
    int source_w,
    int source_h) const {
    if (!pixel_world_target_.valid()) return;
    const bgfx::TextureHandle texture = bgfx::getTexture(pixel_world_target_.frame_buffer, 0);
    if (!bgfx::isValid(texture)) return;

    framebuffer_w = std::max(1, framebuffer_w);
    framebuffer_h = std::max(1, framebuffer_h);
    source_w = std::max(1, source_w);
    source_h = std::max(1, source_h);
    const int integer_scale = std::max(1, std::min(framebuffer_w / source_w, framebuffer_h / source_h));
    const int dest_w = source_w * integer_scale;
    const int dest_h = source_h * integer_scale;
    const int dest_x = (framebuffer_w - dest_w) / 2;
    const int dest_y = (framebuffer_h - dest_h) / 2;

    float view[16];
    float proj[16];
    identity(view);
    bx::mtxOrtho(
        proj,
        0.0f,
        static_cast<float>(framebuffer_w),
        static_cast<float>(framebuffer_h),
        0.0f,
        0.0f,
        100.0f,
        0.0f,
        backend_.homogeneousDepth());
    bgfx::setViewTransform(3, view, proj);
    bgfx::setViewRect(
        3,
        0,
        0,
        static_cast<std::uint16_t>(framebuffer_w),
        static_cast<std::uint16_t>(framebuffer_h));
    bgfx::setViewFrameBuffer(3, BGFX_INVALID_HANDLE);
    bgfx::setViewClear(3, BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
    bgfx::setViewMode(3, bgfx::ViewMode::Sequential);

    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;
    if (!bgfx::allocTransientBuffers(&tvb, layout_, 4, &tib, 6)) {
        return;
    }

    const float x0 = static_cast<float>(dest_x);
    const float y0 = static_cast<float>(dest_y);
    const float x1 = static_cast<float>(dest_x + dest_w);
    const float y1 = static_cast<float>(dest_y + dest_h);
    const float v_top = backend_.originBottomLeft() ? 1.0f : 0.0f;
    const float v_bottom = backend_.originBottomLeft() ? 0.0f : 1.0f;
    auto* verts = reinterpret_cast<Vertex*>(tvb.data);
    verts[0] = Vertex{x0, y0, 0.0f, 0xffffffffu, 0.0f, v_top};
    verts[1] = Vertex{x1, y0, 0.0f, 0xffffffffu, 1.0f, v_top};
    verts[2] = Vertex{x1, y1, 0.0f, 0xffffffffu, 1.0f, v_bottom};
    verts[3] = Vertex{x0, y1, 0.0f, 0xffffffffu, 0.0f, v_bottom};
    auto* idx = reinterpret_cast<std::uint16_t*>(tib.data);
    idx[0] = 0;
    idx[1] = 1;
    idx[2] = 2;
    idx[3] = 0;
    idx[4] = 2;
    idx[5] = 3;

    float model[16];
    identity(model);
    const float tint[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    const float adjust[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    const float texture_blur[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float light_dir[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    const float light_params[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    bgfx::setTransform(model);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setIndexBuffer(&tib);
    bgfx::setTexture(0, tex_uniform_, texture, samplerFlags());
    bgfx::setUniform(tint_cutoff_uniform_, tint);
    bgfx::setUniform(color_adjust_uniform_, adjust);
    bgfx::setUniform(texture_blur_uniform_, texture_blur);
    bgfx::setUniform(uv_offset_uniform_, texture_blur);
    bgfx::setUniform(light_dir_uniform_, light_dir);
    bgfx::setUniform(light_params_uniform_, light_params);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(3, world_program_);
}

void OverworldBgfxRenderer::Impl::submitOverlaySlice(
    bgfx::ViewId view_id,
    bgfx::TextureHandle texture,
    int texture_w,
    int texture_h,
    const SDL_Rect& src,
    const SDL_Rect& dst) const {
    if (!bgfx::isValid(texture) || texture_w <= 0 || texture_h <= 0 ||
        src.w <= 0 || src.h <= 0 || dst.w <= 0 || dst.h <= 0) {
        return;
    }

    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;
    if (!bgfx::allocTransientBuffers(&tvb, layout_, 4, &tib, 6)) {
        return;
    }

    const float x0 = static_cast<float>(dst.x);
    const float y0 = static_cast<float>(dst.y);
    const float x1 = static_cast<float>(dst.x + dst.w);
    const float y1 = static_cast<float>(dst.y + dst.h);
    const float u0 = static_cast<float>(src.x) / static_cast<float>(texture_w);
    const float u1 = static_cast<float>(src.x + src.w) / static_cast<float>(texture_w);
    const float raw_v0 = static_cast<float>(src.y) / static_cast<float>(texture_h);
    const float raw_v1 = static_cast<float>(src.y + src.h) / static_cast<float>(texture_h);
    const float v_top = backend_.originBottomLeft() ? raw_v1 : raw_v0;
    const float v_bottom = backend_.originBottomLeft() ? raw_v0 : raw_v1;

    auto* verts = reinterpret_cast<Vertex*>(tvb.data);
    verts[0] = Vertex{x0, y0, 0.0f, 0xffffffffu, u0, v_top, 0.0f, 1.0f, 0.0f};
    verts[1] = Vertex{x1, y0, 0.0f, 0xffffffffu, u1, v_top, 0.0f, 1.0f, 0.0f};
    verts[2] = Vertex{x1, y1, 0.0f, 0xffffffffu, u1, v_bottom, 0.0f, 1.0f, 0.0f};
    verts[3] = Vertex{x0, y1, 0.0f, 0xffffffffu, u0, v_bottom, 0.0f, 1.0f, 0.0f};
    auto* idx = reinterpret_cast<std::uint16_t*>(tib.data);
    const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    std::copy(std::begin(indices), std::end(indices), idx);

    float model[16];
    identity(model);
    const float tint[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    const float adjust[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    const float texture_blur[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float light_dir[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    const float light_params[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    bgfx::setTransform(model);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setIndexBuffer(&tib);
    bgfx::setTexture(0, tex_uniform_, texture, samplerFlags());
    bgfx::setUniform(tint_cutoff_uniform_, tint);
    bgfx::setUniform(color_adjust_uniform_, adjust);
    bgfx::setUniform(texture_blur_uniform_, texture_blur);
    bgfx::setUniform(uv_offset_uniform_, texture_blur);
    bgfx::setUniform(light_dir_uniform_, light_dir);
    bgfx::setUniform(light_params_uniform_, light_params);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
    bgfx::submit(view_id, world_program_);
}

void OverworldBgfxRenderer::Impl::submitTextboxOverlay(
    int framebuffer_w,
    int framebuffer_h,
    const SDL_Rect& viewport_dst,
    int base_viewport_w,
    int base_viewport_h) const {
    if (!textbox_overlay_visible_ || !textbox_texture_.valid() || !dialogue::overworldTextboxEnabled(textbox_config_)) {
        return;
    }
    const OverlayThreeSliceLayout layout = buildOverlayThreeSliceLayout(
        OverlayThreeSliceConfig{
            textbox_config_.selected_skin_index,
            textbox_config_.valid_skin_count,
            textbox_config_.source_cell_width_px,
            textbox_config_.source_cell_height_px,
            textbox_config_.sheet_columns,
            textbox_config_.side_padding_px,
            textbox_config_.bottom_padding_px,
            textbox_config_.stretch_strip_width_px,
            textbox_config_.stretch_strip_center_x_px},
        base_viewport_w,
        base_viewport_h,
        textbox_texture_.width,
        textbox_texture_.height);
    if (!layout.visible) {
        return;
    }

    float view[16];
    float proj[16];
    identity(view);
    bx::mtxOrtho(
        proj,
        0.0f,
        static_cast<float>(std::max(1, framebuffer_w)),
        static_cast<float>(std::max(1, framebuffer_h)),
        0.0f,
        0.0f,
        100.0f,
        0.0f,
        backend_.homogeneousDepth());
    constexpr bgfx::ViewId kTextboxView = 4;
    bgfx::setViewTransform(kTextboxView, view, proj);
    bgfx::setViewRect(
        kTextboxView,
        0,
        0,
        static_cast<std::uint16_t>(std::max(1, framebuffer_w)),
        static_cast<std::uint16_t>(std::max(1, framebuffer_h)));
    bgfx::setViewFrameBuffer(kTextboxView, BGFX_INVALID_HANDLE);
    // View state is process-global in bgfx and survives the Attend renderer.
    // Attend uses view 4 as its backbuffer compositor with a black clear, while
    // the overworld reuses view 4 for the textbox. Always remove that inherited
    // clear or opening post-Attend dialogue erases the world behind the box.
    bgfx::setViewClear(kTextboxView, BGFX_CLEAR_NONE);
    bgfx::setViewMode(kTextboxView, bgfx::ViewMode::Sequential);

    submitOverlaySlice(
        kTextboxView,
        textbox_texture_.handle,
        textbox_texture_.width,
        textbox_texture_.height,
        layout.left_src,
        scaleOverlayRect(layout.left_dst, viewport_dst, base_viewport_w, base_viewport_h));
    submitOverlaySlice(
        kTextboxView,
        textbox_texture_.handle,
        textbox_texture_.width,
        textbox_texture_.height,
        layout.middle_src,
        scaleOverlayRect(layout.middle_dst, viewport_dst, base_viewport_w, base_viewport_h));
    submitOverlaySlice(
        kTextboxView,
        textbox_texture_.handle,
        textbox_texture_.width,
        textbox_texture_.height,
        layout.right_src,
        scaleOverlayRect(layout.right_dst, viewport_dst, base_viewport_w, base_viewport_h));

    if (textbox_text_texture_.valid()) {
        const SDL_Rect text_src{0, 0, textbox_text_texture_.width, textbox_text_texture_.height};
        const SDL_Rect text_base{
            textbox_config_.text_left_inset_px,
            layout.left_dst.y + textbox_config_.text_top_inset_px,
            textbox_text_texture_.width,
            textbox_text_texture_.height};
        submitOverlaySlice(
            kTextboxView,
            textbox_text_texture_.handle,
            textbox_text_texture_.width,
            textbox_text_texture_.height,
            text_src,
            scaleOverlayRect(text_base, viewport_dst, base_viewport_w, base_viewport_h));
    }
}

void OverworldBgfxRenderer::Impl::submitAttendButtonOverlay(
    int framebuffer_w, int framebuffer_h, int logical_w, int logical_h) const {
    if (!attend_button_visible_ || !attend_button_texture_.valid()) return;
    float view[16];
    float proj[16];
    identity(view);
    bx::mtxOrtho(proj, 0.0f, static_cast<float>(std::max(1, framebuffer_w)),
        static_cast<float>(std::max(1, framebuffer_h)), 0.0f, 0.0f, 100.0f, 0.0f,
        backend_.homogeneousDepth());
    constexpr bgfx::ViewId kAttendButtonView = 5;
    bgfx::setViewTransform(kAttendButtonView, view, proj);
    bgfx::setViewRect(kAttendButtonView, 0, 0,
        static_cast<std::uint16_t>(std::max(1, framebuffer_w)),
        static_cast<std::uint16_t>(std::max(1, framebuffer_h)));
    bgfx::setViewFrameBuffer(kAttendButtonView, BGFX_INVALID_HANDLE);
    bgfx::setViewClear(kAttendButtonView, BGFX_CLEAR_NONE);
    bgfx::setViewMode(kAttendButtonView, bgfx::ViewMode::Sequential);
    const SDL_Rect src{0, 0, attend_button_texture_.width, attend_button_texture_.height};
    const SDL_Rect dst{
        attend_button_logical_rect_.x * framebuffer_w / std::max(1, logical_w),
        attend_button_logical_rect_.y * framebuffer_h / std::max(1, logical_h),
        attend_button_logical_rect_.w * framebuffer_w / std::max(1, logical_w),
        attend_button_logical_rect_.h * framebuffer_h / std::max(1, logical_h)};
    submitOverlaySlice(kAttendButtonView, attend_button_texture_.handle,
        attend_button_texture_.width, attend_button_texture_.height, src, dst);
}

void OverworldBgfxRenderer::Impl::submitBlackIrisTransition(
    int framebuffer_w, int framebuffer_h, int logical_w, int logical_h) const {
    if (!iris_visible_ || !white_texture_.valid()) return;
    const float cx = iris_logical_x_ * framebuffer_w / std::max(1, logical_w);
    const float cy = iris_logical_y_ * framebuffer_h / std::max(1, logical_h);
    const float diagonal = std::hypot(static_cast<float>(framebuffer_w), static_cast<float>(framebuffer_h));
    const float inner = diagonal * iris_max_radius_scale_ * (1.0f - iris_closed_amount_);
    const float outer = diagonal * 2.5f;
    const int segments = iris_segments_;
    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;
    if (!bgfx::allocTransientBuffers(&tvb, layout_, static_cast<std::uint32_t>(segments * 4),
            &tib, static_cast<std::uint32_t>(segments * 6))) return;
    auto* vertices = reinterpret_cast<Vertex*>(tvb.data);
    auto* indices = reinterpret_cast<std::uint16_t*>(tib.data);
    for (int i = 0; i < segments; ++i) {
        const float a0 = 2.0f * bx::kPi * i / segments;
        const float a1 = 2.0f * bx::kPi * (i + 1) / segments;
        const int v = i * 4;
        vertices[v + 0] = Vertex{cx + std::cos(a0) * inner, cy + std::sin(a0) * inner, 0, 0xff000000u, 0, 0, 0, 1, 0};
        vertices[v + 1] = Vertex{cx + std::cos(a1) * inner, cy + std::sin(a1) * inner, 0, 0xff000000u, 0, 0, 0, 1, 0};
        vertices[v + 2] = Vertex{cx + std::cos(a1) * outer, cy + std::sin(a1) * outer, 0, 0xff000000u, 0, 0, 0, 1, 0};
        vertices[v + 3] = Vertex{cx + std::cos(a0) * outer, cy + std::sin(a0) * outer, 0, 0xff000000u, 0, 0, 0, 1, 0};
        const int k = i * 6;
        indices[k+0]=v; indices[k+1]=v+1; indices[k+2]=v+2;
        indices[k+3]=v; indices[k+4]=v+2; indices[k+5]=v+3;
    }
    float view[16], proj[16], model[16]; identity(view); identity(model);
    bx::mtxOrtho(proj, 0, static_cast<float>(framebuffer_w), static_cast<float>(framebuffer_h), 0,
        0, 100, 0, backend_.homogeneousDepth());
    constexpr bgfx::ViewId view_id = 7;
    bgfx::setViewTransform(view_id, view, proj);
    bgfx::setViewRect(view_id, 0, 0, static_cast<uint16_t>(framebuffer_w), static_cast<uint16_t>(framebuffer_h));
    bgfx::setViewFrameBuffer(view_id, BGFX_INVALID_HANDLE);
    bgfx::setViewClear(view_id, BGFX_CLEAR_NONE);
    bgfx::setTransform(model); bgfx::setVertexBuffer(0, &tvb); bgfx::setIndexBuffer(&tib);
    bgfx::setTexture(0, tex_uniform_, white_texture_.handle, samplerFlags());
    const float tint[4]={1,1,1,0}, adjust[4]={1,1,1,0}, zero[4]={0,0,0,0}, light[4]={0,1,0,0}, params[4]={1,0,0,0};
    bgfx::setUniform(tint_cutoff_uniform_, tint); bgfx::setUniform(color_adjust_uniform_, adjust);
    bgfx::setUniform(texture_blur_uniform_, zero); bgfx::setUniform(uv_offset_uniform_, zero);
    bgfx::setUniform(light_dir_uniform_, light); bgfx::setUniform(light_params_uniform_, params);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A); bgfx::submit(view_id, world_program_);
}

void OverworldBgfxRenderer::Impl::appendProjectedShadowForScene(
    const SceneConfig& scene,
    float origin_x,
    float origin_y,
    float origin_z,
    const rendering::BillboardPlacement& placement,
    const camera::Vec3& shadow_right,
    const camera::Vec3& shadow_forward,
    const camera::Vec3& view_bias,
    float half_w,
    float half_h,
    std::vector<Vertex>& vertices,
    std::vector<std::uint16_t>& indices) const {
    const float tile_size = std::max(1.0f, scene.grid.tile_size);
    const float world_min_x = placement.shadow_ground.x - half_w;
    const float world_max_x = placement.shadow_ground.x + half_w;
    const float world_min_z = placement.shadow_ground.z - half_h;
    const float world_max_z = placement.shadow_ground.z + half_h;
    const float local_min_x = world_min_x - origin_x;
    const float local_max_x = world_max_x - origin_x;
    const float local_min_z = world_min_z - origin_z;
    const float local_max_z = world_max_z - origin_z;
    const int min_tx = static_cast<int>(std::floor(local_min_x / tile_size));
    const int max_tx = static_cast<int>(std::floor(local_max_x / tile_size));
    const int min_ty = static_cast<int>(std::floor(local_min_z / tile_size));
    const int max_ty = static_cast<int>(std::floor(local_max_z / tile_size));
    const int grid_w = std::max(1, scene.grid.width);
    const int grid_h = std::max(1, scene.grid.height);
    const float y_bias = std::max(0.04f, tile_size * 0.003f);
    const std::uint32_t white = 0xffffffffu;

    const auto rendered_tile_height = [&](float local_x, float local_z, int sample_tx, int sample_ty) {
        float corners[4]{};
        terrain::fillTileCornerHeights(scene, sample_tx, sample_ty, corners);
        const float u = std::clamp((local_x - (static_cast<float>(sample_tx) * tile_size)) / tile_size, 0.0f, 1.0f);
        const float v = std::clamp((local_z - (static_cast<float>(sample_ty) * tile_size)) / tile_size, 0.0f, 1.0f);

        // Terrain tops are rendered as two triangles with the NW→SE diagonal:
        //   NW, NE, SE and NW, SE, SW.
        // Shadow decals must use the same piecewise-linear surface, not the smoother
        // bilinear movement surface, or opposite corner ramps can depth-hide the decal.
        if (v <= u) {
            const float nw = corners[0] + ((corners[1] - corners[0]) * u);
            const float se = corners[3] + ((corners[2] - corners[3]) * u);
            return nw + ((se - nw) * v);
        }
        const float nw = corners[0] + ((corners[3] - corners[0]) * v);
        const float se = corners[1] + ((corners[2] - corners[1]) * v);
        return nw + ((se - nw) * u);
    };

    const auto append_vertex = [&](float local_x, float local_z, int sample_tx, int sample_ty) {
        const float world_x = origin_x + local_x;
        const float world_z = origin_z + local_z;
        const float y = origin_y + rendered_tile_height(local_x, local_z, sample_tx, sample_ty) + y_bias;
        const float dx = world_x - placement.shadow_ground.x;
        const float dz = world_z - placement.shadow_ground.z;
        const float u = 0.5f + (((dx * shadow_right.x) + (dz * shadow_right.z)) / std::max(0.001f, half_w * 2.0f));
        const float v = 0.5f + (((dx * shadow_forward.x) + (dz * shadow_forward.z)) / std::max(0.001f, half_h * 2.0f));
        vertices.push_back(Vertex{
            world_x + view_bias.x,
            y + view_bias.y,
            world_z + view_bias.z,
            white,
            u,
            v});
    };

    for (int ty = std::max(0, min_ty); ty <= std::min(grid_h - 1, max_ty); ++ty) {
        for (int tx = std::max(0, min_tx); tx <= std::min(grid_w - 1, max_tx); ++tx) {
            const float x0 = std::max(static_cast<float>(tx) * tile_size, local_min_x);
            const float x1 = std::min((static_cast<float>(tx) + 1.0f) * tile_size, local_max_x);
            const float z0 = std::max(static_cast<float>(ty) * tile_size, local_min_z);
            const float z1 = std::min((static_cast<float>(ty) + 1.0f) * tile_size, local_max_z);
            if (x1 <= x0 || z1 <= z0) {
                continue;
            }
            const std::uint16_t base = static_cast<std::uint16_t>(vertices.size());
            append_vertex(x0, z0, tx, ty);
            append_vertex(x1, z0, tx, ty);
            append_vertex(x1, z1, tx, ty);
            append_vertex(x0, z1, tx, ty);
            indices.insert(indices.end(), {
                base,
                static_cast<std::uint16_t>(base + 1U),
                static_cast<std::uint16_t>(base + 2U),
                base,
                static_cast<std::uint16_t>(base + 2U),
                static_cast<std::uint16_t>(base + 3U)});
        }
    }
}

void OverworldBgfxRenderer::Impl::submitProjectedCharacterShadows(
    const camera::Gen4FollowCamera& camera,
    const std::vector<rendering::CharacterBillboardDraw>& characters) const {
    if (!shadow_texture_.valid() || characters.empty()) {
        return;
    }

    std::vector<Vertex> vertices;
    std::vector<std::uint16_t> indices;
    vertices.reserve(characters.size() * 24U);
    indices.reserve(characters.size() * 36U);
    const auto pose = camera.pose();
    const float decal_depth_bias = std::max(0.08f, scene_.grid.tile_size * 0.006f);
    const camera::Vec3 view_bias{
        -pose.forward.x * decal_depth_bias,
        -pose.forward.y * decal_depth_bias,
        -pose.forward.z * decal_depth_bias};

    for (const rendering::CharacterBillboardDraw& character : characters) {
        if (!character.draw_shadow || !character.placement.visible) {
            continue;
        }
        vertices.clear();
        indices.clear();
        float half_w = std::max(0.5f, character.placement.world_w * scene_.sprite_shadow.radius_x_tiles);
        float half_h = std::max(0.5f, character.placement.world_h * scene_.sprite_shadow.radius_z_tiles);
        if (scene_.sprite_shadow.pixel_coherent) {
            const float base_sprite_h =
                authoredPixelsWorldUnits(scene_, static_cast<float>(std::max(1, character.source_rect.h)), 1.0f);
            const float sprite_scale = character.placement.world_h / std::max(0.001f, base_sprite_h);
            half_w = authoredPixelsWorldUnits(
                scene_,
                static_cast<float>(std::max(1, scene_.sprite_shadow.texture_width_px)),
                sprite_scale) * 0.5f;
            half_h = authoredPixelsWorldUnits(
                scene_,
                static_cast<float>(std::max(1, scene_.sprite_shadow.texture_height_px)),
                sprite_scale) * 0.5f;
        }
        const camera::Vec3 shadow_right{1.0f, 0.0f, 0.0f};
        const camera::Vec3 shadow_forward{0.0f, 0.0f, 1.0f};
        appendProjectedShadowForScene(
            scene_,
            0.0f,
            0.0f,
            0.0f,
            character.placement,
            shadow_right,
            shadow_forward,
            view_bias,
            half_w,
            half_h,
            vertices,
            indices);
        for (const StaticChunkGpuResource& chunk : static_chunks_) {
            appendProjectedShadowForScene(
                chunk.scene,
                chunk.origin_x,
                chunk.origin_y,
                chunk.origin_z,
                character.placement,
                shadow_right,
                shadow_forward,
                view_bias,
                half_w,
                half_h,
                vertices,
                indices);
        }

        if (vertices.empty() || indices.empty()) {
            continue;
        }
        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;
        if (!bgfx::allocTransientBuffers(
                &tvb,
                layout_,
                static_cast<std::uint32_t>(vertices.size()),
                &tib,
                static_cast<std::uint32_t>(indices.size()))) {
            return;
        }
        std::memcpy(tvb.data, vertices.data(), vertices.size() * sizeof(Vertex));
        std::memcpy(tib.data, indices.data(), indices.size() * sizeof(std::uint16_t));

        const float br = std::max(0.0f, scene_.lighting_brightness);
        float tint[4] = {
            scene_.lighting_tint_r * br,
            scene_.lighting_tint_g * br,
            scene_.lighting_tint_b * br,
            1.0f / 255.0f};
        float model[16];
        identity(model);
        const float adjust[4] = {1.0f, 1.0f, 1.0f, 0.0f};
        const float texture_blur[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        const float light_dir[4] = {0.0f, 1.0f, 0.0f, 0.0f};
        const float light_params[4] = {1.0f, 0.0f, 0.0f, 0.0f};

        bgfx::setTransform(model);
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setIndexBuffer(&tib);
        bgfx::setTexture(0, tex_uniform_, shadow_texture_.handle, samplerFlags());
        bgfx::setUniform(tint_cutoff_uniform_, tint);
        bgfx::setUniform(color_adjust_uniform_, adjust);
        bgfx::setUniform(texture_blur_uniform_, texture_blur);
        bgfx::setUniform(uv_offset_uniform_, texture_blur);
        bgfx::setUniform(light_dir_uniform_, light_dir);
        bgfx::setUniform(light_params_uniform_, light_params);
        bgfx::setStencil(shadowStencilTestOnly());
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA | BGFX_STATE_DEPTH_TEST_LEQUAL);
        bgfx::submit(1, world_program_);

        bgfx::setTransform(model);
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setIndexBuffer(&tib);
        bgfx::setTexture(0, tex_uniform_, shadow_texture_.handle, samplerFlags());
        bgfx::setUniform(tint_cutoff_uniform_, tint);
        bgfx::setUniform(color_adjust_uniform_, adjust);
        bgfx::setUniform(texture_blur_uniform_, texture_blur);
        bgfx::setUniform(uv_offset_uniform_, texture_blur);
        bgfx::setUniform(light_dir_uniform_, light_dir);
        bgfx::setUniform(light_params_uniform_, light_params);
        bgfx::setStencil(shadowStencilMark());
        bgfx::setState(BGFX_STATE_DEPTH_TEST_LEQUAL);
        bgfx::submit(1, world_program_);
    }
}

void OverworldBgfxRenderer::Impl::refreshBillboardDrawer() {
    auto copyTexture = [](const TextureGpuResource& src) {
        BillboardBgfxDrawer::TextureGpuResource out{};
        out.handle = src.handle;
        out.width = src.width;
        out.height = src.height;
        return out;
    };

    BillboardBgfxDrawer::Dependencies deps{};
    deps.layout = layout_;
    deps.billboard_program = billboard_program_;
    deps.tex_uniform = tex_uniform_;
    deps.tint_cutoff_uniform = tint_cutoff_uniform_;
    deps.color_adjust_uniform = color_adjust_uniform_;
    deps.texture_blur_uniform = texture_blur_uniform_;
    deps.uv_offset_uniform = uv_offset_uniform_;
    deps.light_dir_uniform = light_dir_uniform_;
    deps.light_params_uniform = light_params_uniform_;
    deps.view_id = 1;
    deps.scene = &scene_;
    deps.textures_for_character = [this, copyTexture](const CharacterSpriteDefinition& character) {
        const CharacterGpuTextures& src = texturesForCharacter(character);
        BillboardBgfxDrawer::CharacterGpuTextures out{};
        out.color = copyTexture(src.color);
        out.white = copyTexture(src.white);
        out.run_color = copyTexture(src.run_color);
        out.run_white = copyTexture(src.run_white);
        for (const auto& [activity_id, texture] : src.activity_color) {
            out.activity_color[activity_id] = copyTexture(texture);
        }
        for (const auto& [activity_id, texture] : src.activity_white) {
            out.activity_white[activity_id] = copyTexture(texture);
        }
        return out;
    };
    deps.texture_for_key = [this, copyTexture](
                                const std::string& cache_key,
                                const std::vector<std::uint8_t>& png_bytes,
                                const std::string& fallback_path,
                                const char* debug_name) {
        return copyTexture(textureForKey(cache_key, png_bytes, fallback_path, debug_name));
    };
    billboard_drawer_.emplace(std::move(deps));
}

void OverworldBgfxRenderer::Impl::render(
    const camera::Gen4FollowCamera& camera,
    const camera::Vec3& player_pos,
    const SDL_Rect& player_source_rect,
    const std::string& player_activity_id,
    bool player_use_run_texture,
    bool player_draw_shadow,
    const terrain::ActorTerrainBinding& player_binding,
    int logical_w,
    int logical_h,
    int framebuffer_w,
    int framebuffer_h,
    const std::vector<rendering::CharacterBillboardDraw>& character_draws,
    const std::vector<rendering::TextureBillboardDraw>& texture_draws,
    const std::string& debug_frame_counter_label) {
    (void)renderInternal(
        camera,
        player_pos,
        player_source_rect,
        player_activity_id,
        player_use_run_texture,
        player_draw_shadow,
        player_binding,
        logical_w,
        logical_h,
        framebuffer_w,
        framebuffer_h,
        character_draws,
        texture_draws,
        debug_frame_counter_label,
        RenderOptions{});
}

OverworldBgfxRenderer::EmbeddedViewportTexture
OverworldBgfxRenderer::Impl::renderEmbeddedViewport(
    const camera::Gen4FollowCamera& camera,
    const camera::Vec3& player_pos,
    const SDL_Rect& player_source_rect,
    const std::string& player_activity_id,
    bool player_use_run_texture,
    bool player_draw_shadow,
    const terrain::ActorTerrainBinding& player_binding,
    int logical_w,
    int logical_h,
    int framebuffer_w,
    int framebuffer_h,
    const std::vector<rendering::CharacterBillboardDraw>& character_draws,
    const std::vector<rendering::TextureBillboardDraw>& texture_draws,
    const OverworldBgfxRenderer::EmbeddedViewportOptions& options) {
    RenderOptions render_options;
    render_options.embedded_viewport = true;
    render_options.override_animation_clock = true;
    render_options.animations_enabled = options.animations_enabled;
    render_options.animation_time_seconds = options.animation_time_seconds;
    return renderInternal(
        camera,
        player_pos,
        player_source_rect,
        player_activity_id,
        player_use_run_texture,
        player_draw_shadow,
        player_binding,
        logical_w,
        logical_h,
        framebuffer_w,
        framebuffer_h,
        character_draws,
        texture_draws,
        {},
        render_options);
}

OverworldBgfxRenderer::EmbeddedViewportTexture OverworldBgfxRenderer::Impl::renderInternal(
    const camera::Gen4FollowCamera& camera,
    const camera::Vec3& player_pos,
    const SDL_Rect& player_source_rect,
    const std::string& player_activity_id,
    bool player_use_run_texture,
    bool player_draw_shadow,
    const terrain::ActorTerrainBinding& player_binding,
    int logical_w,
    int logical_h,
    int framebuffer_w,
    int framebuffer_h,
    const std::vector<rendering::CharacterBillboardDraw>& character_draws,
    const std::vector<rendering::TextureBillboardDraw>& texture_draws,
    const std::string& debug_frame_counter_label,
    const RenderOptions& options) {
    if (!valid()) return {};
    player_aquarium_renderer_.advanceFrame();
    backend_.reset(framebuffer_w, framebuffer_h);
    const int base_w = rendering::worldViewportBaseWidth(scene_);
    const int base_h = rendering::worldViewportBaseHeight(scene_);
    const int internal_scale = rendering::worldViewportInternalScale(scene_);
    const int render_w = rendering::worldViewportRenderWidth(scene_);
    const int render_h = rendering::worldViewportRenderHeight(scene_);
    bool pixel_world_enabled = scene_.world_viewport.enabled || options.embedded_viewport;
    if (pixel_world_enabled && !ensurePixelWorldTarget(render_w, render_h)) {
        if (options.embedded_viewport) {
            return {};
        }
        std::cerr << "[OverworldBgfx] Pixel-perfect target unavailable, rendering direct: "
                  << last_error_ << std::endl;
        pixel_world_enabled = false;
    }
    if (!pixel_world_enabled) {
        pixel_world_target_.destroy();
    }
    const int world_view_w = pixel_world_enabled ? render_w : std::max(1, framebuffer_w);
    const int world_view_h = pixel_world_enabled ? render_h : std::max(1, framebuffer_h);
    const int placement_w = pixel_world_enabled ? base_w : std::max(1, logical_w);
    const int placement_h = pixel_world_enabled ? base_h : std::max(1, logical_h);
    bgfx::FrameBufferHandle world_frame_buffer = BGFX_INVALID_HANDLE;
    if (pixel_world_enabled) {
        world_frame_buffer = pixel_world_target_.frame_buffer;
    }

    const TerrainColor clear = scene_.environment.clear_color;
    if (!options.embedded_viewport) {
        backend_.beginFrame(
            static_cast<float>(clear.r) / 255.0f,
            static_cast<float>(clear.g) / 255.0f,
            static_cast<float>(clear.b) / 255.0f,
            static_cast<float>(clear.a) / 255.0f);
    }

    const auto pose = camera.pose();
    float view[16];
    float proj[16];
    cameraViewMatrix(pose, view);
    const float aspect =
        static_cast<float>(std::max(1, placement_w)) / static_cast<float>(std::max(1, placement_h));
    bx::mtxProj(
        proj,
        pose.preset.fov_y_deg,
        aspect,
        pose.preset.near_clip,
        pose.preset.far_clip,
        backend_.homogeneousDepth());
    bgfx::setViewTransform(0, view, proj);
    bgfx::setViewFrameBuffer(0, world_frame_buffer);
    bgfx::setViewRect(
        0,
        0,
        0,
        static_cast<std::uint16_t>(world_view_w),
        static_cast<std::uint16_t>(world_view_h));
    const std::uint32_t clear_rgba =
        (static_cast<std::uint32_t>(clear.r) << 24U) |
        (static_cast<std::uint32_t>(clear.g) << 16U) |
        (static_cast<std::uint32_t>(clear.b) << 8U) |
        static_cast<std::uint32_t>(clear.a);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, clear_rgba, 1.0f, 0);
    if (options.embedded_viewport) {
        // beginFrame() normally guarantees a view-0 clear by touching it. The
        // embedding host owns frame boundaries, so keep that guarantee local.
        bgfx::touch(0);
    }
    bgfx::setViewTransform(1, view, proj);
    bgfx::setViewFrameBuffer(1, world_frame_buffer);
    bgfx::setViewRect(
        1,
        0,
        0,
        static_cast<std::uint16_t>(world_view_w),
        static_cast<std::uint16_t>(world_view_h));
    bgfx::setViewClear(1, BGFX_CLEAR_STENCIL, 0, 1.0f, 0);
    bgfx::setViewMode(1, bgfx::ViewMode::Sequential);

    const bool previous_override_animation_clock = override_animation_clock_;
    const bool previous_animations_enabled = animations_enabled_;
    const double previous_animation_time_seconds = animation_time_seconds_;
    override_animation_clock_ = options.override_animation_clock;
    animations_enabled_ = options.animations_enabled;
    animation_time_seconds_ = options.animation_time_seconds;

    float ident[16];
    identity(ident);
    for (ModelGpuResource& model : models_) updateModelAnimation(model);
    updateDoorTileAnimations(tile_layer_mesh_);
    for (StaticChunkGpuResource& chunk : static_chunks_) {
        for (ModelGpuResource& model : chunk.models) updateModelAnimation(model);
        updateDoorTileAnimations(chunk.tile_layer_mesh);
    }
    submitMesh(terrain_flat_top_mesh_, ident, world_program_, MaterialClass::Opaque, 0.0f, stateFor(MaterialClass::Opaque));
    submitMesh(terrain_slope_top_mesh_, ident, world_program_, MaterialClass::Opaque, 0.0f, stateFor(MaterialClass::Opaque));
    submitMesh(
        terrain_wall_mesh_, ident, wall_clip_program_, MaterialClass::Opaque, 0.0f,
        stateFor(MaterialClass::Opaque), 0, false, interior_wall_clip_);
    submitMesh(tile_layer_mesh_, ident, world_program_, MaterialClass::Opaque, 0.0f, stateFor(MaterialClass::Opaque));
    for (const ModelGpuResource& model : models_) {
        submitMesh(model.mesh, model.model_matrix, world_program_, MaterialClass::Opaque, 0.0f,
            stateFor(MaterialClass::Opaque), 0, false, nullptr, model.aquarium_tank_lit);
    }
    for (const StaticChunkGpuResource& chunk : static_chunks_) {
        submitMesh(chunk.terrain_flat_top_mesh, chunk.world_matrix, world_program_, MaterialClass::Opaque, 0.0f, stateFor(MaterialClass::Opaque));
        submitMesh(chunk.terrain_slope_top_mesh, chunk.world_matrix, world_program_, MaterialClass::Opaque, 0.0f, stateFor(MaterialClass::Opaque));
        submitMesh(chunk.terrain_wall_mesh, chunk.world_matrix, world_program_, MaterialClass::Opaque, 0.0f, stateFor(MaterialClass::Opaque));
        submitMesh(chunk.tile_layer_mesh, chunk.world_matrix, world_program_, MaterialClass::Opaque, 0.0f, stateFor(MaterialClass::Opaque));
        for (const ModelGpuResource& model : chunk.models) {
            submitMesh(model.mesh, model.model_matrix, world_program_, MaterialClass::Opaque, 0.0f, stateFor(MaterialClass::Opaque));
        }
    }
    player_aquarium_renderer_.submitOpaque(1);
    submitAquariumTankLightSpills(1);
    submitMesh(tile_layer_mesh_, ident, world_program_, MaterialClass::MaskCutout, 0.5f, stateFor(MaterialClass::MaskCutout));
    for (const ModelGpuResource& model : models_) {
        submitMesh(model.mesh, model.model_matrix, world_program_, MaterialClass::MaskCutout, 0.5f,
            stateFor(MaterialClass::MaskCutout), 0, false, nullptr, model.aquarium_tank_lit);
    }
    for (const StaticChunkGpuResource& chunk : static_chunks_) {
        submitMesh(chunk.tile_layer_mesh, chunk.world_matrix, world_program_, MaterialClass::MaskCutout, 0.5f, stateFor(MaterialClass::MaskCutout));
        for (const ModelGpuResource& model : chunk.models) {
            submitMesh(model.mesh, model.model_matrix, world_program_, MaterialClass::MaskCutout, 0.5f, stateFor(MaterialClass::MaskCutout));
        }
    }

    // Aquarium actors are real skinned Attend models. Player-built glass/water
    // is submitted with the other late transparent geometry below.
    aquarium_pokemon_renderer_.submit(1, false);
    aquarium_pokemon_renderer_.submit(1, true);

    // RAE material-motion tiles may carry a source display-list order that
    // crosses opaque/cutout/blend classes (notably Gen 5 shoreline layers).
    // Submit those ranges once, in their stable renderOrder, on the sequential
    // overlay view instead of regrouping them by alpha class.
    submitMesh(
        tile_layer_mesh_, ident, world_program_, MaterialClass::Opaque, 0.0f,
        stateFor(MaterialClass::Opaque), 1, true);
    for (const StaticChunkGpuResource& chunk : static_chunks_) {
        submitMesh(
            chunk.tile_layer_mesh, chunk.world_matrix, world_program_, MaterialClass::Opaque, 0.0f,
            stateFor(MaterialClass::Opaque), 1, true);
    }

    // Ground overlays must be part of the floor before character shadows are composited.
    // If blended RTPKS/tile materials draw after shadows, they cover the shadow on ramps
    // and make it look clipped even when the shadow position/depth is correct.
    submitMesh(tile_layer_mesh_, ident, world_program_, MaterialClass::TrueBlend, 0.0f, stateFor(MaterialClass::TrueBlend), 1);
    for (const StaticChunkGpuResource& chunk : static_chunks_) {
        submitMesh(chunk.tile_layer_mesh, chunk.world_matrix, world_program_, MaterialClass::TrueBlend, 0.0f, stateFor(MaterialClass::TrueBlend), 1);
    }
    aquarium_construction_renderer_.submitWorld(1);

    bgfx::touch(1);

    std::vector<rendering::CharacterBillboardDraw> characters = character_draws;
    if (player_visible_) {
        rendering::CharacterBillboardDraw player_draw{};
        player_draw.character = &character_;
        player_draw.source_rect = player_source_rect;
        player_draw.activity_id = player_activity_id;
        player_draw.use_run_texture = player_use_run_texture;
        player_draw.draw_shadow = player_draw_shadow;
        player_draw.depth_priority_bias = kPlayerBillboardDepthPriorityBias;
        player_draw.placement = rendering::buildCharacterBillboardPlacement(
            scene_,
            camera,
            player_binding,
            character_,
            player_pos,
            player_source_rect,
            placement_w,
            placement_h);
        characters.push_back(player_draw);
    }

    submitProjectedCharacterShadows(camera, characters);

    if (billboard_drawer_) {
        billboard_drawer_->setWorldViewport(
            placement_w,
            placement_h,
            world_view_w,
            world_view_h,
            pixel_world_enabled ? internal_scale : 1);
    }

    if (billboard_drawer_) {
        std::stable_sort(
            characters.begin(),
            characters.end(),
            [](const rendering::CharacterBillboardDraw& a, const rendering::CharacterBillboardDraw& b) {
                constexpr float kDepthTieEpsilon = 0.0001f;
                const float a_depth = a.placement.visible ? a.placement.depth : std::numeric_limits<float>::max();
                const float b_depth = b.placement.visible ? b.placement.depth : std::numeric_limits<float>::max();
                if (std::abs(a_depth - b_depth) <= kDepthTieEpsilon) {
                    return (a_depth + a.depth_priority_bias) > (b_depth + b.depth_priority_bias);
                }
                return a_depth > b_depth;
            }
        );
        for (const rendering::CharacterBillboardDraw& character_draw : characters) {
            billboard_drawer_->submitCharacterDraw(camera, character_draw);
        }

        for (const ModelGpuResource& model : models_) {
            submitMesh(model.mesh, model.model_matrix, world_program_, MaterialClass::TrueBlend, 0.0f,
                stateFor(MaterialClass::TrueBlend), 1, false, nullptr, model.aquarium_tank_lit);
        }
        for (const StaticChunkGpuResource& chunk : static_chunks_) {
            for (const ModelGpuResource& model : chunk.models) {
                submitMesh(model.mesh, model.model_matrix, world_program_, MaterialClass::TrueBlend, 0.0f, stateFor(MaterialClass::TrueBlend), 1);
            }
        }

        std::vector<rendering::TextureBillboardDraw> transparent_textures = texture_draws;
        std::stable_sort(
            transparent_textures.begin(),
            transparent_textures.end(),
            [](const rendering::TextureBillboardDraw& a, const rendering::TextureBillboardDraw& b) {
                const float a_depth = a.placement.visible ? a.placement.depth : std::numeric_limits<float>::max();
                const float b_depth = b.placement.visible ? b.placement.depth : std::numeric_limits<float>::max();
                return a_depth > b_depth;
            });
        for (const rendering::TextureBillboardDraw& texture_draw : transparent_textures) {
            billboard_drawer_->submitTextureDraw(camera, texture_draw);
        }
    }
    const auto aquarium_camera_pose = camera.pose();
    player_aquarium_renderer_.submitTransparent(
        1,
        aquarium_camera_pose.position.x,
        aquarium_camera_pose.position.y,
        aquarium_camera_pose.position.z);

    override_animation_clock_ = previous_override_animation_clock;
    animations_enabled_ = previous_animations_enabled;
    animation_time_seconds_ = previous_animation_time_seconds;

    if (options.embedded_viewport) {
        const bgfx::TextureHandle color = bgfx::getTexture(pixel_world_target_.frame_buffer, 0);
        if (!bgfx::isValid(color)) return {};
        OverworldBgfxRenderer::EmbeddedViewportTexture texture;
        texture.texture_handle_idx = color.idx;
        texture.width = pixel_world_target_.width;
        texture.height = pixel_world_target_.height;
        texture.origin_bottom_left = backend_.originBottomLeft();
        return texture;
    }

    if (pixel_world_enabled) {
        submitPixelWorldToBackbuffer(framebuffer_w, framebuffer_h, render_w, render_h);
    }

    // The Metal view sits above SDL's presentation renderer, so construction
    // controls must be composited on the bgfx backbuffer to remain visible.
    // Geometry stays in the pixel-world view; these vector icons use the full
    // logical canvas and therefore keep crisp, stable hit bounds.
    aquarium_construction_renderer_.submitHud(
        6, framebuffer_w, framebuffer_h, logical_w, logical_h,
        backend_.homogeneousDepth());

    if (textbox_overlay_visible_ && ensureTextboxTexture()) {
        SDL_Rect world_viewport{0, 0, std::max(1, framebuffer_w), std::max(1, framebuffer_h)};
        int textbox_base_w = std::max(1, logical_w);
        int textbox_base_h = std::max(1, logical_h);
        if (pixel_world_enabled) {
            const int integer_scale = std::max(
                1,
                std::min(
                    std::max(1, framebuffer_w) / std::max(1, render_w),
                    std::max(1, framebuffer_h) / std::max(1, render_h)));
            const int dest_w = render_w * integer_scale;
            const int dest_h = render_h * integer_scale;
            world_viewport = SDL_Rect{
                (std::max(1, framebuffer_w) - dest_w) / 2,
                (std::max(1, framebuffer_h) - dest_h) / 2,
                dest_w,
                dest_h};
            textbox_base_w = base_w;
            textbox_base_h = base_h;
        }
        ensureTextboxTextTexture(std::max(
            1,
            textbox_base_w - textbox_config_.text_left_inset_px - textbox_config_.text_right_inset_px));
        submitTextboxOverlay(framebuffer_w, framebuffer_h, world_viewport, textbox_base_w, textbox_base_h);
    }
    if (attend_button_visible_ && ensureAttendButtonTexture()) {
        submitAttendButtonOverlay(framebuffer_w, framebuffer_h, logical_w, logical_h);
    }
    submitBlackIrisTransition(framebuffer_w, framebuffer_h, logical_w, logical_h);

    if (!debug_frame_counter_label.empty()) {
        bgfx::setDebug(BGFX_DEBUG_TEXT);
        bgfx::dbgTextClear();
        const int columns = std::max(1, framebuffer_w / 8);
        const int x = std::max(0, columns - static_cast<int>(debug_frame_counter_label.size()) - 2);
        bgfx::dbgTextPrintf(static_cast<std::uint16_t>(x), 1, 0x0f, "%s", debug_frame_counter_label.c_str());
    } else {
        bgfx::setDebug(BGFX_DEBUG_NONE);
    }

    backend_.endFrame();
    return {};
}

} // namespace pr::gameplay::world3d::rendering::bgfx_backend
