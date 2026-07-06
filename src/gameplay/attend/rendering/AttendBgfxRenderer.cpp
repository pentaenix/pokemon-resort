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

namespace {

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

std::uint32_t packAbgr(float r, float g, float b, float a = 1.0f) {
    const auto c = [](float v) -> std::uint32_t {
        return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return (c(a) << 24U) | (c(b) << 16U) | (c(g) << 8U) | c(r);
}

std::uint8_t byteChannel(float v) {
    return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

bool insideRoundedRect(int x, int y, int w, int h, int radius) {
    if (w <= 0 || h <= 0) return false;
    radius = std::clamp(radius, 0, std::min(w, h) / 2);
    if (radius <= 0) return x >= 0 && x < w && y >= 0 && y < h;
    const int left = radius;
    const int right = w - radius - 1;
    const int top = radius;
    const int bottom = h - radius - 1;
    if ((x >= left && x <= right) || (y >= top && y <= bottom)) return true;
    const int cx = x < left ? left : right;
    const int cy = y < top ? top : bottom;
    const int dx = x - cx;
    const int dy = y - cy;
    return dx * dx + dy * dy <= radius * radius;
}

void setRgba(std::vector<std::uint8_t>& pixels, int w, int x, int y, const Color4& color) {
    const std::size_t offset = static_cast<std::size_t>((y * w + x) * 4);
    pixels[offset + 0] = byteChannel(color.r);
    pixels[offset + 1] = byteChannel(color.g);
    pixels[offset + 2] = byteChannel(color.b);
    pixels[offset + 3] = byteChannel(color.a);
}

std::string overlayStyleKey(const AttendOverlayButtonConfig& style, const std::string& label, int w, int h) {
    std::ostringstream out;
    out << label << '|' << w << 'x' << h << '|'
        << style.corner_radius << '|' << style.stroke_width << '|' << style.font_size << '|'
        << style.fill.r << ',' << style.fill.g << ',' << style.fill.b << ',' << style.fill.a << '|'
        << style.stroke.r << ',' << style.stroke.g << ',' << style.stroke.b << ',' << style.stroke.a << '|'
        << style.text.r << ',' << style.text.g << ',' << style.text.b << ',' << style.text.a;
    return out.str();
}

Color3 mix(Color3 a, Color3 b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return Color3{
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t};
}

Color3 sampleGradient(const std::vector<GradientStop>& stops, float at) {
    if (stops.empty()) return Color3{};
    at = std::clamp(at, 0.0f, 1.0f);
    if (at <= stops.front().at) return stops.front().color;
    for (std::size_t i = 1; i < stops.size(); ++i) {
        if (at <= stops[i].at) {
            const float span = std::max(0.0001f, stops[i].at - stops[i - 1].at);
            return mix(stops[i - 1].color, stops[i].color, (at - stops[i - 1].at) / span);
        }
    }
    return stops.back().color;
}

bool containsAnySubstring(const std::string& value, const std::vector<std::string>& needles) {
    for (const std::string& needle : needles) {
        if (!needle.empty() && value.find(needle) != std::string::npos) return true;
    }
    return false;
}

std::string lowercaseAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool containsAscii(std::string value, const char* needle) {
    value = lowercaseAscii(std::move(value));
    return value.find(needle) != std::string::npos;
}

int jsonIntOr(const JsonValue* value, int fallback) {
    return value && value->isNumber() ? static_cast<int>(value->asNumber()) : fallback;
}

bool jsonBoolOr(const JsonValue* value, bool fallback) {
    return value && value->isBool() ? value->asBool() : fallback;
}

pr::gameplay::world3d::WorldViewportConfig loadSharedWorldViewportConfig(const std::string& project_root) {
    pr::gameplay::world3d::WorldViewportConfig out{};
    const fs::path path = fs::path(project_root) / "config" / "gameplay" / "world3d" / "render.json";
    try {
        const JsonValue root = parseJsonFile(path.string());
        const JsonValue* world_viewport = root.isObject() ? root.get("worldViewport") : nullptr;
        if (world_viewport && world_viewport->isObject()) {
            out.enabled = jsonBoolOr(world_viewport->get("enabled"), out.enabled);
            out.base_width = jsonIntOr(world_viewport->get("baseWidth"), out.base_width);
            out.base_height = jsonIntOr(world_viewport->get("baseHeight"), out.base_height);
            out.internal_scale = jsonIntOr(
                world_viewport->get("upscale"),
                jsonIntOr(world_viewport->get("internalScale"), out.internal_scale));
        }
    } catch (const std::exception& ex) {
        std::cerr << "[AttendBgfx] Could not load shared worldViewport config: "
                  << ex.what() << '\n';
    }
    out.base_width = std::clamp(out.base_width, 160, 1920);
    out.base_height = std::clamp(out.base_height, 120, 1080);
    out.internal_scale = std::clamp(out.internal_scale, 1, 4);
    return out;
}

void normalize3(float& x, float& y, float& z) {
    const float len = std::sqrt(x * x + y * y + z * z);
    if (len <= 0.00001f) {
        x = 0.0f;
        y = 1.0f;
        z = 0.0f;
        return;
    }
    x /= len;
    y /= len;
    z /= len;
}

void identity(float (&m)[16]) {
    std::fill(std::begin(m), std::end(m), 0.0f);
    m[0] = 1.0f;
    m[5] = 1.0f;
    m[10] = 1.0f;
    m[15] = 1.0f;
}

void placementMatrix(float x, float y, float z, float yaw_deg, float pitch_deg, float scale, float (&m)[16]) {
    identity(m);
    const float yaw = yaw_deg * (kPi / 180.0f);
    const float pitch = pitch_deg * (kPi / 180.0f);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    m[0] = cy * scale;
    m[1] = 0.0f;
    m[2] = -sy * scale;
    m[4] = sy * sp * scale;
    m[5] = cp * scale;
    m[6] = cy * sp * scale;
    m[8] = sy * cp * scale;
    m[9] = -sp * scale;
    m[10] = cy * cp * scale;
    m[12] = x;
    m[13] = y;
    m[14] = z;
}

Vertex transformStaticVertex(
    const pr::gameplay::world3d::data::GlbVertex& src,
    float x,
    float y,
    float z,
    float yaw_deg,
    float scale) {
    const float yaw = yaw_deg * (kPi / 180.0f);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float sx = src.x * scale;
    const float syv = src.y * scale;
    const float sz = src.z * scale;
    return Vertex{
        x + sx * cy + sz * sy,
        y + syv,
        z - sx * sy + sz * cy,
        0.0f,
        1.0f,
        0.0f,
        packAbgr(src.r, src.g, src.b, src.a),
        src.u,
        src.v};
}

Vertex transformAttendVertex(
    const AttendPokemonVertex& src,
    float x,
    float y,
    float z,
    float yaw_deg,
    float scale) {
    const float yaw = yaw_deg * (kPi / 180.0f);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float sx = src.x * scale;
    const float syv = src.y * scale;
    const float sz = src.z * scale;
    float nx = src.nx * cy + src.nz * sy;
    float ny = src.ny;
    float nz = -src.nx * sy + src.nz * cy;
    normalize3(nx, ny, nz);
    return Vertex{
        x + sx * cy + sz * sy,
        y + syv,
        z - sx * sy + sz * cy,
        nx,
        ny,
        nz,
        packAbgr(src.r, src.g, src.b, src.a),
        src.u,
        src.v};
}

bgfx::ShaderHandle loadShader(const fs::path& shader_root, const std::string& shader_subdir, const char* name) {
    const fs::path path = shader_root / shader_subdir / (std::string(name) + ".sc.bin");
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "[AttendBgfx] Missing shader: " << path << std::endl;
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
    if (bgfx::isValid(shader)) bgfx::setName(shader, name);
    return shader;
}

std::uint64_t samplerFlags(int wrap_s = 10497, int wrap_t = 10497) {
    std::uint64_t flags = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
    if (wrap_s == 33071) {
        flags |= BGFX_SAMPLER_U_CLAMP;
    } else if (wrap_s == 33648) {
        flags |= BGFX_SAMPLER_U_MIRROR;
    }
    if (wrap_t == 33071) {
        flags |= BGFX_SAMPLER_V_CLAMP;
    } else if (wrap_t == 33648) {
        flags |= BGFX_SAMPLER_V_MIRROR;
    }
    return flags;
}

std::uint64_t smoothSamplerFlags(int wrap_s = 10497, int wrap_t = 10497) {
    std::uint64_t flags = 0;
    if (wrap_s == 33071) {
        flags |= BGFX_SAMPLER_U_CLAMP;
    } else if (wrap_s == 33648) {
        flags |= BGFX_SAMPLER_U_MIRROR;
    }
    if (wrap_t == 33071) {
        flags |= BGFX_SAMPLER_V_CLAMP;
    } else if (wrap_t == 33648) {
        flags |= BGFX_SAMPLER_V_MIRROR;
    }
    return flags;
}

std::uint64_t samplerFlagsFromGfWrap(int wrap_s, int wrap_t) {
    const auto toGltfWrap = [](int wrap) {
        if (wrap == 0 || wrap == 1) return 33071;
        if (wrap == 3) return 33648;
        return 10497;
    };
    return samplerFlags(toGltfWrap(wrap_s), toGltfWrap(wrap_t));
}

std::uint64_t opaqueState() {
    return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
}

std::uint64_t blendState() {
    return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_BLEND_ALPHA;
}

std::uint64_t overlayState() {
    return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_BLEND_ALPHA;
}

std::uint64_t eyeScleraStencilState() {
    return BGFX_STATE_DEPTH_TEST_LEQUAL;
}

std::uint64_t backdropState(bool blend) {
    std::uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
    if (blend) state |= BGFX_STATE_BLEND_ALPHA;
    return state;
}

int expressionFrameOr(const AttendSceneConfig& config, const char* key, int fallback) {
    const auto it = config.interaction_adapter.eye_expression_frames.find(key);
    return it == config.interaction_adapter.eye_expression_frames.end() ? fallback : std::max(0, it->second);
}

int mouthExpressionFrameOr(const AttendSceneConfig& config, const char* key, int fallback) {
    const auto it = config.interaction_adapter.mouth_expression_frames.find(key);
    return it == config.interaction_adapter.mouth_expression_frames.end() ? fallback : std::max(0, it->second);
}

bool materialUsesEyeExpressionFrames(const AttendPokemonMaterial* material) {
    return material &&
        material->material_role == AttendMaterialRole::EyeSclera &&
        material->eye_sheet.enabled &&
        containsAscii(material->name, "eye");
}

bool materialUsesMouthExpressionFrames(const AttendPokemonMaterial* material) {
    return material &&
        material->material_role == AttendMaterialRole::Mouth &&
        material->eye_sheet.enabled;
}

bool materialIsEyeScleraMask(const AttendPokemonMaterial* material) {
    return materialUsesEyeExpressionFrames(material);
}

bool materialIsSeparateEyeIris(const AttendPokemonMaterial* material) {
    return material && material->material_role == AttendMaterialRole::EyeIris;
}

bool pokemonPrimitivePreviewVisible(
    const AttendPokemonModel& model,
    const AttendPokemonPrimitive& primitive) {
    if (primitive.default_visible) return true;
    if (!primitive.visible_for_forms.empty()) return true;
    if (primitive.mesh_node >= 0 && primitive.mesh_node < static_cast<int>(model.nodes.size()) &&
        containsAscii(model.nodes[static_cast<std::size_t>(primitive.mesh_node)].name, "vco")) {
        return true;
    }
    if (primitive.material >= 0 && primitive.material < static_cast<int>(model.materials.size()) &&
        containsAscii(model.materials[static_cast<std::size_t>(primitive.material)].name, "vco")) {
        return true;
    }
    return false;
}

bool stringListContains(const std::vector<std::string>& values, const std::string& needle) {
    return std::find(values.begin(), values.end(), needle) != values.end();
}

Vertex pokemonVertexForMaterial(
    const AttendPokemonVertex& src,
    const AttendPokemonMaterial* material,
    int eye_expression_frame,
    int mouth_expression_frame) {
    float u = src.u;
    float v = src.v;
    if (material &&
        (material->material_role == AttendMaterialRole::EyeSclera ||
         material->material_role == AttendMaterialRole::Mouth) &&
        material->eye_sheet.enabled) {
        const AttendEyeSheet& eye = material->eye_sheet;
        int desired_frame = eye.default_frame;
        if (materialUsesEyeExpressionFrames(material)) {
            desired_frame = eye_expression_frame;
        } else if (materialUsesMouthExpressionFrames(material)) {
            desired_frame = mouth_expression_frame;
        }
        const int frame = std::clamp(
            desired_frame,
            0,
            std::max(0, static_cast<int>(eye.frame_offsets.size()) - 1));
        const float offset_x = eye.frame_offsets.empty() ? 0.0f : eye.frame_offsets[static_cast<std::size_t>(frame)][0];
        const float offset_y = eye.frame_offsets.empty() ? 0.0f : eye.frame_offsets[static_cast<std::size_t>(frame)][1];
        u = src.u * std::abs(eye.scale_x) + offset_x;
        v = src.v + offset_y;
    }
    return Vertex{src.x, src.y, src.z, src.nx, src.ny, src.nz, packAbgr(src.r, src.g, src.b, src.a), u, v};
}

} // namespace

class AttendBgfxRenderer::Impl {
public:
    Impl(std::string project_root, AttendSceneConfig config)
        : project_root_(std::move(project_root)),
          config_(std::move(config)),
          world_viewport_(loadSharedWorldViewportConfig(project_root_)) {}
    ~Impl() { shutdown(); }

    bool initialize(SDL_Window* window, int width, int height, const std::string& bgfx_preference, void* sdl_metal_view);
    void shutdown();
    bool valid() const { return initialized_ && backend_.valid(); }
    std::string lastError() const { return last_error_; }
    void setPetting(bool petting) { petting_ = petting; }
    void setPetContact(float vertical_bias) {
        pet_contact_target_ = std::clamp(vertical_bias, -1.0f, 1.0f);
    }
    void setFaceLook(float x, float y) {
        face_target_x_ = std::clamp(x, -1.0f, 1.0f);
        face_target_y_ = std::clamp(y, -1.0f, 1.0f);
    }
    void setViewportLook(float x, float y) {
        viewport_look_target_x_ = std::clamp(x, -1.0f, 1.0f);
        viewport_look_target_y_ = std::clamp(y, -1.0f, 1.0f);
    }
    void setFaceView(bool face_view) { face_view_ = face_view; }
    void triggerReaction(std::string reaction_id, double interaction_seconds) {
        pending_reaction_id_ = std::move(reaction_id);
        pending_reaction_interaction_seconds_ = interaction_seconds;
    }
    bool faceViewAvailable() const {
        return pokemon_bounds_valid_ &&
            pokemon_world_height_ >= config_.camera.face_view_min_model_height;
    }
    SDL_Rect pokemonPointerRect() const { return last_pokemon_pointer_rect_; }
    void setWeatherMode(int index) {
        const int count = static_cast<int>(config_.floor.weather_modes.size());
        floor_weather_index_ = count > 0 ? ((index % count) + count) % count : 0;
    }
    void setTextureVariant(int index) {
        const int count = static_cast<int>(pokemon_model_.texture_variants.size());
        texture_variant_index_ = count > 0 ? ((index % count) + count) % count : 0;
    }
    void setFormVariant(int index) {
        const int count = static_cast<int>(pokemon_model_.form_variants.size());
        form_variant_index_ = count > 0 ? ((index % count) + count) % count : 0;
    }
    void setOverlayButtons(std::vector<AttendBgfxOverlayButton> buttons, int logical_w, int logical_h) {
        overlay_buttons_ = std::move(buttons);
        overlay_logical_w_ = std::max(1, logical_w);
        overlay_logical_h_ = std::max(1, logical_h);
        overlay_button_textures_.resize(overlay_buttons_.size());
    }
    int weatherModeCount() const { return static_cast<int>(config_.floor.weather_modes.size()); }
    int textureVariantIndex() const { return texture_variant_index_; }
    int textureVariantCount() const { return static_cast<int>(pokemon_model_.texture_variants.size()); }
    std::string textureVariantLabel(int index) const {
        if (index >= 0 && index < static_cast<int>(pokemon_model_.texture_variants.size())) {
            const AttendTextureVariantOption& option = pokemon_model_.texture_variants[static_cast<std::size_t>(index)];
            return option.label.empty() ? option.id : option.label;
        }
        return "Normal";
    }
    int formVariantIndex() const { return form_variant_index_; }
    int formVariantCount() const { return static_cast<int>(pokemon_model_.form_variants.size()); }
    std::string formVariantLabel(int index) const {
        if (index >= 0 && index < static_cast<int>(pokemon_model_.form_variants.size())) {
            const AttendTextureVariantOption& option = pokemon_model_.form_variants[static_cast<std::size_t>(index)];
            return option.label.empty() ? option.id : option.label;
        }
        return "Form";
    }
    void render(double scene_time_seconds, int width, int height);
    void queueScreenshot(const std::string& output_path) { backend_.queueScreenshot(output_path); }

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
    float pokemon_world_height_ = 0.0f;
    float pokemon_min_x_ = 0.0f;
    float pokemon_min_y_ = 0.0f;
    float pokemon_min_z_ = 0.0f;
    float pokemon_max_x_ = 0.0f;
    float pokemon_max_y_ = 0.0f;
    float pokemon_max_z_ = 0.0f;
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
    bool reaction_active_ = false;
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
    int texture_variant_index_ = 0;
    int form_variant_index_ = 0;
    int floor_weather_index_ = 0;
    std::vector<AttendBgfxOverlayButton> overlay_buttons_;
    int overlay_logical_w_ = 1280;
    int overlay_logical_h_ = 800;
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
    void scheduleNextRandomBlink(double scene_time_seconds);
    void updateRandomBlink(double scene_time_seconds);
    void updatePetControls(double scene_time_seconds);
    void updateFaceControls(double scene_time_seconds);
    void updateViewportLook(double scene_time_seconds);
    void updateFloorAnimation(double scene_time_seconds);
    void updatePokemonAnimation(double scene_time_seconds);
    void startPendingReaction(double scene_time_seconds);
    float reactionWeight(double scene_time_seconds) const;
    const AttendPokemonAnimation* resolveSemanticAnimation(const std::string& semantic) const;
    int eyeExpressionFrame(const std::string& semantic, int fallback) const;
    int mouthExpressionFrame(const std::string& semantic, int fallback) const;
    const AttendInteractionAdapterConfig::ReactionCombo* reactionCombo(const std::string& id) const;
    float petReadyCueWeight(double scene_time_seconds, const AttendInteractionAdapterConfig::ReactionCombo& combo) const;
    bool floorMaterialVisible(const MaterialResource* material) const;
    bool pokemonPrimitiveVisibleForHit(std::size_t primitive_index) const;
    void updatePokemonBoundsFromVertices(const std::vector<Vertex>& vertices);
    void cameraForFrame(float look_x, float look_y, bx::Vec3& eye, bx::Vec3& at) const;
    void updatePokemonPointerRect(const float* pokemon_matrix, const float* view, const float* proj, int width, int height);
    void submitMesh(const MeshResource& mesh, const float* matrix, bool force_blend = false, bool backdrop = false, bool floor = false);
    void submitPokemonShadow();
    void submitPixelSceneToBackbuffer(int framebuffer_w, int framebuffer_h, int source_w, int source_h);
    void submitOverlayButtons(int framebuffer_w, int framebuffer_h);
};

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
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
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
    bgfx::ShaderHandle vs_eye = loadShader(shader_root, backend_.shaderSubdirectory(), "vs_world");
    bgfx::ShaderHandle fs_eye = loadShader(shader_root, backend_.shaderSubdirectory(), "fs_pokemon_eye");
    bgfx::ShaderHandle vs_eye_mask = loadShader(shader_root, backend_.shaderSubdirectory(), "vs_world");
    bgfx::ShaderHandle fs_eye_mask = loadShader(shader_root, backend_.shaderSubdirectory(), "fs_eye_sclera_mask");
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs) || !bgfx::isValid(vs_eye) || !bgfx::isValid(fs_eye) ||
        !bgfx::isValid(vs_eye_mask) || !bgfx::isValid(fs_eye_mask)) {
        if (bgfx::isValid(vs)) bgfx::destroy(vs);
        if (bgfx::isValid(fs)) bgfx::destroy(fs);
        if (bgfx::isValid(vs_eye)) bgfx::destroy(vs_eye);
        if (bgfx::isValid(fs_eye)) bgfx::destroy(fs_eye);
        if (bgfx::isValid(vs_eye_mask)) bgfx::destroy(vs_eye_mask);
        if (bgfx::isValid(fs_eye_mask)) bgfx::destroy(fs_eye_mask);
        last_error_ = "Could not load attend shaders";
        return false;
    }
    program_ = bgfx::createProgram(vs, fs, true);
    eye_program_ = bgfx::createProgram(vs_eye, fs_eye, true);
    eye_sclera_mask_program_ = bgfx::createProgram(vs_eye_mask, fs_eye_mask, true);
    if (!bgfx::isValid(program_) || !bgfx::isValid(eye_program_) || !bgfx::isValid(eye_sclera_mask_program_)) {
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

bool AttendBgfxRenderer::Impl::buildStaticGlbMesh(
    MeshResource& mesh,
    const std::string& path,
    float x,
    float y,
    float z,
    float yaw_degrees,
    float scale) {
    if (path.empty()) return false;
    std::string error;
    const pr::gameplay::world3d::data::GlbMesh src =
        pr::gameplay::world3d::data::loadGlbModel(path, &error);
    if (!src.valid || src.triangles.empty()) {
        std::cerr << "[AttendBgfx] Could not load static GLB " << path << ": " << error << std::endl;
        return false;
    }

    mesh.materials.resize(src.materials.size());
    for (std::size_t i = 0; i < src.materials.size(); ++i) {
        const auto& in = src.materials[i];
        MaterialResource& out = mesh.materials[i];
        out.name = in.name;
        out.visible = !containsAnySubstring(in.name, config_.floor.hidden_material_substrings);
        std::copy(std::begin(in.base_color), std::end(in.base_color), std::begin(out.base_color));
        out.alpha_cutoff = in.alpha_cutoff;
        out.sampler_flags = smoothSamplerFlags();
        if (in.has_texture) {
            out.texture = decodeTexture(in.image_bytes, in.name.empty() ? "attend_static_glb" : in.name.c_str());
        }
        out.blend = in.alpha_mode == pr::gameplay::world3d::data::GlbMaterial::AlphaMode::Blend ||
                    out.base_color[3] < 0.999f;
        out.mask_cutout = in.alpha_mode == pr::gameplay::world3d::data::GlbMaterial::AlphaMode::Mask ||
                          (out.texture.has_zero_alpha && out.base_color[3] >= 0.999f);
        if (out.mask_cutout) {
            out.alpha_cutoff = std::max(out.alpha_cutoff, 0.5f);
        }
    }
    if (mesh.materials.empty()) {
        mesh.materials.push_back(MaterialResource{});
    }

    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(src.triangles.size() * 3);
    indices.reserve(src.triangles.size() * 3);
    const int material_count = static_cast<int>(mesh.materials.size());
    for (int material = 0; material < material_count; ++material) {
        const std::uint32_t start = static_cast<std::uint32_t>(indices.size());
        for (const auto& tri : src.triangles) {
            const int tri_material = tri.material >= 0 ? tri.material : 0;
            if (tri_material != material) continue;
            const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
            vertices.push_back(transformStaticVertex(tri.a, x, y, z, yaw_degrees, scale));
            vertices.push_back(transformStaticVertex(tri.b, x, y, z, yaw_degrees, scale));
            vertices.push_back(transformStaticVertex(tri.c, x, y, z, yaw_degrees, scale));
            indices.insert(indices.end(), {base, base + 1, base + 2});
        }
        const std::uint32_t count = static_cast<std::uint32_t>(indices.size()) - start;
        if (count > 0) {
            mesh.ranges.push_back(MeshResource::Range{start, count, material});
        }
    }
    return uploadMesh(mesh, vertices, indices);
}

bool AttendBgfxRenderer::Impl::buildAnimatedFloor() {
    if (config_.floor.model_path.empty()) return false;
    std::string error;
    floor_model_ = loadAttendPokemonModel(config_.floor.model_path, &error);
    if (!floor_model_.valid || floor_model_.primitives.empty()) {
        floor_model_ = AttendPokemonModel{};
        return false;
    }
    floor_animation_ = findAttendPokemonAnimation(floor_model_, config_.floor.animation_name);
    if (!floor_animation_ || floor_animation_->channels.empty()) {
        floor_model_ = AttendPokemonModel{};
        floor_animation_ = nullptr;
        return false;
    }

    floor_mesh_.materials.resize(floor_model_.materials.size());
    for (std::size_t i = 0; i < floor_model_.materials.size(); ++i) {
        const AttendPokemonMaterial& src = floor_model_.materials[i];
        MaterialResource& dst = floor_mesh_.materials[i];
        dst.name = src.name;
        dst.visible = !containsAnySubstring(src.name, config_.floor.hidden_material_substrings);
        std::copy(std::begin(src.base_color), std::end(src.base_color), std::begin(dst.base_color));
        dst.alpha_cutoff = src.alpha_cutoff;
        dst.pokemon_eye = false;
        dst.sampler_flags = smoothSamplerFlags(src.base_color_sampler.wrap_s, src.base_color_sampler.wrap_t);
        if (src.has_base_color_texture) {
            dst.texture = decodeTexture(
                src.base_color_bytes,
                src.name.empty() ? config_.floor.id.c_str() : src.name.c_str());
        }
        dst.blend = src.render_class == AttendRenderClass::Blend ||
                    src.render_class == AttendRenderClass::UniformDecal ||
                    dst.base_color[3] < 0.999f;
        dst.mask_cutout = src.render_class == AttendRenderClass::Mask;
        if (!src.has_rae_policy && !src.has_alpha_mode) {
            dst.blend = dst.blend || dst.texture.has_partial_alpha;
            dst.mask_cutout = dst.texture.has_zero_alpha && !dst.blend && dst.base_color[3] >= 0.999f;
        }
        if (dst.mask_cutout) {
            dst.alpha_cutoff = std::max(dst.alpha_cutoff, 0.5f);
        }
    }
    if (floor_mesh_.materials.empty()) {
        floor_mesh_.materials.push_back(MaterialResource{});
    }

    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    floor_skinned_primitives_.resize(floor_model_.primitives.size());
    const std::vector<std::array<float, 16>> floor_globals =
        buildAttendPokemonGlobals(floor_model_, floor_animation_, 0.0);
    const std::vector<std::vector<std::array<float, 16>>> floor_skin_matrices =
        buildAttendPokemonSkinMatrices(floor_model_, floor_globals);
    for (std::size_t primitive_index = 0; primitive_index < floor_model_.primitives.size(); ++primitive_index) {
        skinAttendPokemonPrimitiveWithPose(
            floor_model_,
            floor_model_.primitives[primitive_index],
            floor_globals,
            floor_skin_matrices,
            floor_skinned_primitives_[primitive_index]);
    }
    for (std::size_t mat = 0; mat < floor_mesh_.materials.size(); ++mat) {
        const std::uint32_t start = static_cast<std::uint32_t>(indices.size());
        for (std::size_t primitive_index = 0; primitive_index < floor_model_.primitives.size(); ++primitive_index) {
            const AttendPokemonPrimitive& primitive = floor_model_.primitives[primitive_index];
            if (primitive.material != static_cast<int>(mat)) continue;
            const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
            const std::vector<AttendPokemonVertex>& skinned = floor_skinned_primitives_[primitive_index];
            for (const AttendPokemonVertex& v : skinned) {
                vertices.push_back(transformAttendVertex(
                    v,
                    config_.floor.model_x,
                    config_.floor.model_y,
                    config_.floor.model_z,
                    config_.floor.model_yaw_degrees,
                    config_.floor.model_scale));
            }
            for (std::uint32_t index : primitive.indices) {
                indices.push_back(base + index);
            }
        }
        const std::uint32_t count = static_cast<std::uint32_t>(indices.size()) - start;
        if (count > 0) {
            floor_mesh_.ranges.push_back(MeshResource::Range{start, count, static_cast<int>(mat)});
        }
    }
    floor_frame_vertices_ = vertices;
    floor_animated_ = uploadDynamicMesh(floor_mesh_, vertices, indices);
    if (!floor_animated_) {
        floor_model_ = AttendPokemonModel{};
        floor_animation_ = nullptr;
        floor_skinned_primitives_.clear();
        floor_frame_vertices_.clear();
    }
    return floor_animated_;
}

bool AttendBgfxRenderer::Impl::buildFloor() {
    if (!config_.floor.enabled) {
        return true;
    }
    bool floor_built = buildAnimatedFloor();
    if (!floor_built) {
        floor_mesh_.destroy();
        floor_built = !config_.floor.model_path.empty() &&
            buildStaticGlbMesh(
                floor_mesh_,
                config_.floor.model_path,
                config_.floor.model_x,
                config_.floor.model_y,
                config_.floor.model_z,
                config_.floor.model_yaw_degrees,
                config_.floor.model_scale);
    }

    if (!floor_built) {
        last_error_ = "Attend environment floor model could not be loaded: " + config_.floor.model_path;
        return false;
    }

    for (MeshResource& mesh : floor_extension_meshes_) {
        mesh.destroy();
    }
    floor_extension_meshes_.clear();
    floor_extension_meshes_.reserve(config_.floor.extensions.size());
    for (const AttendFloorExtensionConfig& extension : config_.floor.extensions) {
        if (!extension.enabled || extension.model_path.empty()) continue;
        MeshResource mesh;
        if (!buildStaticGlbMesh(
                mesh,
                extension.model_path,
                extension.model_x,
                extension.model_y,
                extension.model_z,
                extension.model_yaw_degrees,
                extension.model_scale)) {
            last_error_ = "Attend environment floor extension model could not be loaded: " + extension.model_path;
            mesh.destroy();
            return false;
        }
        floor_extension_meshes_.push_back(std::move(mesh));
    }
    return true;
}

bool AttendBgfxRenderer::Impl::buildWall() {
    if (!config_.wall.enabled) return true;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    const int segments = std::max(4, config_.wall.segments);
    const int vertical = std::max(1, config_.wall.vertical_segments);
    if (config_.wall.shape == "dome" || config_.wall.shape == "sphere") {
        const int dome_segments = std::max(24, segments);
        const int dome_vertical = std::max(12, vertical);
        const float radius = std::max(8.0f, config_.wall.radius);
        vertices.reserve(static_cast<std::size_t>((dome_segments + 1) * (dome_vertical + 1)));
        for (int y = 0; y <= dome_vertical; ++y) {
            const float vt = static_cast<float>(y) / static_cast<float>(dome_vertical);
            const float phi = -kPi * 0.5f + vt * kPi;
            const float ring_radius = std::cos(phi) * radius;
            const float py = config_.wall.bottom_y + std::sin(phi) * radius;
            Color3 color = sampleGradient(config_.wall.gradient_colors, vt);
            const float horizon = 1.0f - std::clamp(config_.wall.edge_darkening, 0.0f, 0.95f) *
                std::pow(std::max(0.0f, 0.35f - vt) / 0.35f, 2.0f) * 0.35f;
            for (int s = 0; s <= dome_segments; ++s) {
                const float st = static_cast<float>(s) / static_cast<float>(dome_segments);
                const float theta = st * kPi * 2.0f;
                float nx = -std::sin(theta) * std::cos(phi);
                float ny = -std::sin(phi);
                float nz = -std::cos(theta) * std::cos(phi);
                normalize3(nx, ny, nz);
                vertices.push_back(Vertex{
                    std::sin(theta) * ring_radius,
                    py,
                    std::cos(theta) * ring_radius,
                    nx,
                    ny,
                    nz,
                    packAbgr(color.r * horizon, color.g * horizon, color.b * horizon),
                    st,
                    vt});
            }
        }
        for (int y = 0; y < dome_vertical; ++y) {
            for (int s = 0; s < dome_segments; ++s) {
                const std::uint32_t a = static_cast<std::uint32_t>(y * (dome_segments + 1) + s);
                const std::uint32_t b = a + 1;
                const std::uint32_t c = static_cast<std::uint32_t>((y + 1) * (dome_segments + 1) + s);
                const std::uint32_t d = c + 1;
                indices.insert(indices.end(), {a, b, c, b, d, c});
            }
        }
        wall_mesh_.materials.push_back(MaterialResource{});
        wall_mesh_.ranges.push_back(MeshResource::Range{0, static_cast<std::uint32_t>(indices.size()), 0});
        return uploadMesh(wall_mesh_, vertices, indices);
    }
    const float half_arc = config_.wall.arc_degrees * 0.5f * (kPi / 180.0f);
    vertices.reserve(static_cast<std::size_t>((segments + 1) * (vertical + 1)));
    for (int y = 0; y <= vertical; ++y) {
        const float vt = static_cast<float>(y) / static_cast<float>(vertical);
        Color3 color = sampleGradient(config_.wall.gradient_colors, vt);
        for (int s = 0; s <= segments; ++s) {
            const float st = static_cast<float>(s) / static_cast<float>(segments);
            const float a = -half_arc + st * half_arc * 2.0f;
            const float edge = std::abs(st - 0.5f) * 2.0f;
            const float shade = 1.0f - config_.wall.edge_darkening * edge * edge;
            float nx = -std::sin(a);
            float ny = 0.0f;
            float nz = std::cos(a);
            normalize3(nx, ny, nz);
            vertices.push_back(Vertex{
                std::sin(a) * config_.wall.radius,
                config_.wall.bottom_y + vt * config_.wall.height,
                -config_.wall.distance - std::cos(a) * config_.wall.radius + config_.wall.radius,
                nx,
                ny,
                nz,
                packAbgr(color.r * shade, color.g * shade, color.b * shade),
                st,
                vt});
        }
    }
    for (int y = 0; y < vertical; ++y) {
        for (int s = 0; s < segments; ++s) {
            const std::uint32_t a = static_cast<std::uint32_t>(y * (segments + 1) + s);
            const std::uint32_t b = a + 1;
            const std::uint32_t c = static_cast<std::uint32_t>((y + 1) * (segments + 1) + s);
            const std::uint32_t d = c + 1;
            indices.insert(indices.end(), {a, c, b, b, c, d});
        }
    }
    wall_mesh_.materials.push_back(MaterialResource{});
    wall_mesh_.ranges.push_back(MeshResource::Range{0, static_cast<std::uint32_t>(indices.size()), 0});
    return uploadMesh(wall_mesh_, vertices, indices);
}

AttendBgfxRenderer::Impl::TextureResource AttendBgfxRenderer::Impl::decodeTexture(
    const std::vector<std::uint8_t>& bytes,
    const char* debug_name) {
    TextureResource out;
    if (bytes.empty()) return out;
    SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
    if (!rw) return out;
    SDL_Surface* loaded = IMG_Load_RW(rw, 1);
    if (!loaded) return out;
    SDL_Surface* rgba = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(loaded);
    if (!rgba) return out;
    out.width = rgba->w;
    out.height = rgba->h;
    const auto* pixels = static_cast<const std::uint8_t*>(rgba->pixels);
    for (int i = 0; i < rgba->w * rgba->h; ++i) {
        const std::uint8_t alpha = pixels[i * 4 + 3];
        if (alpha == 0) out.has_zero_alpha = true;
        if (alpha > 0 && alpha < 255) out.has_partial_alpha = true;
    }
    const bgfx::Memory* mem = bgfx::copy(rgba->pixels, static_cast<std::uint32_t>(rgba->w * rgba->h * 4));
    out.handle = bgfx::createTexture2D(
        static_cast<std::uint16_t>(rgba->w),
        static_cast<std::uint16_t>(rgba->h),
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        samplerFlags(),
        mem);
    if (out.valid()) bgfx::setName(out.handle, debug_name);
    SDL_FreeSurface(rgba);
    return out;
}

AttendBgfxRenderer::Impl::TextureResource AttendBgfxRenderer::Impl::buildPokemonEyeTexture(
    const std::vector<std::uint8_t>& base_bytes,
    const std::vector<std::uint8_t>& lym_bytes,
    const char* debug_name) {
    TextureResource out;
    if (base_bytes.empty() || lym_bytes.empty()) return out;

    SDL_RWops* base_rw = SDL_RWFromConstMem(base_bytes.data(), static_cast<int>(base_bytes.size()));
    SDL_RWops* lym_rw = SDL_RWFromConstMem(lym_bytes.data(), static_cast<int>(lym_bytes.size()));
    if (!base_rw || !lym_rw) {
        if (base_rw) SDL_RWclose(base_rw);
        if (lym_rw) SDL_RWclose(lym_rw);
        return out;
    }
    SDL_Surface* loaded_base = IMG_Load_RW(base_rw, 1);
    SDL_Surface* loaded_lym = IMG_Load_RW(lym_rw, 1);
    if (!loaded_base || !loaded_lym) {
        if (loaded_base) SDL_FreeSurface(loaded_base);
        if (loaded_lym) SDL_FreeSurface(loaded_lym);
        return out;
    }
    SDL_Surface* base = SDL_ConvertSurfaceFormat(loaded_base, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_Surface* lym = SDL_ConvertSurfaceFormat(loaded_lym, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(loaded_base);
    SDL_FreeSurface(loaded_lym);
    if (!base || !lym) {
        if (base) SDL_FreeSurface(base);
        if (lym) SDL_FreeSurface(lym);
        return out;
    }

    const auto* base_pixels = static_cast<const std::uint8_t*>(base->pixels);
    const auto* lym_pixels = static_cast<const std::uint8_t*>(lym->pixels);
    int base_alpha_pixels = 0;
    int base_nonwhite_pixels = 0;
    int base_zero_alpha_pixels = 0;
    int base_partial_alpha_pixels = 0;
    for (int i = 0; i < base->w * base->h; ++i) {
        const std::size_t src = static_cast<std::size_t>(i * 4);
        const std::uint8_t r = base_pixels[src + 0];
        const std::uint8_t g = base_pixels[src + 1];
        const std::uint8_t b = base_pixels[src + 2];
        const std::uint8_t a = base_pixels[src + 3];
        if (a == 0) {
            ++base_zero_alpha_pixels;
            continue;
        }
        if (a < 255) ++base_partial_alpha_pixels;
        ++base_alpha_pixels;
        if (r < 245 || g < 245 || b < 245) ++base_nonwhite_pixels;
    }
    const float base_nonwhite_ratio = base_alpha_pixels > 0
        ? static_cast<float>(base_nonwhite_pixels) / static_cast<float>(base_alpha_pixels)
        : 0.0f;
    if (base_nonwhite_ratio > 0.05f) {
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(base->w * base->h * 4));
        std::copy(base_pixels, base_pixels + rgba.size(), rgba.begin());
        const bgfx::Memory* mem = bgfx::copy(rgba.data(), static_cast<std::uint32_t>(rgba.size()));
        out.handle = bgfx::createTexture2D(
            static_cast<std::uint16_t>(base->w),
            static_cast<std::uint16_t>(base->h),
            false,
            1,
            bgfx::TextureFormat::RGBA8,
            samplerFlags(),
            mem);
        out.has_zero_alpha = base_zero_alpha_pixels > 0;
        out.has_partial_alpha = base_partial_alpha_pixels > 0;
        if (out.valid()) bgfx::setName(out.handle, debug_name);
        SDL_FreeSurface(base);
        SDL_FreeSurface(lym);
        return out;
    }

    int mask_min_x = lym->w;
    int mask_min_y = lym->h;
    int mask_max_x = -1;
    int mask_max_y = -1;
    for (int y = 0; y < lym->h; ++y) {
        for (int x = 0; x < lym->w; ++x) {
            const std::size_t src = static_cast<std::size_t>((y * lym->w + x) * 4);
            if (lym_pixels[src + 3] > 0) {
                mask_min_x = std::min(mask_min_x, x);
                mask_min_y = std::min(mask_min_y, y);
                mask_max_x = std::max(mask_max_x, x);
                mask_max_y = std::max(mask_max_y, y);
            }
        }
    }
    const bool has_mask = mask_max_x >= mask_min_x && mask_max_y >= mask_min_y;
    const float mask_center_x = has_mask ? (static_cast<float>(mask_min_x + mask_max_x) * 0.5f) : 0.0f;
    const float mask_center_y = has_mask ? (static_cast<float>(mask_min_y + mask_max_y) * 0.5f) : 0.0f;
    const float mask_radius_x = has_mask ? std::max(3.5f, static_cast<float>(mask_max_x - mask_min_x + 1) * 0.62f) : 1.0f;
    const float mask_radius_y = has_mask ? std::max(3.5f, static_cast<float>(mask_max_y - mask_min_y + 1) * 0.70f) : 1.0f;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(lym->w * lym->h * 4));
    for (int y = 0; y < lym->h; ++y) {
        for (int x = 0; x < lym->w; ++x) {
            const std::size_t src = static_cast<std::size_t>((y * lym->w + x) * 4);
            const int base_x = std::clamp(
                static_cast<int>((static_cast<float>(x) / static_cast<float>(std::max(1, lym->w - 1))) * static_cast<float>(base->w - 1) + 0.5f),
                0,
                std::max(0, base->w - 1));
            const int base_y = std::clamp(
                static_cast<int>((static_cast<float>(y) / static_cast<float>(std::max(1, lym->h - 1))) * static_cast<float>(base->h - 1) + 0.5f),
                0,
                std::max(0, base->h - 1));
            const std::size_t base_src = static_cast<std::size_t>((base_y * base->w + base_x) * 4);
            const float dx = (static_cast<float>(x) - mask_center_x) / mask_radius_x;
            const float dy = (static_cast<float>(y) - mask_center_y) / mask_radius_y;
            const bool pupil = has_mask && (dx * dx + dy * dy) <= 1.0f;
            rgba[src + 0] = pupil ? 22 : base_pixels[base_src + 0];
            rgba[src + 1] = pupil ? 14 : base_pixels[base_src + 1];
            rgba[src + 2] = pupil ? 9 : base_pixels[base_src + 2];
            rgba[src + 3] = 255;
        }
    }

    const bgfx::Memory* mem = bgfx::copy(rgba.data(), static_cast<std::uint32_t>(rgba.size()));
    out.handle = bgfx::createTexture2D(
        static_cast<std::uint16_t>(lym->w),
        static_cast<std::uint16_t>(lym->h),
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        samplerFlags(),
        mem);
    if (out.valid()) bgfx::setName(out.handle, debug_name);
    SDL_FreeSurface(base);
    SDL_FreeSurface(lym);
    return out;
}

bool AttendBgfxRenderer::Impl::buildPokemon() {
    std::string error;
    pokemon_model_ = loadAttendPokemonModel(config_.pokemon.model_path, &error);
    if (!pokemon_model_.valid) {
        last_error_ = "Could not load attend Pokemon GLB: " + error;
        return false;
    }
    pokemon_animation_ = findAttendPokemonAnimation(pokemon_model_, config_.pokemon.animation_name);
    if (!pokemon_animation_) {
        last_error_ = "Attend Pokemon GLB has no animation channels";
        return false;
    }
    texture_variant_index_ = 0;
    for (std::size_t i = 0; i < pokemon_model_.texture_variants.size(); ++i) {
        if (pokemon_model_.texture_variants[i].id == pokemon_model_.default_texture_variant) {
            texture_variant_index_ = static_cast<int>(i);
            break;
        }
    }
    form_variant_index_ = 0;
    for (std::size_t i = 0; i < pokemon_model_.form_variants.size(); ++i) {
        if (pokemon_model_.form_variants[i].id == pokemon_model_.default_form_variant) {
            form_variant_index_ = static_cast<int>(i);
            break;
        }
    }
    pokemon_eye_close_animation_ = nullptr;
    if (!config_.interaction_adapter.eye_close_animation.empty()) {
        for (const AttendPokemonAnimation& animation : pokemon_model_.animations) {
            if (animation.name == config_.interaction_adapter.eye_close_animation) {
                pokemon_eye_close_animation_ = &animation;
                break;
            }
        }
    }
    if (!pokemon_eye_close_animation_) {
        pokemon_eye_close_animation_ = findAttendPokemonAnimation(pokemon_model_, "eye");
        if (pokemon_eye_close_animation_ == pokemon_animation_) {
            pokemon_eye_close_animation_ = nullptr;
        }
    }
    normal_eye_expression_frame_ = expressionFrameOr(config_, "normal_open", 0);
    closed_eye_expression_frame_ = expressionFrameOr(config_, "closed", 4);
    current_eye_expression_frame_ = normal_eye_expression_frame_;
    normal_mouth_expression_frame_ = mouthExpressionFrameOr(config_, "closed_normal", 0);
    current_mouth_expression_frame_ = normal_mouth_expression_frame_;
    has_eye_expression_frames_ = false;
    has_mouth_expression_frames_ = false;
    for (const AttendPokemonMaterial& material : pokemon_model_.materials) {
        if (materialUsesEyeExpressionFrames(&material)) {
            has_eye_expression_frames_ = true;
        }
        if (materialUsesMouthExpressionFrames(&material)) {
            has_mouth_expression_frames_ = true;
        }
    }

    pokemon_mesh_.materials.resize(pokemon_model_.materials.size());
    for (std::size_t i = 0; i < pokemon_model_.materials.size(); ++i) {
        const AttendPokemonMaterial& src = pokemon_model_.materials[i];
        MaterialResource& dst = pokemon_mesh_.materials[i];
        dst.name = src.name;
        std::copy(std::begin(src.base_color), std::end(src.base_color), std::begin(dst.base_color));
        dst.alpha_cutoff = src.alpha_cutoff;
        dst.sampler_flags = samplerFlags(
            src.base_color_sampler.wrap_s,
            src.base_color_sampler.wrap_t);
        if ((src.material_role == AttendMaterialRole::EyeSclera ||
             src.material_role == AttendMaterialRole::Mouth) &&
            src.eye_sheet.enabled) {
            dst.sampler_flags = samplerFlagsFromGfWrap(src.eye_sheet.wrap_s, src.eye_sheet.wrap_t);
        }
        dst.pokemon_eye = src.pokemon_eye;
        dst.eye_sclera_mask = materialIsEyeScleraMask(&src);
        dst.separate_eye_iris = materialIsSeparateEyeIris(&src);
        dst.form_variant_materials = src.form_material_indices;
        dst.texture_variant_materials.assign(pokemon_model_.texture_variants.size(), static_cast<int>(i));
        for (std::size_t variant_index = 0; variant_index < pokemon_model_.texture_variants.size(); ++variant_index) {
            if (pokemon_model_.texture_variants[variant_index].id == "shiny" &&
                src.shiny_material_index >= 0 &&
                src.shiny_material_index < static_cast<int>(pokemon_model_.materials.size())) {
                dst.texture_variant_materials[variant_index] = src.shiny_material_index;
            }
        }
        if (src.pokemon_eye && src.has_base_color_texture && src.has_emissive_texture) {
            const std::string eye_base_name = src.name.empty() ? config_.pokemon.id + "_pupil_eye_base" : src.name + "_pupil_base";
            dst.texture = buildPokemonEyeTexture(
                src.base_color_bytes,
                src.emissive_bytes,
                eye_base_name.c_str());
        } else if (src.has_base_color_texture) {
            dst.texture = decodeTexture(src.base_color_bytes, src.name.empty() ? config_.pokemon.id.c_str() : src.name.c_str());
        }
        if (src.has_emissive_texture) {
            const std::string mask_name = src.name.empty() ? config_.pokemon.id + "_eye_mask" : src.name + "_eye_mask";
            dst.eye_mask_texture = decodeTexture(src.emissive_bytes, mask_name.c_str());
        }
        dst.blend = src.render_class == AttendRenderClass::Blend ||
                    src.render_class == AttendRenderClass::UniformDecal ||
                    dst.base_color[3] < 0.999f;
        dst.mask_cutout = src.render_class == AttendRenderClass::Mask;
        if (!src.has_rae_policy && !src.has_alpha_mode) {
            dst.blend = dst.blend || dst.texture.has_partial_alpha;
            dst.mask_cutout = dst.texture.has_zero_alpha && !dst.blend && dst.base_color[3] >= 0.999f;
        }
        if (dst.mask_cutout) {
            dst.alpha_cutoff = std::max(dst.alpha_cutoff, 0.5f);
        }
        if (dst.separate_eye_iris) {
            dst.mask_cutout = true;
            dst.alpha_cutoff = std::max(dst.alpha_cutoff, 0.5f);
        }
    }

    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    skinned_primitives_.resize(pokemon_model_.primitives.size());
    pokemon_primitive_vertex_offsets_.assign(
        pokemon_model_.primitives.size(),
        static_cast<std::uint32_t>(-1));
    const std::vector<std::array<float, 16>> pokemon_globals =
        buildAttendPokemonGlobals(pokemon_model_, pokemon_animation_, 0.0);
    const std::vector<std::vector<std::array<float, 16>>> pokemon_skin_matrices =
        buildAttendPokemonSkinMatrices(pokemon_model_, pokemon_globals);
    for (std::size_t primitive_index = 0; primitive_index < pokemon_model_.primitives.size(); ++primitive_index) {
        skinAttendPokemonPrimitiveWithPose(
            pokemon_model_,
            pokemon_model_.primitives[primitive_index],
            pokemon_globals,
            pokemon_skin_matrices,
            skinned_primitives_[primitive_index]);
    }
    pokemon_draw_order_.clear();
    pokemon_draw_order_.reserve(pokemon_model_.primitives.size());
    for (std::size_t primitive_index = 0; primitive_index < pokemon_model_.primitives.size(); ++primitive_index) {
        if (pokemonPrimitivePreviewVisible(pokemon_model_, pokemon_model_.primitives[primitive_index])) {
            pokemon_draw_order_.push_back(primitive_index);
        }
    }
    std::stable_sort(
        pokemon_draw_order_.begin(),
        pokemon_draw_order_.end(),
        [this](std::size_t a, std::size_t b) {
            const AttendPokemonPrimitive& lhs = pokemon_model_.primitives[a];
            const AttendPokemonPrimitive& rhs = pokemon_model_.primitives[b];
            if (lhs.render_order != rhs.render_order) return lhs.render_order < rhs.render_order;
            return lhs.scene_order < rhs.scene_order;
        });
    for (std::size_t ordered_index = 0; ordered_index < pokemon_draw_order_.size(); ++ordered_index) {
        const std::size_t primitive_index = pokemon_draw_order_[ordered_index];
        const AttendPokemonPrimitive& primitive = pokemon_model_.primitives[primitive_index];
        const std::uint32_t start = static_cast<std::uint32_t>(indices.size());
        const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
        pokemon_primitive_vertex_offsets_[primitive_index] = base;
        const std::vector<AttendPokemonVertex>& skinned = skinned_primitives_[primitive_index];
        const AttendPokemonMaterial* material =
            primitive.material >= 0 && primitive.material < static_cast<int>(pokemon_model_.materials.size())
                ? &pokemon_model_.materials[static_cast<std::size_t>(primitive.material)]
                : nullptr;
        for (const AttendPokemonVertex& v : skinned) {
            vertices.push_back(pokemonVertexForMaterial(
                v,
                material,
                current_eye_expression_frame_,
                current_mouth_expression_frame_));
        }
        for (std::uint32_t index : primitive.indices) {
            indices.push_back(base + index);
        }
        const std::uint32_t count = static_cast<std::uint32_t>(indices.size()) - start;
        if (count > 0) {
            MeshResource::Range range;
            range.start = start;
            range.count = count;
            range.material = primitive.material;
            range.visible_for_forms = primitive.visible_for_forms;
            pokemon_mesh_.ranges.push_back(std::move(range));
        }
    }
    pokemon_frame_vertices_ = vertices;
    pokemon_frame_vertex_primitives_.clear();
    pokemon_frame_vertex_primitives_.reserve(vertices.size());
    for (std::size_t primitive_index : pokemon_draw_order_) {
        if (primitive_index >= pokemon_model_.primitives.size()) continue;
        const std::size_t count = pokemon_model_.primitives[primitive_index].vertices.size();
        pokemon_frame_vertex_primitives_.insert(
            pokemon_frame_vertex_primitives_.end(),
            count,
            primitive_index);
    }
    updatePokemonBoundsFromVertices(pokemon_frame_vertices_);
    return uploadDynamicMesh(pokemon_mesh_, vertices, indices);
}

void AttendBgfxRenderer::Impl::updatePokemonAnimation(double scene_time_seconds) {
    if (!pokemon_mesh_.dynamic || !bgfx::isValid(pokemon_mesh_.dvbh) || pokemon_model_.primitives.empty()) return;
    startPendingReaction(scene_time_seconds);
    if (reaction_active_ && scene_time_seconds >= reaction_start_seconds_ + reaction_duration_seconds_) {
        reaction_active_ = false;
        reaction_animation_ = nullptr;
    }
    const float look_distance = std::sqrt(face_look_x_ * face_look_x_ + face_look_y_ * face_look_y_);
    const float look_weight = std::clamp((look_distance - 0.10f) / 0.90f, 0.0f, 1.0f);
    const float head_look_strength = std::max(0.0f, config_.interaction_adapter.head_look_strength);
    const float pet_contact_pitch_bias = pet_contact_bias_ >= 0.0f
        ? pet_contact_bias_ * 8.5f
        : pet_contact_bias_ * 5.0f;
    const float head_pitch_degrees = (-face_look_y_ * 5.5f * head_look_strength) + pet_contact_pitch_bias;
    const float head_weight = std::clamp(std::max(look_weight, std::abs(pet_contact_bias_) * 0.9f), 0.0f, 1.0f);
    const float eye_close_amount = std::max(pet_eye_close_amount_, random_blink_amount_);
    const bool reaction_eye_visible = reaction_active_ || scene_time_seconds < reaction_eye_linger_until_seconds_;
    current_eye_expression_frame_ = reaction_eye_visible
        ? reaction_eye_expression_frame_
        : (eye_close_amount > 0.38f ? closed_eye_expression_frame_ : normal_eye_expression_frame_);
    current_mouth_expression_frame_ = reaction_active_
        ? reaction_mouth_expression_frame_
        : normal_mouth_expression_frame_;
    const AttendPokemonAnimation* eyelid_animation =
        reaction_eye_visible ? nullptr : (pokemon_eye_close_animation_ ? pokemon_eye_close_animation_ : nullptr);
    const float reaction_weight = reactionWeight(scene_time_seconds);
    const AttendInteractionAdapterConfig::ReactionCombo* pet_happy_combo = reactionCombo("pet_happy");
    const float ready_weight = (!reaction_active_ && petting_ && pet_happy_combo)
        ? petReadyCueWeight(scene_time_seconds, *pet_happy_combo)
        : 0.0f;
    const AttendPokemonAnimation* ready_animation = ready_weight > 0.0f && pet_happy_combo
        ? resolveSemanticAnimation(pet_happy_combo->ready_animation_semantic)
        : nullptr;
    const bool using_reaction_overlay = reaction_active_ && reaction_animation_;
    const bool using_ready_overlay = !using_reaction_overlay && ready_animation && ready_weight > 0.0f;
    if (!reaction_active_ && ready_weight > 0.0f && pet_happy_combo) {
        current_mouth_expression_frame_ =
            mouthExpressionFrame(pet_happy_combo->ready_mouth_expression, normal_mouth_expression_frame_);
    }
    const AttendPokemonPoseOverlay eye_close_overlay{
        using_reaction_overlay ? reaction_animation_ : (using_ready_overlay ? ready_animation : eyelid_animation),
        using_reaction_overlay
            ? scene_time_seconds - reaction_start_seconds_
            : (using_ready_overlay && pet_started_seconds_ >= 0.0
                ? scene_time_seconds - pet_started_seconds_
                : config_.interaction_adapter.eye_close_time_seconds),
        using_reaction_overlay ? reaction_weight : (using_ready_overlay ? ready_weight : (reaction_eye_visible ? 0.0f : eye_close_amount)),
        !(using_reaction_overlay || using_ready_overlay),
        using_reaction_overlay || using_ready_overlay ? std::vector<std::string>{} : config_.interaction_adapter.eyelid_node_substrings,
        -face_look_x_ * 8.0f * head_look_strength,
        head_pitch_degrees,
        head_weight,
        config_.interaction_adapter.head_node_names,
        {
            config_.interaction_adapter.head_yaw_axis[0],
            config_.interaction_adapter.head_yaw_axis[1],
            config_.interaction_adapter.head_yaw_axis[2]},
        {
            config_.interaction_adapter.head_pitch_axis[0],
            config_.interaction_adapter.head_pitch_axis[1],
            config_.interaction_adapter.head_pitch_axis[2]}};
    const std::vector<std::array<float, 16>> globals =
        buildAttendPokemonGlobals(
            pokemon_model_,
            pokemon_animation_,
            scene_time_seconds,
            eye_close_overlay);
    const std::vector<std::vector<std::array<float, 16>>> skin_matrices =
        buildAttendPokemonSkinMatrices(pokemon_model_, globals);
    if (pokemon_frame_vertices_.size() != pokemon_mesh_.vertex_count ||
        pokemon_primitive_vertex_offsets_.size() != pokemon_model_.primitives.size()) {
        return;
    }
    bool updated_any = false;
    for (std::size_t primitive_index : pokemon_draw_order_) {
        if (primitive_index >= pokemon_model_.primitives.size()) continue;
        if (!pokemonPrimitiveVisibleForHit(primitive_index)) continue;
        const AttendPokemonPrimitive& primitive = pokemon_model_.primitives[primitive_index];
        if (primitive_index >= pokemon_primitive_vertex_offsets_.size()) continue;
        const std::uint32_t base = pokemon_primitive_vertex_offsets_[primitive_index];
        if (base == static_cast<std::uint32_t>(-1) ||
            static_cast<std::size_t>(base) + primitive.vertices.size() > pokemon_frame_vertices_.size()) {
            continue;
        }
        skinAttendPokemonPrimitiveWithPose(
            pokemon_model_,
            primitive,
            globals,
            skin_matrices,
            skinned_primitives_[primitive_index]);
        const AttendPokemonMaterial* material =
            primitive.material >= 0 && primitive.material < static_cast<int>(pokemon_model_.materials.size())
                ? &pokemon_model_.materials[static_cast<std::size_t>(primitive.material)]
                : nullptr;
        const std::vector<AttendPokemonVertex>& skinned = skinned_primitives_[primitive_index];
        for (std::size_t i = 0; i < skinned.size(); ++i) {
            pokemon_frame_vertices_[static_cast<std::size_t>(base) + i] = pokemonVertexForMaterial(
                skinned[i],
                material,
                current_eye_expression_frame_,
                current_mouth_expression_frame_);
        }
        const bgfx::Memory* primitive_mem = bgfx::copy(
            pokemon_frame_vertices_.data() + base,
            static_cast<std::uint32_t>(skinned.size() * sizeof(Vertex)));
        bgfx::update(pokemon_mesh_.dvbh, base, primitive_mem);
        updated_any = true;
    }
    if (!updated_any) return;
    updatePokemonBoundsFromVertices(pokemon_frame_vertices_);
}

const AttendPokemonAnimation* AttendBgfxRenderer::Impl::resolveSemanticAnimation(const std::string& semantic) const {
    const auto it = config_.interaction_adapter.semantic_animation_slots.find(semantic);
    if (it == config_.interaction_adapter.semantic_animation_slots.end()) return nullptr;
    for (const std::string& candidate : it->second) {
        for (const AttendPokemonAnimation& animation : pokemon_model_.animations) {
            if (animation.name == candidate) return &animation;
        }
    }
    for (const std::string& candidate : it->second) {
        for (const AttendPokemonAnimation& animation : pokemon_model_.animations) {
            if (!candidate.empty() && animation.name.find(candidate) != std::string::npos) return &animation;
        }
    }
    return nullptr;
}

float AttendBgfxRenderer::Impl::reactionWeight(double scene_time_seconds) const {
    if (!reaction_active_ || reaction_duration_seconds_ <= 0.0) return 0.0f;
    const double elapsed = std::clamp(scene_time_seconds - reaction_start_seconds_, 0.0, reaction_duration_seconds_);
    float in_weight = 1.0f;
    if (reaction_fade_in_seconds_ > 0.0) {
        in_weight = static_cast<float>(std::clamp(elapsed / reaction_fade_in_seconds_, 0.0, 1.0));
    }
    float out_weight = 1.0f;
    if (reaction_fade_out_seconds_ > 0.0) {
        const double remaining = reaction_duration_seconds_ - elapsed;
        out_weight = static_cast<float>(std::clamp(remaining / reaction_fade_out_seconds_, 0.0, 1.0));
    }
    const float weight = std::min(in_weight, out_weight);
    return weight * weight * (3.0f - 2.0f * weight);
}

const AttendInteractionAdapterConfig::ReactionCombo* AttendBgfxRenderer::Impl::reactionCombo(const std::string& id) const {
    const auto it = config_.interaction_adapter.reaction_combos.find(id);
    return it == config_.interaction_adapter.reaction_combos.end() ? nullptr : &it->second;
}

float AttendBgfxRenderer::Impl::petReadyCueWeight(
    double scene_time_seconds,
    const AttendInteractionAdapterConfig::ReactionCombo& combo) const {
    if (combo.ready_animation_semantic.empty() || combo.ready_weight <= 0.0f || pet_started_seconds_ < 0.0) return 0.0f;
    const double elapsed = scene_time_seconds - pet_started_seconds_;
    if (elapsed < static_cast<double>(combo.min_pet_seconds)) return 0.0f;
    const double cue_elapsed = elapsed - static_cast<double>(combo.min_pet_seconds);
    const float fade = static_cast<float>(std::clamp(cue_elapsed / static_cast<double>(std::max(0.01f, combo.ready_fade_seconds)), 0.0, 1.0));
    const float eased = fade * fade * (3.0f - 2.0f * fade);
    return std::clamp(combo.ready_weight * eased, 0.0f, 1.0f);
}

int AttendBgfxRenderer::Impl::eyeExpressionFrame(const std::string& semantic, int fallback) const {
    const auto it = config_.interaction_adapter.eye_expression_frames.find(semantic);
    return it == config_.interaction_adapter.eye_expression_frames.end() ? fallback : std::max(0, it->second);
}

int AttendBgfxRenderer::Impl::mouthExpressionFrame(const std::string& semantic, int fallback) const {
    const auto it = config_.interaction_adapter.mouth_expression_frames.find(semantic);
    return it == config_.interaction_adapter.mouth_expression_frames.end() ? fallback : std::max(0, it->second);
}

void AttendBgfxRenderer::Impl::startPendingReaction(double scene_time_seconds) {
    if (pending_reaction_id_.empty()) return;
    const std::string id = std::move(pending_reaction_id_);
    pending_reaction_id_.clear();
    const double interaction_seconds = pending_reaction_interaction_seconds_;
    pending_reaction_interaction_seconds_ = 0.0;
    const auto it = config_.interaction_adapter.reaction_combos.find(id);
    if (it == config_.interaction_adapter.reaction_combos.end()) return;

    const AttendInteractionAdapterConfig::ReactionCombo& combo = it->second;
    if (interaction_seconds < static_cast<double>(combo.min_pet_seconds)) return;
    reaction_animation_ = resolveSemanticAnimation(combo.animation_semantic);
    reaction_eye_expression_frame_ = eyeExpressionFrame(combo.eye_expression, normal_eye_expression_frame_);
    reaction_mouth_expression_frame_ = mouthExpressionFrame(combo.mouth_expression, normal_mouth_expression_frame_);
    reaction_duration_seconds_ = std::max(0.05, static_cast<double>(combo.duration_seconds));
    if (reaction_animation_ && reaction_animation_->duration_seconds > 0.0f) {
        reaction_duration_seconds_ = std::min(
            reaction_duration_seconds_,
            static_cast<double>(std::max(0.05f, reaction_animation_->duration_seconds)));
    }
    const double fade_scale = pokemon_world_height_ >= combo.large_pokemon_height
        ? static_cast<double>(combo.large_pokemon_fade_scale)
        : 1.0;
    reaction_fade_in_seconds_ = std::min(static_cast<double>(combo.fade_in_seconds) * fade_scale, reaction_duration_seconds_ * 0.5);
    reaction_fade_out_seconds_ = std::min(static_cast<double>(combo.fade_out_seconds) * fade_scale, reaction_duration_seconds_ * 0.5);
    reaction_eye_linger_until_seconds_ =
        scene_time_seconds + reaction_duration_seconds_ + static_cast<double>(combo.eye_linger_seconds);
    reaction_start_seconds_ = scene_time_seconds;
    reaction_active_ = true;
    random_blink_amount_ = 0.0f;
}

void AttendBgfxRenderer::Impl::updateFloorAnimation(double scene_time_seconds) {
    if (!floor_animated_ || !floor_mesh_.dynamic || !bgfx::isValid(floor_mesh_.dvbh) ||
        floor_model_.primitives.empty() || !floor_animation_) {
        return;
    }
    const std::vector<std::array<float, 16>> globals =
        buildAttendPokemonGlobals(floor_model_, floor_animation_, scene_time_seconds);
    const std::vector<std::vector<std::array<float, 16>>> skin_matrices =
        buildAttendPokemonSkinMatrices(floor_model_, globals);
    for (std::size_t primitive_index = 0; primitive_index < floor_model_.primitives.size(); ++primitive_index) {
        skinAttendPokemonPrimitiveWithPose(
            floor_model_,
            floor_model_.primitives[primitive_index],
            globals,
            skin_matrices,
            floor_skinned_primitives_[primitive_index]);
    }

    floor_frame_vertices_.clear();
    floor_frame_vertices_.reserve(floor_mesh_.vertex_count);
    for (std::size_t mat = 0; mat < floor_mesh_.materials.size(); ++mat) {
        for (std::size_t primitive_index = 0; primitive_index < floor_model_.primitives.size(); ++primitive_index) {
            const AttendPokemonPrimitive& primitive = floor_model_.primitives[primitive_index];
            if (primitive.material != static_cast<int>(mat)) continue;
            for (const AttendPokemonVertex& v : floor_skinned_primitives_[primitive_index]) {
                floor_frame_vertices_.push_back(transformAttendVertex(
                    v,
                    config_.floor.model_x,
                    config_.floor.model_y,
                    config_.floor.model_z,
                    config_.floor.model_yaw_degrees,
                    config_.floor.model_scale));
            }
        }
    }
    if (floor_frame_vertices_.size() != floor_mesh_.vertex_count) return;
    const bgfx::Memory* vb_mem = bgfx::copy(
        floor_frame_vertices_.data(),
        static_cast<std::uint32_t>(floor_frame_vertices_.size() * sizeof(Vertex)));
    bgfx::update(floor_mesh_.dvbh, 0, vb_mem);
}

void AttendBgfxRenderer::Impl::scheduleNextRandomBlink(double scene_time_seconds) {
    std::uniform_real_distribution<float> dist(
        config_.interaction_adapter.spontaneous_blink_min_seconds,
        config_.interaction_adapter.spontaneous_blink_max_seconds);
    next_random_blink_seconds_ = scene_time_seconds + static_cast<double>(dist(blink_rng_));
    random_blink_started_seconds_ = -1.0;
    random_blink_amount_ = 0.0f;
}

void AttendBgfxRenderer::Impl::updateRandomBlink(double scene_time_seconds) {
    if (!pokemon_eye_close_animation_ && !has_eye_expression_frames_) {
        random_blink_amount_ = 0.0f;
        return;
    }
    if (petting_) {
        random_blink_amount_ = 0.0f;
        if (next_random_blink_seconds_ < scene_time_seconds + 1.5) {
            next_random_blink_seconds_ = scene_time_seconds + 1.5;
        }
        return;
    }
    if (next_random_blink_seconds_ < 0.0) {
        scheduleNextRandomBlink(scene_time_seconds);
        return;
    }
    if (random_blink_started_seconds_ < 0.0 && scene_time_seconds >= next_random_blink_seconds_) {
        random_blink_started_seconds_ = scene_time_seconds;
    }
    if (random_blink_started_seconds_ < 0.0) {
        random_blink_amount_ = 0.0f;
        return;
    }

    const float duration = std::max(0.05f, config_.interaction_adapter.spontaneous_blink_duration_seconds);
    const float t = std::clamp(
        static_cast<float>((scene_time_seconds - random_blink_started_seconds_) / static_cast<double>(duration)),
        0.0f,
        1.0f);
    random_blink_amount_ = std::sin(t * kPi);
    if (t >= 1.0f) {
        scheduleNextRandomBlink(scene_time_seconds);
    }
}

void AttendBgfxRenderer::Impl::updatePetControls(double scene_time_seconds) {
    if (last_pet_update_seconds_ < 0.0) {
        last_pet_update_seconds_ = scene_time_seconds;
        pet_started_seconds_ = petting_ ? scene_time_seconds : -1.0;
        pet_eye_close_amount_ = 0.0f;
        was_petting_ = petting_;
        return;
    }
    const float dt = std::clamp(static_cast<float>(scene_time_seconds - last_pet_update_seconds_), 0.0f, 0.1f);
    last_pet_update_seconds_ = scene_time_seconds;

    if (petting_ && !was_petting_) {
        pet_started_seconds_ = scene_time_seconds;
    } else if (!petting_ && was_petting_) {
        pet_started_seconds_ = -1.0;
        pet_eye_close_cooldown_until_seconds_ =
            scene_time_seconds + static_cast<double>(config_.interaction_adapter.pet_eye_close_cooldown_seconds);
    }
    was_petting_ = petting_;

    const bool delay_elapsed = petting_ && pet_started_seconds_ >= 0.0 &&
        scene_time_seconds >= pet_started_seconds_ +
            static_cast<double>(config_.interaction_adapter.pet_eye_close_delay_seconds);
    const bool cooldown_elapsed = scene_time_seconds >= pet_eye_close_cooldown_until_seconds_;
    const float target = (delay_elapsed && cooldown_elapsed) ? 1.0f : 0.0f;
    const float speed = target > pet_eye_close_amount_ ? 4.6f : 6.5f;
    if (pet_eye_close_amount_ < target) {
        pet_eye_close_amount_ = std::min(target, pet_eye_close_amount_ + dt * speed);
    } else {
        pet_eye_close_amount_ = std::max(target, pet_eye_close_amount_ - dt * speed);
    }
    const float contact_target = petting_ ? pet_contact_target_ : 0.0f;
    const float contact_follow = std::clamp(dt * 7.0f, 0.0f, 1.0f);
    pet_contact_bias_ += (contact_target - pet_contact_bias_) * contact_follow;
}

void AttendBgfxRenderer::Impl::updateFaceControls(double scene_time_seconds) {
    if (last_face_update_seconds_ < 0.0) {
        last_face_update_seconds_ = scene_time_seconds;
        face_look_x_ = face_target_x_;
        face_look_y_ = face_target_y_;
        return;
    }
    const float dt = std::clamp(static_cast<float>(scene_time_seconds - last_face_update_seconds_), 0.0f, 0.1f);
    last_face_update_seconds_ = scene_time_seconds;
    const float follow = std::clamp(dt * 5.0f, 0.0f, 1.0f);
    face_look_x_ += (face_target_x_ - face_look_x_) * follow;
    face_look_y_ += (face_target_y_ - face_look_y_) * follow;
}

void AttendBgfxRenderer::Impl::updateViewportLook(double scene_time_seconds) {
    if (last_viewport_look_update_seconds_ < 0.0) {
        last_viewport_look_update_seconds_ = scene_time_seconds;
        viewport_look_x_ = viewport_look_target_x_;
        viewport_look_y_ = viewport_look_target_y_;
        return;
    }
    const float dt = std::clamp(static_cast<float>(scene_time_seconds - last_viewport_look_update_seconds_), 0.0f, 0.1f);
    last_viewport_look_update_seconds_ = scene_time_seconds;
    const float seconds = std::max(0.01f, config_.viewport_look.smooth_seconds);
    const float follow = 1.0f - std::exp(-dt / seconds);
    viewport_look_x_ += (viewport_look_target_x_ - viewport_look_x_) * follow;
    viewport_look_y_ += (viewport_look_target_y_ - viewport_look_y_) * follow;
}

void AttendBgfxRenderer::Impl::updatePokemonBoundsFromVertices(const std::vector<Vertex>& vertices) {
    if (vertices.empty()) {
        pokemon_bounds_valid_ = false;
        pokemon_world_height_ = 0.0f;
        return;
    }
    bool any = false;
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        if (i < pokemon_frame_vertex_primitives_.size() &&
            !pokemonPrimitiveVisibleForHit(pokemon_frame_vertex_primitives_[i])) {
            continue;
        }
        const Vertex& vertex = vertices[i];
        if (!any) {
            pokemon_min_x_ = pokemon_max_x_ = vertex.x;
            pokemon_min_y_ = pokemon_max_y_ = vertex.y;
            pokemon_min_z_ = pokemon_max_z_ = vertex.z;
            any = true;
            continue;
        }
        pokemon_min_x_ = std::min(pokemon_min_x_, vertex.x);
        pokemon_min_y_ = std::min(pokemon_min_y_, vertex.y);
        pokemon_min_z_ = std::min(pokemon_min_z_, vertex.z);
        pokemon_max_x_ = std::max(pokemon_max_x_, vertex.x);
        pokemon_max_y_ = std::max(pokemon_max_y_, vertex.y);
        pokemon_max_z_ = std::max(pokemon_max_z_, vertex.z);
    }
    if (!any) {
        pokemon_bounds_valid_ = false;
        pokemon_world_height_ = 0.0f;
        return;
    }
    pokemon_bounds_valid_ = true;
    pokemon_world_height_ = std::max(0.0f, (pokemon_max_y_ - pokemon_min_y_) * config_.pokemon.scale);
}

