#include "gameplay/world3d/rendering/bgfx/OverworldBgfxRenderer.hpp"

#include "gameplay/world3d/rendering/bgfx/BillboardBgfxDrawer.hpp"
#include "gameplay/world3d/rendering/BillboardPlacement.hpp"
#include "gameplay/world3d/rendering/CharacterTextureCache.hpp"
#include "gameplay/world3d/rendering/SpriteShadowDecal.hpp"
#include "gameplay/world3d/data/GlbModelLoader.hpp"
#include "gameplay/world3d/rendering/bgfx/BgfxBackend.hpp"

#include <SDL_image.h>

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <unordered_map>

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

std::uint32_t packAbgr(float r, float g, float b, float a = 1.0f) {
    const auto c = [](float v) -> std::uint32_t {
        return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return (c(a) << 24U) | (c(b) << 16U) | (c(g) << 8U) | c(r);
}

std::uint32_t packTerrainColor(const TerrainColor& color) {
    return packAbgr(
        static_cast<float>(color.r) / 255.0f,
        static_cast<float>(color.g) / 255.0f,
        static_cast<float>(color.b) / 255.0f,
        static_cast<float>(color.a) / 255.0f);
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
    if (material.alpha_mode == data::GlbMaterial::AlphaMode::Mask) return MaterialClass::MaskCutout;
    if (material.alpha_mode == data::GlbMaterial::AlphaMode::Blend) return MaterialClass::TrueBlend;
    if (material.base_color[3] < 0.999f) return MaterialClass::TrueBlend;
    return MaterialClass::Opaque;
}

std::uint64_t samplerFlags() {
    return BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
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

} // namespace

class OverworldBgfxRenderer::Impl {
public:
    Impl(std::string project_root, SceneConfig scene, CharacterSpriteDefinition character)
        : project_root_(std::move(project_root)), scene_(std::move(scene)), character_(std::move(character)) {}
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
    void render(
        const camera::Gen4FollowCamera& camera,
        const camera::Vec3& player_pos,
        const SDL_Rect& player_source_rect,
        const terrain::ActorTerrainBinding& player_binding,
        int logical_w,
        int logical_h,
        int framebuffer_w,
        int framebuffer_h,
        const std::vector<rendering::CharacterBillboardDraw>& character_draws,
        const std::vector<rendering::TextureBillboardDraw>& texture_draws);

private:
    struct Vertex {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        std::uint32_t abgr = 0xffffffffu;
        float u = 0.0f;
        float v = 0.0f;
    };

    struct TextureGpuResource {
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        int width = 0;
        int height = 0;
        bool destroy();
        bool valid() const { return bgfx::isValid(handle); }
    };

    struct CharacterGpuTextures {
        TextureGpuResource color;
        TextureGpuResource white;
    };

    struct MaterialGpuResource {
        TextureGpuResource texture;
        MaterialClass material_class = MaterialClass::Opaque;
        float base_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float alpha_cutoff = 0.5f;
    };

    struct MaterialRange {
        std::uint32_t start_index = 0;
        std::uint32_t index_count = 0;
        int material = -1;
        MaterialClass material_class = MaterialClass::Opaque;
    };

    struct MeshGpuResource {
        bgfx::VertexBufferHandle vbh = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle ibh = BGFX_INVALID_HANDLE;
        std::vector<MaterialRange> ranges;
        std::vector<MaterialGpuResource> materials;
        bool destroy();
        bool valid() const { return bgfx::isValid(vbh) && bgfx::isValid(ibh); }
    };

    struct ModelGpuResource {
        MeshGpuResource mesh;
        float model_matrix[16]{};
    };

    std::string project_root_;
    SceneConfig scene_;
    CharacterSpriteDefinition character_;
    BgfxBackend backend_;
    bool initialized_ = false;
    std::string last_error_;

    bgfx::VertexLayout layout_{};
    bgfx::ProgramHandle world_program_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle billboard_program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle tex_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle tint_cutoff_uniform_ = BGFX_INVALID_HANDLE;
    TextureGpuResource white_texture_;
    TextureGpuResource shadow_texture_;
    mutable std::unordered_map<std::string, CharacterGpuTextures> character_textures_;
    mutable std::unordered_map<std::string, TextureGpuResource> effect_textures_;
    MeshGpuResource terrain_flat_top_mesh_;
    MeshGpuResource terrain_slope_top_mesh_;
    MeshGpuResource terrain_wall_mesh_;
    std::vector<ModelGpuResource> models_;

    bool createPrograms();
    bool buildTerrain();
    bool buildModels();
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
        const char* debug_name) const;

    void submitMesh(
        const MeshGpuResource& mesh,
        const float* model_matrix,
        bgfx::ProgramHandle program,
        MaterialClass pass,
        float alpha_cutoff,
        std::uint64_t state,
        bgfx::ViewId view_id = 0) const;
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
    return true;
}