void AttendBgfxRenderer::Impl::cameraForFrame(float look_x, float look_y, bx::Vec3& eye, bx::Vec3& at) const {
    float target_x = config_.camera.target_x;
    float target_y = config_.camera.target_height;
    float target_z = config_.camera.target_z;
    float distance = config_.camera.distance;
    float height = config_.camera.height;

    if (config_.camera.auto_focus && pokemon_bounds_valid_) {
        const float scaled_min_y = config_.pokemon.y + pokemon_min_y_ * config_.pokemon.scale;
        const float scaled_max_y = config_.pokemon.y + pokemon_max_y_ * config_.pokemon.scale;
        const float model_height = std::max(0.2f, scaled_max_y - scaled_min_y);
        const float model_width = std::max(0.2f, (pokemon_max_x_ - pokemon_min_x_) * config_.pokemon.scale);
        const float model_depth = std::max(0.2f, (pokemon_max_z_ - pokemon_min_z_) * config_.pokemon.scale);
        const float fov_y = std::max(1.0f, config_.camera.fov_y_degrees) * (kPi / 180.0f);
        const float aspect = 16.0f / 9.0f;
        const float fov_x = 2.0f * std::atan(std::tan(fov_y * 0.5f) * aspect);
        const float desired_ratio = std::clamp(
            face_view_ ? config_.camera.face_screen_height_ratio : config_.camera.screen_height_ratio,
            0.15f,
            0.98f);
        const float distance_scale = face_view_ ? config_.camera.face_distance_scale : config_.camera.distance_scale;
        const float target_y_ratio = face_view_ ? config_.camera.face_target_y_ratio : config_.camera.target_y_ratio;
        const float height_offset = face_view_ ? config_.camera.face_height_offset : config_.camera.height_offset;
        const float height_offset_limit = model_height * (face_view_ ? 0.08f : 0.52f);
        const float desired_distance =
            (model_height / (2.0f * std::tan(fov_y * 0.5f) * desired_ratio)) *
            distance_scale;
        const float width_distance =
            (model_width / (2.0f * std::tan(fov_x * 0.5f) * std::max(0.35f, desired_ratio * 1.12f))) *
            distance_scale;
        const float depth_padding = model_depth *
            (face_view_ ? config_.camera.face_depth_padding_scale : config_.camera.depth_padding_scale);
        target_x = config_.pokemon.x;
        target_y = scaled_min_y + model_height * target_y_ratio;
        target_z = config_.pokemon.z;
        distance = std::clamp(
            std::max(desired_distance, width_distance) + depth_padding,
            config_.camera.min_distance,
            config_.camera.max_distance);
        height = target_y + std::min(height_offset, height_offset_limit);
    }

    eye = bx::Vec3{
        target_x,
        height,
        target_z + distance};
    at = bx::Vec3{
        target_x + look_x,
        target_y + look_y,
        target_z};
}

void AttendBgfxRenderer::Impl::updatePokemonPointerRect(
    const float* pokemon_matrix,
    const float* view,
    const float* proj,
    int width,
    int height) {
    last_pokemon_pointer_rect_ = SDL_Rect{0, 0, 0, 0};
    if (pokemon_frame_vertices_.empty() || width <= 0 || height <= 0) return;

    const auto transform = [](const float* m, float x, float y, float z, float& ox, float& oy, float& oz, float& ow) {
        ox = x * m[0] + y * m[4] + z * m[8] + m[12];
        oy = x * m[1] + y * m[5] + z * m[9] + m[13];
        oz = x * m[2] + y * m[6] + z * m[10] + m[14];
        ow = x * m[3] + y * m[7] + z * m[11] + m[15];
    };

    float min_x = 1.0f;
    float min_y = 1.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;
    bool any = false;
    for (std::size_t i = 0; i < pokemon_frame_vertices_.size(); ++i) {
        if (i < pokemon_frame_vertex_primitives_.size() &&
            !pokemonPrimitiveVisibleForHit(pokemon_frame_vertex_primitives_[i])) {
            continue;
        }
        const Vertex& vertex = pokemon_frame_vertices_[i];
        float wx = 0.0f;
        float wy = 0.0f;
        float wz = 0.0f;
        float ww = 1.0f;
        transform(pokemon_matrix, vertex.x, vertex.y, vertex.z, wx, wy, wz, ww);
        float vx = 0.0f;
        float vy = 0.0f;
        float vz = 0.0f;
        float vw = 1.0f;
        transform(view, wx, wy, wz, vx, vy, vz, vw);
        float cx = 0.0f;
        float cy = 0.0f;
        float cz = 0.0f;
        float cw = 1.0f;
        transform(proj, vx, vy, vz, cx, cy, cz, cw);
        if (std::abs(cw) < 0.0001f) continue;
        const float ndc_x = cx / cw;
        const float ndc_y = cy / cw;
        if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y)) continue;
        const float sx = ndc_x * 0.5f + 0.5f;
        const float sy = 0.5f - ndc_y * 0.5f;
        min_x = std::min(min_x, sx);
        min_y = std::min(min_y, sy);
        max_x = std::max(max_x, sx);
        max_y = std::max(max_y, sy);
        any = true;
    }
    if (!any) return;
    min_x = std::clamp(min_x, 0.0f, 1.0f);
    max_x = std::clamp(max_x, 0.0f, 1.0f);
    min_y = std::clamp(min_y, 0.0f, 1.0f);
    max_y = std::clamp(max_y, 0.0f, 1.0f);
    if (max_x <= min_x || max_y <= min_y) return;

    const float sx = static_cast<float>(std::max(1, overlay_logical_w_)) / static_cast<float>(std::max(1, width));
    const float sy = static_cast<float>(std::max(1, overlay_logical_h_)) / static_cast<float>(std::max(1, height));
    const int x = static_cast<int>(std::floor(min_x * static_cast<float>(width) * sx));
    const int y = static_cast<int>(std::floor(min_y * static_cast<float>(height) * sy));
    const int w = static_cast<int>(std::ceil((max_x - min_x) * static_cast<float>(width) * sx));
    const int h = static_cast<int>(std::ceil((max_y - min_y) * static_cast<float>(height) * sy));
    const int pad_x = std::clamp(w / 24, 4, 14);
    const int pad_y = std::clamp(h / 24, 4, 14);
    last_pokemon_pointer_rect_ = SDL_Rect{
        std::max(0, x - pad_x),
        std::max(0, y - pad_y),
        std::min(std::max(1, overlay_logical_w_) - std::max(0, x - pad_x), w + pad_x * 2),
        std::min(std::max(1, overlay_logical_h_) - std::max(0, y - pad_y), h + pad_y * 2)};
}