bool OverworldBgfxRenderer::Impl::MeshGpuResource::destroy() {
    if (bgfx::isValid(vbh)) {
        bgfx::destroy(vbh);
        vbh = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(ibh)) {
        bgfx::destroy(ibh);
        ibh = BGFX_INVALID_HANDLE;
    }
    for (MaterialGpuResource& material : materials) {
        material.texture.destroy();
    }
    ranges.clear();
    materials.clear();
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

void OverworldBgfxRenderer::render(
    const camera::Gen4FollowCamera& camera,
    const camera::Vec3& player_pos,
    const SDL_Rect& player_source_rect,
    const terrain::ActorTerrainBinding& player_binding,
    int logical_w,
    int logical_h,
    int framebuffer_w,
    int framebuffer_h,
    const std::vector<rendering::CharacterBillboardDraw>& character_draws,
    const std::vector<rendering::TextureBillboardDraw>& texture_draws) {
    if (impl_) {
        impl_->render(
            camera,
            player_pos,
            player_source_rect,
            player_binding,
            logical_w,
            logical_h,
            framebuffer_w,
            framebuffer_h,
            character_draws,
            texture_draws);
    }
}

bool OverworldBgfxRenderer::Impl::initialize(
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
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();

    tex_uniform_ = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
    tint_cutoff_uniform_ = bgfx::createUniform("u_tintCutoff", bgfx::UniformType::Vec4);

    std::uint8_t white[4] = {255, 255, 255, 255};
    white_texture_ = createTextureFromRgba(white, 1, 1, "world3d-white");
    if (!white_texture_.valid()) {
        last_error_ = "Could not create white texture";
        shutdown();
        return false;
    }

    if (!createPrograms() || !buildTerrain() || !buildModels() || !buildPlayerTexture() || !buildShadowTexture()) {
        shutdown();
        return false;
    }

    refreshBillboardDrawer();
    initialized_ = true;
    std::cerr << "[OverworldBgfx] Ready terrain="
              << (terrain_flat_top_mesh_.valid() || terrain_slope_top_mesh_.valid() || terrain_wall_mesh_.valid() ? "yes" : "no")
              << " models=" << models_.size()
              << " playerTexture=" << character_textures_[character_.texture_path].color.width << "x"
              << character_textures_[character_.texture_path].color.height
              << '\n';
    return initialized_;
}

void OverworldBgfxRenderer::Impl::shutdown() {
    terrain_flat_top_mesh_.destroy();
    terrain_slope_top_mesh_.destroy();
    terrain_wall_mesh_.destroy();
    for (ModelGpuResource& model : models_) {
        model.mesh.destroy();
    }
    models_.clear();
    shadow_texture_.destroy();
    for (auto& entry : character_textures_) {
        entry.second.color.destroy();
        entry.second.white.destroy();
    }
    character_textures_.clear();
    for (auto& entry : effect_textures_) {
        entry.second.destroy();
    }
    effect_textures_.clear();
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
    backend_.shutdown();
    initialized_ = false;
}

bool OverworldBgfxRenderer::Impl::createPrograms() {
    const std::filesystem::path shader_root = backend_.shaderDirectory();
    const std::string shader_subdir = backend_.shaderSubdirectory();
    bgfx::ShaderHandle vs_world = loadShader(shader_root, shader_subdir, "vs_world");
    bgfx::ShaderHandle vs_billboard = loadShader(shader_root, shader_subdir, "vs_billboard");
    bgfx::ShaderHandle fs = loadShader(shader_root, shader_subdir, "fs_textured_cutout");
    bgfx::ShaderHandle fs_billboard = loadShader(shader_root, shader_subdir, "fs_textured_cutout");
    if (!bgfx::isValid(vs_world) || !bgfx::isValid(vs_billboard) || !bgfx::isValid(fs) || !bgfx::isValid(fs_billboard)) {
        last_error_ = "Could not load bgfx shader binaries from " + shader_root.string();
        if (bgfx::isValid(vs_world)) bgfx::destroy(vs_world);
        if (bgfx::isValid(vs_billboard)) bgfx::destroy(vs_billboard);
        if (bgfx::isValid(fs)) bgfx::destroy(fs);
        if (bgfx::isValid(fs_billboard)) bgfx::destroy(fs_billboard);
        return false;
    }
    world_program_ = bgfx::createProgram(vs_world, fs, true);
    billboard_program_ = bgfx::createProgram(vs_billboard, fs_billboard, true);
    if (!bgfx::isValid(world_program_) || !bgfx::isValid(billboard_program_)) {
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
    const char* debug_name) const {
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
    TextureGpuResource out = createTextureFromRgba(
        static_cast<const std::uint8_t*>(converted->pixels),
        converted->w,
        converted->h,
        debug_name);
    SDL_FreeSurface(converted);
    return out;
}

bool OverworldBgfxRenderer::Impl::buildTerrain() {
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

    const std::uint32_t floor_a = packTerrainColor(scene_.terrain.floor_color_a);
    const std::uint32_t floor_b = packTerrainColor(scene_.terrain.floor_color_b);
    const std::uint32_t first_non_base_a = packTerrainColor(scene_.terrain.first_non_base_floor_color_a);
    const std::uint32_t first_non_base_b = packTerrainColor(scene_.terrain.first_non_base_floor_color_b);
    const std::uint32_t ramp_a = packTerrainColor(scene_.terrain.ramp_color_a);
    const std::uint32_t ramp_b = packTerrainColor(scene_.terrain.ramp_color_b);
    for (int y = 0; y < grid_h; ++y) {
        for (int x = 0; x < grid_w; ++x) {
            const float x0 = static_cast<float>(x) * tile_size;
            const float z0 = static_cast<float>(y) * tile_size;
            const float x1 = x0 + tile_size;
            const float z1 = z0 + tile_size;
            float c[4]{};
            fill_corners(x, y, c);
            const bool slope = is_slope_special(tile_special(x, y));
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
            push_quad(top_vertices, top_indices, x0, c[0], z0, x1, c[1], z0, x1, c[2], z1, x0, c[3], z1, color);
        }
    }

    const auto add_wall_if_drop = [&](float xa, float za, float ya0, float ya1,
                                      float xb, float zb, float yb0, float yb1,
                                      std::uint32_t color) {
        const float high = std::min(ya0, ya1);
        const float low = std::min(yb0, yb1);
        if (high <= low) return;
        push_quad(wall_vertices, wall_indices, xa, low, za, xb, low, zb, xb, high, zb, xa, high, za, color);
    };
    const std::uint32_t wall_ns = packTerrainColor(scene_.terrain.wall_color_ns);
    const std::uint32_t wall_ew = packTerrainColor(scene_.terrain.wall_color_ew);
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

    if (flat_top_vertices.empty() && slope_top_vertices.empty() && wall_vertices.empty()) {
        last_error_ = "Terrain produced no bgfx geometry";
        return false;
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
    return upload_mesh(terrain_flat_top_mesh_, flat_top_vertices, flat_top_indices) &&
        upload_mesh(terrain_slope_top_mesh_, slope_top_vertices, slope_top_indices) &&
        upload_mesh(terrain_wall_mesh_, wall_vertices, wall_indices);
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
        }

        std::vector<Vertex> vertices;
        std::vector<std::uint32_t> indices;
        vertices.reserve(glb.triangles.size() * 3);
        indices.reserve(glb.triangles.size() * 3);

        for (std::size_t mat_index = 0; mat_index < glb.materials.size(); ++mat_index) {
            const std::uint32_t range_start = static_cast<std::uint32_t>(indices.size());
            for (const data::GlbTriangle& tri : glb.triangles) {
                if (tri.material != static_cast<int>(mat_index)) continue;
                const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
                vertices.push_back(Vertex{tri.a.x, tri.a.y, tri.a.z, 0xffffffffu, tri.a.u, tri.a.v});
                vertices.push_back(Vertex{tri.b.x, tri.b.y, tri.b.z, 0xffffffffu, tri.b.u, tri.b.v});
                vertices.push_back(Vertex{tri.c.x, tri.c.y, tri.c.z, 0xffffffffu, tri.c.u, tri.c.v});
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
        const bgfx::Memory* vb_mem = bgfx::copy(vertices.data(), static_cast<std::uint32_t>(vertices.size() * sizeof(Vertex)));
        const bgfx::Memory* ib_mem = bgfx::copy(indices.data(), static_cast<std::uint32_t>(indices.size() * sizeof(std::uint32_t)));
        model.mesh.vbh = bgfx::createVertexBuffer(vb_mem, layout_);
        model.mesh.ibh = bgfx::createIndexBuffer(ib_mem, BGFX_BUFFER_INDEX32);
        if (model.mesh.valid()) {
            models_.push_back(std::move(model));
        }
    }
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
    bgfx::ViewId view_id) const {
    if (!mesh.valid()) return;
    for (const MaterialRange& range : mesh.ranges) {
        if (range.material_class != pass || range.index_count == 0) continue;
        const MaterialGpuResource* material = nullptr;
        if (range.material >= 0 && range.material < static_cast<int>(mesh.materials.size())) {
            material = &mesh.materials[static_cast<std::size_t>(range.material)];
        }
        const TextureGpuResource& texture = (material && material->texture.valid()) ? material->texture : white_texture_;
        const float br = std::max(0.0f, scene_.lighting_brightness);
        float tint[4] = {
            scene_.lighting_tint_r * br,
            scene_.lighting_tint_g * br,
            scene_.lighting_tint_b * br,
            pass == MaterialClass::Opaque ? 0.0f : 0.5f,
        };
        if (material) {
            tint[0] *= material->base_color[0];
            tint[1] *= material->base_color[1];
            tint[2] *= material->base_color[2];
            tint[3] = pass == MaterialClass::Opaque ? 0.0f : material->alpha_cutoff;
        }
        bgfx::setTransform(model_matrix);
        bgfx::setVertexBuffer(0, mesh.vbh);
        bgfx::setIndexBuffer(mesh.ibh, range.start_index, range.index_count);
        bgfx::setTexture(0, tex_uniform_, texture.handle, samplerFlags());
        bgfx::setUniform(tint_cutoff_uniform_, tint);
        bgfx::setState(state);
        bgfx::submit(view_id, program);
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
    deps.view_id = 1;
    deps.scene = &scene_;
    deps.shadow_texture = copyTexture(shadow_texture_);
    deps.textures_for_character = [this, copyTexture](const CharacterSpriteDefinition& character) {
        const CharacterGpuTextures& src = texturesForCharacter(character);
        BillboardBgfxDrawer::CharacterGpuTextures out{};
        out.color = copyTexture(src.color);
        out.white = copyTexture(src.white);
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
    const terrain::ActorTerrainBinding& player_binding,
    int logical_w,
    int logical_h,
    int framebuffer_w,
    int framebuffer_h,
    const std::vector<rendering::CharacterBillboardDraw>& character_draws,
    const std::vector<rendering::TextureBillboardDraw>& texture_draws) {
    if (!valid()) return;
    backend_.reset(framebuffer_w, framebuffer_h);
    backend_.beginFrame(150.0f / 255.0f, 191.0f / 255.0f, 224.0f / 255.0f, 1.0f);

    const auto pose = camera.pose();
    const bx::Vec3 eye{pose.position.x, pose.position.y, pose.position.z};
    const bx::Vec3 at{
        pose.position.x + pose.forward.x,
        pose.position.y + pose.forward.y,
        pose.position.z + pose.forward.z};
    const bx::Vec3 up{pose.up.x, pose.up.y, pose.up.z};
    float view[16];
    float proj[16];
    bx::mtxLookAt(view, eye, at, up);
    const float aspect =
        static_cast<float>(std::max(1, logical_w)) / static_cast<float>(std::max(1, logical_h));
    bx::mtxProj(
        proj,
        pose.preset.fov_y_deg,
        aspect,
        pose.preset.near_clip,
        pose.preset.far_clip,
        backend_.homogeneousDepth());
    bgfx::setViewTransform(0, view, proj);
    bgfx::setViewRect(
        0,
        0,
        0,
        static_cast<std::uint16_t>(framebuffer_w),
        static_cast<std::uint16_t>(framebuffer_h));
    bgfx::setViewTransform(1, view, proj);
    bgfx::setViewRect(
        1,
        0,
        0,
        static_cast<std::uint16_t>(framebuffer_w),
        static_cast<std::uint16_t>(framebuffer_h));
    bgfx::setViewClear(1, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
    bgfx::setViewMode(1, bgfx::ViewMode::Sequential);

    float ident[16];
    identity(ident);
    submitMesh(terrain_flat_top_mesh_, ident, world_program_, MaterialClass::Opaque, 0.0f, stateFor(MaterialClass::Opaque));
    submitMesh(terrain_slope_top_mesh_, ident, world_program_, MaterialClass::Opaque, 0.0f, stateFor(MaterialClass::Opaque));
    submitMesh(terrain_wall_mesh_, ident, world_program_, MaterialClass::Opaque, 0.0f, stateFor(MaterialClass::Opaque));
    for (const ModelGpuResource& model : models_) {
        submitMesh(model.mesh, model.model_matrix, world_program_, MaterialClass::Opaque, 0.0f, stateFor(MaterialClass::Opaque));
    }
    for (const ModelGpuResource& model : models_) {
        submitMesh(model.mesh, model.model_matrix, world_program_, MaterialClass::MaskCutout, 0.5f, stateFor(MaterialClass::MaskCutout));
    }

    bgfx::touch(1);
    submitMesh(terrain_wall_mesh_, ident, world_program_, MaterialClass::Opaque, 0.0f, stateForDepthOnly(), 1);
    for (const ModelGpuResource& model : models_) {
        submitMesh(model.mesh, model.model_matrix, world_program_, MaterialClass::Opaque, 0.0f, stateForDepthOnly(), 1);
    }
    for (const ModelGpuResource& model : models_) {
        submitMesh(model.mesh, model.model_matrix, world_program_, MaterialClass::MaskCutout, 0.5f, stateForDepthOnly(), 1);
    }

    struct DrawOrderItem {
        float sort_depth = 0.0f;
        bool is_texture = false;
        std::size_t index = 0;
    };

    std::vector<rendering::CharacterBillboardDraw> characters = character_draws;
    rendering::CharacterBillboardDraw player_draw{};
    player_draw.character = &character_;
    player_draw.source_rect = player_source_rect;
    player_draw.draw_shadow = true;
    player_draw.placement = rendering::buildCharacterBillboardPlacement(
        scene_,
        camera,
        player_binding,
        character_,
        player_pos,
        player_source_rect,
        logical_w,
        logical_h);
    characters.push_back(player_draw);

    if (billboard_drawer_) {
        for (const rendering::CharacterBillboardDraw& character_draw : characters) {
            billboard_drawer_->submitCharacterShadow(camera, character_draw);
        }
    }

    std::vector<DrawOrderItem> draw_order;
    draw_order.reserve(characters.size() + texture_draws.size());
    for (std::size_t i = 0; i < characters.size(); ++i) {
        const float depth = characters[i].placement.visible
            ? characters[i].placement.depth
            : rendering::billboardSortDepth(camera, player_pos, logical_w, logical_h);
        draw_order.push_back(DrawOrderItem{depth, false, i});
    }
    for (std::size_t i = 0; i < texture_draws.size(); ++i) {
        const float depth = texture_draws[i].placement.visible
            ? texture_draws[i].placement.depth
            : std::numeric_limits<float>::max();
        draw_order.push_back(DrawOrderItem{depth, true, i});
    }
    std::stable_sort(
        draw_order.begin(),
        draw_order.end(),
        [](const DrawOrderItem& a, const DrawOrderItem& b) { return a.sort_depth > b.sort_depth; });

    if (billboard_drawer_) {
        for (const DrawOrderItem& item : draw_order) {
            if (item.is_texture) {
                billboard_drawer_->submitTextureDraw(camera, texture_draws[item.index]);
            } else {
                billboard_drawer_->submitCharacterDraw(camera, characters[item.index]);
            }
        }
    }

    for (const ModelGpuResource& model : models_) {
        submitMesh(model.mesh, model.model_matrix, world_program_, MaterialClass::TrueBlend, 0.5f, stateFor(MaterialClass::TrueBlend));
    }

    backend_.endFrame();
}

} // namespace pr::gameplay::world3d::rendering::bgfx_backend