bool AttendBgfxRenderer::Impl::floorMaterialVisible(const MaterialResource* material) const {
    if (!material || !material->visible) return material == nullptr || (material && material->visible);
    if (config_.floor.weather_material_substrings.empty() ||
        !containsAnySubstring(material->name, config_.floor.weather_material_substrings)) {
        return true;
    }
    if (floor_weather_index_ < 0 ||
        floor_weather_index_ >= static_cast<int>(config_.floor.weather_modes.size())) {
        return false;
    }
    const WeatherModeConfig& mode = config_.floor.weather_modes[static_cast<std::size_t>(floor_weather_index_)];
    return containsAnySubstring(material->name, mode.visible_material_substrings);
}

bool AttendBgfxRenderer::Impl::pokemonPrimitiveVisibleForHit(std::size_t primitive_index) const {
    if (primitive_index >= pokemon_model_.primitives.size()) return false;
    const AttendPokemonPrimitive& primitive = pokemon_model_.primitives[primitive_index];
    if (!primitive.visible_for_forms.empty()) {
        const std::string form_id =
            form_variant_index_ >= 0 && form_variant_index_ < static_cast<int>(pokemon_model_.form_variants.size())
                ? pokemon_model_.form_variants[static_cast<std::size_t>(form_variant_index_)].id
                : std::string{};
        if (form_id.empty() || !stringListContains(primitive.visible_for_forms, form_id)) {
            return false;
        }
    }
    if (primitive.material >= 0 && primitive.material < static_cast<int>(pokemon_mesh_.materials.size())) {
        const MaterialResource& material = pokemon_mesh_.materials[static_cast<std::size_t>(primitive.material)];
        if (!material.visible) return false;
        if (material.separate_eye_iris && current_eye_expression_frame_ == closed_eye_expression_frame_) {
            return false;
        }
    }
    return true;
}

void AttendBgfxRenderer::Impl::submitMesh(
    const MeshResource& mesh,
    const float* matrix,
    bool force_blend,
    bool backdrop,
    bool floor) {
    if (!mesh.valid()) return;
    const bool focus_backdrop = backdrop || floor;
    float adjust[4] = {
        config_.lighting.brightness * (focus_backdrop ? config_.lighting.backdrop_brightness : config_.lighting.pokemon_brightness),
        focus_backdrop ? config_.lighting.backdrop_saturation : 1.0f,
        focus_backdrop ? config_.lighting.backdrop_contrast : 1.0f,
        focus_backdrop && config_.depth_of_field.enabled ? config_.depth_of_field.focus_depth : 0.0f};
    float lx = config_.lighting.light_direction[0];
    float ly = config_.lighting.light_direction[1];
    float lz = config_.lighting.light_direction[2];
    normalize3(lx, ly, lz);
    const float light_dir[4] = {lx, ly, lz, 0.0f};
    const float light_params[4] = {
        focus_backdrop ? 1.0f : config_.lighting.ambient,
        focus_backdrop ? 0.0f : config_.lighting.directional,
        focus_backdrop ? 0.0f : config_.lighting.form_shadow,
        0.0f};
    for (const MeshResource::Range& range : mesh.ranges) {
        if (!backdrop && !floor && !range.visible_for_forms.empty()) {
            const std::string form_id =
                form_variant_index_ >= 0 && form_variant_index_ < static_cast<int>(pokemon_model_.form_variants.size())
                    ? pokemon_model_.form_variants[static_cast<std::size_t>(form_variant_index_)].id
                    : std::string{};
            if (form_id.empty() || !stringListContains(range.visible_for_forms, form_id)) {
                continue;
            }
        }
        const MaterialResource* material = nullptr;
        if (range.material >= 0 && range.material < static_cast<int>(mesh.materials.size())) {
            material = &mesh.materials[static_cast<std::size_t>(range.material)];
        }
        if (!backdrop && !floor && material && !material->form_variant_materials.empty() &&
            form_variant_index_ >= 0 &&
            form_variant_index_ < static_cast<int>(pokemon_model_.form_variants.size())) {
            const std::string& form_id = pokemon_model_.form_variants[static_cast<std::size_t>(form_variant_index_)].id;
            for (const auto& [candidate_id, candidate_material] : material->form_variant_materials) {
                if (candidate_id != form_id) continue;
                if (candidate_material >= 0 && candidate_material < static_cast<int>(mesh.materials.size())) {
                    material = &mesh.materials[static_cast<std::size_t>(candidate_material)];
                }
                break;
            }
        }
        if (!backdrop && !floor && material &&
            texture_variant_index_ >= 0 &&
            texture_variant_index_ < static_cast<int>(material->texture_variant_materials.size())) {
            const int variant_material = material->texture_variant_materials[static_cast<std::size_t>(texture_variant_index_)];
            if (variant_material >= 0 && variant_material < static_cast<int>(mesh.materials.size())) {
                material = &mesh.materials[static_cast<std::size_t>(variant_material)];
            }
        }
        if (floor ? !floorMaterialVisible(material) : (material && !material->visible)) continue;
        if (material && material->separate_eye_iris && current_eye_expression_frame_ == closed_eye_expression_frame_) {
            continue;
        }
        const TextureResource& texture = (material && material->texture.valid()) ? material->texture : white_texture_;
        const TextureResource& eye_mask = (material && material->eye_mask_texture.valid()) ? material->eye_mask_texture : white_texture_;
        const float blur_radius = (focus_backdrop && config_.depth_of_field.enabled && texture.valid())
            ? std::clamp(config_.depth_of_field.strength, 0.0f, 1.0f) * std::max(0.0f, config_.depth_of_field.max_radius)
            : 0.0f;
        const float texture_blur[4] = {
            texture.width > 0 ? blur_radius / static_cast<float>(texture.width) : 0.0f,
            texture.height > 0 ? blur_radius / static_cast<float>(texture.height) : 0.0f,
            config_.depth_of_field.enabled ? config_.depth_of_field.falloff : 0.0f,
            config_.pokemon.z};
        float tint[4] = {
            config_.lighting.tint.r,
            config_.lighting.tint.g,
            config_.lighting.tint.b,
            0.0f};
        if (material) {
            tint[0] *= material->base_color[0];
            tint[1] *= material->base_color[1];
            tint[2] *= material->base_color[2];
            tint[3] = material->mask_cutout ? material->alpha_cutoff : 0.0f;
        }
        bgfx::setTransform(matrix);
        if (bgfx::isValid(mesh.dvbh)) {
            bgfx::setVertexBuffer(0, mesh.dvbh);
        } else {
            bgfx::setVertexBuffer(0, mesh.vbh);
        }
        bgfx::setIndexBuffer(mesh.ibh, range.start, range.count);
        bgfx::setTexture(0, tex_uniform_, texture.handle, material ? material->sampler_flags : samplerFlags());
        if (material && material->pokemon_eye && bgfx::isValid(eye_mask_uniform_)) {
            bgfx::setTexture(1, eye_mask_uniform_, eye_mask.handle, samplerFlags());
        }
        bgfx::setUniform(tint_cutoff_uniform_, tint);
        bgfx::setUniform(color_adjust_uniform_, adjust);
        bgfx::setUniform(texture_blur_uniform_, texture_blur);
        bgfx::setUniform(light_dir_uniform_, light_dir);
        bgfx::setUniform(light_params_uniform_, light_params);
        const bool blended = force_blend || (material && material->blend);
        if (material && material->separate_eye_iris && !backdrop && !floor) {
            bgfx::setStencil(
                BGFX_STENCIL_TEST_EQUAL |
                BGFX_STENCIL_FUNC_REF(1) |
                BGFX_STENCIL_FUNC_RMASK(0xff) |
                BGFX_STENCIL_OP_FAIL_S_KEEP |
                BGFX_STENCIL_OP_FAIL_Z_KEEP |
                BGFX_STENCIL_OP_PASS_Z_KEEP);
        }
        if (material && material->separate_eye_iris && !backdrop && !floor) {
            bgfx::setState(overlayState());
        } else {
            bgfx::setState(backdrop ? backdropState(blended) : (blended ? blendState() : opaqueState()));
        }
        bgfx::submit(0, (material && material->pokemon_eye && bgfx::isValid(eye_program_)) ? eye_program_ : program_);
        if (material && material->eye_sclera_mask && !backdrop && !floor &&
            bgfx::isValid(eye_sclera_mask_program_)) {
            bgfx::setTransform(matrix);
            if (bgfx::isValid(mesh.dvbh)) {
                bgfx::setVertexBuffer(0, mesh.dvbh);
            } else {
                bgfx::setVertexBuffer(0, mesh.vbh);
            }
            bgfx::setIndexBuffer(mesh.ibh, range.start, range.count);
            bgfx::setTexture(0, tex_uniform_, texture.handle, material->sampler_flags);
            bgfx::setUniform(tint_cutoff_uniform_, tint);
            bgfx::setUniform(color_adjust_uniform_, adjust);
            bgfx::setUniform(texture_blur_uniform_, texture_blur);
            bgfx::setUniform(light_dir_uniform_, light_dir);
            bgfx::setUniform(light_params_uniform_, light_params);
            bgfx::setStencil(
                BGFX_STENCIL_TEST_ALWAYS |
                BGFX_STENCIL_FUNC_REF(1) |
                BGFX_STENCIL_FUNC_RMASK(0xff) |
                BGFX_STENCIL_OP_FAIL_S_KEEP |
                BGFX_STENCIL_OP_FAIL_Z_KEEP |
                BGFX_STENCIL_OP_PASS_Z_REPLACE);
            bgfx::setState(eyeScleraStencilState());
            bgfx::submit(0, eye_sclera_mask_program_);
        }
    }
}

void AttendBgfxRenderer::Impl::submitPokemonShadow() {
    if (!config_.shadow.enabled || config_.shadow.strength <= 0.0f || !white_texture_.valid() || !bgfx::isValid(program_)) return;
    constexpr int kSegments = 40;
    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;
    if (!bgfx::allocTransientBuffers(&tvb, layout_, kSegments + 1, &tib, kSegments * 3)) return;

    const float y = (config_.floor.enabled ? config_.floor.y : config_.pokemon.y) + config_.shadow.y_offset;
    float center_x = config_.pokemon.x;
    float center_z = config_.pokemon.z;
    float radius_x = config_.shadow.radius_x;
    float radius_z = config_.shadow.radius_z;
    if (pokemon_bounds_valid_) {
        const float width_x = std::max(0.0f, (pokemon_max_x_ - pokemon_min_x_) * config_.pokemon.scale);
        const float depth_z = std::max(0.0f, (pokemon_max_z_ - pokemon_min_z_) * config_.pokemon.scale);
        const float footprint = std::max(width_x, depth_z);
        center_x += (pokemon_min_x_ + pokemon_max_x_) * 0.5f * config_.pokemon.scale;
        center_z += (pokemon_min_z_ + pokemon_max_z_) * 0.5f * config_.pokemon.scale;
        radius_x = std::max(radius_x, std::max(width_x * 0.50f, footprint * 0.26f));
        radius_z = std::max(radius_z, std::max(depth_z * 0.46f, footprint * 0.22f));
    }
    const std::uint32_t center_color = packAbgr(0.0f, 0.0f, 0.0f, config_.shadow.strength);
    const std::uint32_t edge_color = packAbgr(0.0f, 0.0f, 0.0f, 0.0f);

    auto* verts = reinterpret_cast<Vertex*>(tvb.data);
    verts[0] = Vertex{center_x, y, center_z, 0.0f, 1.0f, 0.0f, center_color, 0.5f, 0.5f};
    for (int i = 0; i < kSegments; ++i) {
        const float t = (static_cast<float>(i) / static_cast<float>(kSegments)) * kPi * 2.0f;
        verts[i + 1] = Vertex{
            center_x + std::cos(t) * radius_x,
            y,
            center_z + std::sin(t) * radius_z,
            0.0f,
            1.0f,
            0.0f,
            edge_color,
            0.5f + std::cos(t) * 0.5f,
            0.5f + std::sin(t) * 0.5f};
    }

    auto* idx = reinterpret_cast<std::uint16_t*>(tib.data);
    for (int i = 0; i < kSegments; ++i) {
        idx[i * 3 + 0] = 0;
        idx[i * 3 + 1] = static_cast<std::uint16_t>(i + 1);
        idx[i * 3 + 2] = static_cast<std::uint16_t>((i + 1) % kSegments + 1);
    }

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
    bgfx::setTexture(0, tex_uniform_, white_texture_.handle);
    bgfx::setUniform(tint_cutoff_uniform_, tint);
    bgfx::setUniform(color_adjust_uniform_, adjust);
    bgfx::setUniform(texture_blur_uniform_, texture_blur);
    bgfx::setUniform(light_dir_uniform_, light_dir);
    bgfx::setUniform(light_params_uniform_, light_params);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_BLEND_ALPHA);
    bgfx::submit(0, program_);
}

bool AttendBgfxRenderer::Impl::ensureOverlayButtonTexture(std::size_t index) {
    if (index >= overlay_buttons_.size()) return false;
    if (index >= overlay_button_textures_.size()) overlay_button_textures_.resize(index + 1);
    const AttendBgfxOverlayButton& button = overlay_buttons_[index];
    if (button.w <= 0 || button.h <= 0) return false;
    const AttendOverlayButtonConfig& style = button.style;
    OverlayButtonTexture& resource = overlay_button_textures_[index];
    const int w = std::max(1, button.w);
    const int h = std::max(1, button.h);
    const std::string key = overlayStyleKey(style, button.label, w, h);
    if (resource.texture.valid() && resource.key == key) {
        return true;
    }

    resource.destroy();
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w * h * 4), 0);
    const int radius = std::clamp(style.corner_radius, 0, std::min(w, h) / 2);
    const int stroke_width = std::clamp(style.stroke_width, 0, std::min(w, h) / 2);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!insideRoundedRect(x, y, w, h, radius)) continue;
            const bool inner = stroke_width <= 0 ||
                insideRoundedRect(
                    x - stroke_width,
                    y - stroke_width,
                    w - stroke_width * 2,
                    h - stroke_width * 2,
                    std::max(0, radius - stroke_width));
            setRgba(pixels, w, x, y, inner ? style.fill : style.stroke);
        }
    }

    if (!button.label.empty()) {
        if (!TTF_WasInit() && TTF_Init() != 0) {
            std::cerr << "[AttendBgfxRenderer] TTF_Init failed for overlay text: " << TTF_GetError() << '\n';
        } else {
            const fs::path font_path = fs::path(project_root_) / "assets" / "fonts" / "Arial.ttf";
            TTF_Font* font = TTF_OpenFont(font_path.string().c_str(), std::max(8, style.font_size));
            if (!font) {
                std::cerr << "[AttendBgfxRenderer] Could not open overlay font: " << font_path << " | " << TTF_GetError() << '\n';
            } else {
                const SDL_Color text_color{
                    byteChannel(style.text.r),
                    byteChannel(style.text.g),
                    byteChannel(style.text.b),
                    byteChannel(style.text.a)};
                SDL_Surface* surface = TTF_RenderUTF8_Blended(font, button.label.c_str(), text_color);
                if (surface) {
                    SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
                    SDL_FreeSurface(surface);
                    if (converted) {
                        const int text_x = std::clamp(style.padding_x, 0, std::max(0, w - converted->w));
                        const int text_y = std::max(0, (h - converted->h) / 2);
                        const auto* src_pixels = static_cast<const std::uint32_t*>(converted->pixels);
                        const int pitch_pixels = converted->pitch / 4;
                        for (int ty = 0; ty < converted->h && text_y + ty < h; ++ty) {
                            for (int tx = 0; tx < converted->w && text_x + tx < w; ++tx) {
                                Uint8 r = 0, g = 0, b = 0, a = 0;
                                SDL_GetRGBA(src_pixels[ty * pitch_pixels + tx], converted->format, &r, &g, &b, &a);
                                if (a == 0) continue;
                                const int dst_x = text_x + tx;
                                const int dst_y = text_y + ty;
                                const std::size_t offset = static_cast<std::size_t>((dst_y * w + dst_x) * 4);
                                const float src_a = static_cast<float>(a) / 255.0f;
                                const float dst_a = static_cast<float>(pixels[offset + 3]) / 255.0f;
                                const float out_a = src_a + dst_a * (1.0f - src_a);
                                if (out_a <= 0.0f) continue;
                                pixels[offset + 0] = static_cast<std::uint8_t>(
                                    (static_cast<float>(r) * src_a + static_cast<float>(pixels[offset + 0]) * dst_a * (1.0f - src_a)) / out_a + 0.5f);
                                pixels[offset + 1] = static_cast<std::uint8_t>(
                                    (static_cast<float>(g) * src_a + static_cast<float>(pixels[offset + 1]) * dst_a * (1.0f - src_a)) / out_a + 0.5f);
                                pixels[offset + 2] = static_cast<std::uint8_t>(
                                    (static_cast<float>(b) * src_a + static_cast<float>(pixels[offset + 2]) * dst_a * (1.0f - src_a)) / out_a + 0.5f);
                                pixels[offset + 3] = static_cast<std::uint8_t>(out_a * 255.0f + 0.5f);
                            }
                        }
                        SDL_FreeSurface(converted);
                    }
                }
                TTF_CloseFont(font);
            }
        }
    }

    const bgfx::Memory* mem = bgfx::copy(pixels.data(), static_cast<std::uint32_t>(pixels.size()));
    resource.texture.handle = bgfx::createTexture2D(
        static_cast<std::uint16_t>(w),
        static_cast<std::uint16_t>(h),
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        0,
        mem);
    resource.texture.width = w;
    resource.texture.height = h;
    resource.key = key;
    return resource.texture.valid();
}

void AttendBgfxRenderer::Impl::submitPixelSceneToBackbuffer(
    int framebuffer_w,
    int framebuffer_h,
    int source_w,
    int source_h) {
    if (!pixel_scene_target_.valid() || !bgfx::isValid(program_)) return;
    const bgfx::TextureHandle texture = bgfx::getTexture(pixel_scene_target_.frame_buffer, 0);
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
    bgfx::setViewTransform(1, view, proj);
    bgfx::setViewRect(
        1,
        0,
        0,
        static_cast<std::uint16_t>(framebuffer_w),
        static_cast<std::uint16_t>(framebuffer_h));
    bgfx::setViewFrameBuffer(1, BGFX_INVALID_HANDLE);
    bgfx::setViewClear(1, BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
    bgfx::setViewMode(1, bgfx::ViewMode::Sequential);

    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;
    if (!bgfx::allocTransientBuffers(&tvb, layout_, 4, &tib, 6)) return;

    const float x0 = static_cast<float>(dest_x);
    const float y0 = static_cast<float>(dest_y);
    const float x1 = static_cast<float>(dest_x + dest_w);
    const float y1 = static_cast<float>(dest_y + dest_h);
    const float v_top = backend_.originBottomLeft() ? 1.0f : 0.0f;
    const float v_bottom = backend_.originBottomLeft() ? 0.0f : 1.0f;
    auto* verts = reinterpret_cast<Vertex*>(tvb.data);
    verts[0] = Vertex{x0, y0, 0.0f, 0.0f, 1.0f, 0.0f, 0xffffffffu, 0.0f, v_top};
    verts[1] = Vertex{x1, y0, 0.0f, 0.0f, 1.0f, 0.0f, 0xffffffffu, 1.0f, v_top};
    verts[2] = Vertex{x1, y1, 0.0f, 0.0f, 1.0f, 0.0f, 0xffffffffu, 1.0f, v_bottom};
    verts[3] = Vertex{x0, y1, 0.0f, 0.0f, 1.0f, 0.0f, 0xffffffffu, 0.0f, v_bottom};
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
    bgfx::setUniform(light_dir_uniform_, light_dir);
    bgfx::setUniform(light_params_uniform_, light_params);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(1, program_);
}

void AttendBgfxRenderer::Impl::submitOverlayButtons(int framebuffer_w, int framebuffer_h) {
    if (overlay_buttons_.empty() || !bgfx::isValid(program_)) return;
    framebuffer_w = std::max(1, framebuffer_w);
    framebuffer_h = std::max(1, framebuffer_h);
    const float sx = static_cast<float>(framebuffer_w) / static_cast<float>(std::max(1, overlay_logical_w_));
    const float sy = static_cast<float>(framebuffer_h) / static_cast<float>(std::max(1, overlay_logical_h_));

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
    bgfx::setViewTransform(2, view, proj);
    bgfx::setViewRect(
        2,
        0,
        0,
        static_cast<std::uint16_t>(framebuffer_w),
        static_cast<std::uint16_t>(framebuffer_h));
    bgfx::setViewFrameBuffer(2, BGFX_INVALID_HANDLE);
    bgfx::setViewMode(2, bgfx::ViewMode::Sequential);

    for (std::size_t index = 0; index < overlay_buttons_.size(); ++index) {
        if (!ensureOverlayButtonTexture(index)) continue;
        const AttendBgfxOverlayButton& button = overlay_buttons_[index];
        const OverlayButtonTexture& resource = overlay_button_textures_[index];
        if (!resource.texture.valid()) continue;
        const float x0 = static_cast<float>(button.x) * sx;
        const float y0 = static_cast<float>(button.y) * sy;
        const float x1 = static_cast<float>(button.x + button.w) * sx;
        const float y1 = static_cast<float>(button.y + button.h) * sy;

        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;
        if (!bgfx::allocTransientBuffers(&tvb, layout_, 4, &tib, 6)) return;

        const float v_top = backend_.originBottomLeft() ? 1.0f : 0.0f;
        const float v_bottom = backend_.originBottomLeft() ? 0.0f : 1.0f;
        auto* verts = reinterpret_cast<Vertex*>(tvb.data);
        verts[0] = Vertex{x0, y0, 0.0f, 0.0f, 1.0f, 0.0f, 0xffffffffu, 0.0f, v_top};
        verts[1] = Vertex{x1, y0, 0.0f, 0.0f, 1.0f, 0.0f, 0xffffffffu, 1.0f, v_top};
        verts[2] = Vertex{x1, y1, 0.0f, 0.0f, 1.0f, 0.0f, 0xffffffffu, 1.0f, v_bottom};
        verts[3] = Vertex{x0, y1, 0.0f, 0.0f, 1.0f, 0.0f, 0xffffffffu, 0.0f, v_bottom};
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
        bgfx::setTexture(0, tex_uniform_, resource.texture.handle);
        bgfx::setUniform(tint_cutoff_uniform_, tint);
        bgfx::setUniform(color_adjust_uniform_, adjust);
        bgfx::setUniform(texture_blur_uniform_, texture_blur);
        bgfx::setUniform(light_dir_uniform_, light_dir);
        bgfx::setUniform(light_params_uniform_, light_params);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
        bgfx::submit(2, program_);
    }
}

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
    const float look_x = config_.viewport_look.enabled ? viewport_look_x_ * config_.viewport_look.max_x : 0.0f;
    const float look_y = config_.viewport_look.enabled ? viewport_look_y_ * config_.viewport_look.max_y : 0.0f;
    bx::Vec3 eye{0.0f, 0.0f, 0.0f};
    bx::Vec3 at{0.0f, 0.0f, 0.0f};
    cameraForFrame(look_x, look_y, eye, at);
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
    if (pixel_scene_enabled) {
        submitPixelSceneToBackbuffer(width, height, render_w, render_h);
    }
    submitOverlayButtons(width, height);

    backend_.endFrame();
}

AttendBgfxRenderer::AttendBgfxRenderer(std::string project_root, AttendSceneConfig config)
    : impl_(std::make_unique<Impl>(std::move(project_root), std::move(config))) {}

AttendBgfxRenderer::~AttendBgfxRenderer() = default;

bool AttendBgfxRenderer::initialize(
    SDL_Window* window,
    int width,
    int height,
    const std::string& bgfx_preference,
    void* sdl_metal_view) {
    return impl_->initialize(window, width, height, bgfx_preference, sdl_metal_view);
}

void AttendBgfxRenderer::shutdown() {
    impl_->shutdown();
}

bool AttendBgfxRenderer::valid() const {
    return impl_->valid();
}

std::string AttendBgfxRenderer::lastError() const {
    return impl_->lastError();
}

void AttendBgfxRenderer::setPetting(bool petting) {
    impl_->setPetting(petting);
}

void AttendBgfxRenderer::setPetContact(float vertical_bias) {
    impl_->setPetContact(vertical_bias);
}

void AttendBgfxRenderer::setFaceLook(float x, float y) {
    impl_->setFaceLook(x, y);
}

void AttendBgfxRenderer::setViewportLook(float x, float y) {
    impl_->setViewportLook(x, y);
}

void AttendBgfxRenderer::setWeatherMode(int index) {
    impl_->setWeatherMode(index);
}

void AttendBgfxRenderer::setTextureVariant(int index) {
    impl_->setTextureVariant(index);
}

void AttendBgfxRenderer::setFormVariant(int index) {
    impl_->setFormVariant(index);
}

void AttendBgfxRenderer::triggerReaction(const std::string& reaction_id, double interaction_seconds) {
    impl_->triggerReaction(reaction_id, interaction_seconds);
}

void AttendBgfxRenderer::setOverlayButtons(
    std::vector<AttendBgfxOverlayButton> buttons,
    int logical_w,
    int logical_h) {
    impl_->setOverlayButtons(std::move(buttons), logical_w, logical_h);
}

void AttendBgfxRenderer::setFaceView(bool face_view) {
    impl_->setFaceView(face_view);
}

bool AttendBgfxRenderer::faceViewAvailable() const {
    return impl_->faceViewAvailable();
}

SDL_Rect AttendBgfxRenderer::pokemonPointerRect() const {
    return impl_->pokemonPointerRect();
}

int AttendBgfxRenderer::weatherModeCount() const {
    return impl_->weatherModeCount();
}

int AttendBgfxRenderer::textureVariantIndex() const {
    return impl_->textureVariantIndex();
}

int AttendBgfxRenderer::textureVariantCount() const {
    return impl_->textureVariantCount();
}

std::string AttendBgfxRenderer::textureVariantLabel(int index) const {
    return impl_->textureVariantLabel(index);
}

int AttendBgfxRenderer::formVariantIndex() const {
    return impl_->formVariantIndex();
}

int AttendBgfxRenderer::formVariantCount() const {
    return impl_->formVariantCount();
}

std::string AttendBgfxRenderer::formVariantLabel(int index) const {
    return impl_->formVariantLabel(index);
}

void AttendBgfxRenderer::render(double scene_time_seconds, int width, int height) {
    impl_->render(scene_time_seconds, width, height);
}

void AttendBgfxRenderer::queueScreenshot(const std::string& output_path) {
    impl_->queueScreenshot(output_path);
}

} // namespace pr::gameplay::attend::rendering
