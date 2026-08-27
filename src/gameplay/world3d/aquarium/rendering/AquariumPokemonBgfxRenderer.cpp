#include "gameplay/world3d/aquarium/rendering/AquariumPokemonBgfxRenderer.hpp"

#include "gameplay/attend/rendering/AttendPokemonModel.hpp"
#include "gameplay/attend/rendering/AttendPokemonMaterialPolicy.hpp"
#include "gameplay/attend/rendering/AttendPokemonPresentation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <unordered_map>
#include <utility>

namespace pr::gameplay::world3d::aquarium::rendering {
namespace attend = gameplay::attend::rendering;
namespace {

struct Vertex {
    float x, y, z;
    std::uint32_t abgr;
    float u, v;
    float nx, ny, nz;
};

std::uint32_t packAbgr(float r, float g, float b, float a) {
    const auto channel = [](float value) {
        return static_cast<std::uint32_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return channel(r) | channel(g) << 8U | channel(b) << 16U | channel(a) << 24U;
}

std::string animationSlot(std::string semantic) {
    if (semantic == "walk" || semantic == "swim") return "slot6_02";
    if (semantic == "run") return "slot6_03";
    if (semantic == "idle_default") return "slot4_00";
    if (semantic == "idle_ground") return "slot5_00";
    if (semantic == "fly_flap") return "slot6_01";
    return semantic;
}

std::uint64_t samplerFlags(int wrap_s, int wrap_t, bool gf_wrap = false) {
    if (gf_wrap) {
        const auto convert = [](int wrap) {
            if (wrap == 0 || wrap == 1) return 33071;
            if (wrap == 3) return 33648;
            return 10497;
        };
        wrap_s = convert(wrap_s);
        wrap_t = convert(wrap_t);
    }
    std::uint64_t flags =
        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
    if (wrap_s == 33071) flags |= BGFX_SAMPLER_U_CLAMP;
    else if (wrap_s == 33648) flags |= BGFX_SAMPLER_U_MIRROR;
    if (wrap_t == 33071) flags |= BGFX_SAMPLER_V_CLAMP;
    else if (wrap_t == 33648) flags |= BGFX_SAMPLER_V_MIRROR;
    return flags;
}

void placementMatrix(const AquariumPokemonActor& actor, float (&matrix)[16]) {
    std::fill(std::begin(matrix), std::end(matrix), 0.0f);
    const float radians = actor.world_yaw_degrees * 3.14159265358979323846f / 180.0f;
    const float c = std::cos(radians) * actor.model_scale;
    const float s = std::sin(radians) * actor.model_scale;
    matrix[0] = c; matrix[2] = -s;
    matrix[5] = actor.model_scale;
    matrix[8] = s; matrix[10] = c;
    matrix[12] = actor.world_position[0];
    matrix[13] = actor.world_position[1];
    matrix[14] = actor.world_position[2];
    matrix[15] = 1.0f;
}

} // namespace

class AquariumPokemonBgfxRenderer::Impl {
public:
    struct Texture {
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        void destroy() {
            if (bgfx::isValid(handle)) bgfx::destroy(handle);
            handle = BGFX_INVALID_HANDLE;
        }
    };
    struct Material {
        Texture texture;
        std::uint32_t color = 0xffffffffU;
        float alpha_cutoff = 0.0f;
        bool blended = false;
        bool additive = false;
        bool cull_backface = true;
        std::uint64_t sampler_flags = samplerFlags(10497, 10497);
    };
    struct Model {
        attend::AttendPokemonModel source;
        std::vector<Material> materials;
        bool valid = false;
        void destroy() {
            for (Material& material : materials) material.texture.destroy();
            materials.clear();
            source = {};
            valid = false;
        }
    };

    ~Impl() { shutdown(); }

    void initialize(
        const bgfx::VertexLayout& layout,
        bgfx::ProgramHandle program,
        bgfx::UniformHandle texture_uniform,
        bgfx::UniformHandle tint_cutoff_uniform,
        bgfx::UniformHandle color_adjust_uniform,
        bgfx::UniformHandle texture_blur_uniform,
        bgfx::UniformHandle uv_offset_uniform,
        bgfx::UniformHandle light_dir_uniform,
        bgfx::UniformHandle light_params_uniform) {
        layout_ = layout;
        program_ = program;
        texture_uniform_ = texture_uniform;
        tint_cutoff_uniform_ = tint_cutoff_uniform;
        color_adjust_uniform_ = color_adjust_uniform;
        texture_blur_uniform_ = texture_blur_uniform;
        uv_offset_uniform_ = uv_offset_uniform;
        light_dir_uniform_ = light_dir_uniform;
        light_params_uniform_ = light_params_uniform;
        std::uint8_t white[4]{255, 255, 255, 255};
        white_.handle = bgfx::createTexture2D(
            1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, bgfx::copy(white, sizeof(white)));
        initialized_ = bgfx::isValid(white_.handle);
    }

    void shutdown() {
        for (auto& [path, model] : models_) model.destroy();
        models_.clear();
        white_.destroy();
        initialized_ = false;
    }

    void setActors(const std::vector<AquariumPokemonActor>& actors) {
        actors_ = actors;
        if (!initialized_) return;
        for (const AquariumPokemonActor& actor : actors_) ensureModel(actor.model_path);
    }

    Texture upload(const attend::AttendRgbaImage& image) {
        Texture texture;
        if (!image.valid()) return texture;
        texture.handle = bgfx::createTexture2D(
            static_cast<std::uint16_t>(image.width),
            static_cast<std::uint16_t>(image.height),
            false, 1, bgfx::TextureFormat::RGBA8,
            BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT,
            bgfx::copy(image.pixels.data(), static_cast<std::uint32_t>(image.pixels.size())));
        return texture;
    }

    Model* ensureModel(const std::string& path) {
        auto [it, inserted] = models_.try_emplace(path);
        Model& model = it->second;
        if (!inserted) return model.valid ? &model : nullptr;
        std::string error;
        model.source = attend::loadAttendPokemonModel(path, &error);
        if (!model.source.valid) {
            last_error_ = "Could not load aquarium Pokemon '" + path + "': " + error;
            std::cerr << "[Aquarium] " << last_error_ << '\n';
            return nullptr;
        }
        model.materials.reserve(model.source.materials.size());
        for (const attend::AttendPokemonMaterial& source : model.source.materials) {
            Material material;
            attend::AttendRgbaImage image;
            if (source.pokemon_eye && source.has_base_color_texture && source.has_emissive_texture) {
                image = attend::composeAttendPokemonEye(
                    source.base_color_bytes, source.emissive_bytes);
            } else if (source.has_base_color_texture) {
                image = attend::decodeAttendRgba(source.base_color_bytes);
            }
            material.texture = upload(image);
            material.color = packAbgr(
                source.base_color[0], source.base_color[1], source.base_color[2], source.base_color[3]);
            bool mask = source.render_class == attend::AttendRenderClass::Mask;
            material.blended = source.render_class == attend::AttendRenderClass::Blend ||
                source.render_class == attend::AttendRenderClass::Additive ||
                source.render_class == attend::AttendRenderClass::UniformDecal ||
                source.base_color[3] < 0.999f;
            material.blended = material.blended ||
                attend::shouldPromoteAttendTextureToBlend(
                    source,
                    attend::AttendTextureAlphaSummary{
                        image.minimum_alpha, image.partial_alpha_fraction});
            if (!source.has_rae_policy && !source.has_alpha_mode) {
                material.blended = material.blended || image.has_partial_alpha;
                mask = mask || (image.has_zero_alpha && !material.blended &&
                    source.base_color[3] >= 0.999f);
            }
            material.alpha_cutoff = mask ? std::max(0.5f, source.alpha_cutoff) : 0.0f;
            material.additive = source.render_class == attend::AttendRenderClass::Additive;
            material.cull_backface = attend::shouldCullAttendPokemonMaterial(source);
            if ((source.material_role == attend::AttendMaterialRole::EyeSclera ||
                 source.material_role == attend::AttendMaterialRole::Mouth) &&
                source.eye_sheet.enabled) {
                material.sampler_flags = samplerFlags(
                    source.eye_sheet.wrap_s, source.eye_sheet.wrap_t, true);
            } else {
                material.sampler_flags = samplerFlags(
                    source.base_color_sampler.wrap_s,
                    source.base_color_sampler.wrap_t);
            }
            model.materials.push_back(std::move(material));
        }
        model.valid = true;
        std::cerr << "[Aquarium] Loaded Attend Pokemon " << path
                  << " primitives=" << model.source.primitives.size()
                  << " animations=" << model.source.animations.size() << '\n';
        return &model;
    }

    void submit(std::uint16_t view_id, bool blended_pass) {
        if (!initialized_ || !bgfx::isValid(program_)) return;
        for (const AquariumPokemonActor& actor : actors_) {
            Model* model = ensureModel(actor.model_path);
            if (!model) continue;
            const attend::AttendPokemonAnimation* animation = attend::findAttendPokemonAnimation(
                model->source, animationSlot(actor.animation));
            const auto globals = attend::buildAttendPokemonGlobals(
                model->source, animation, actor.animation_time_seconds);
            const auto skin_matrices = attend::buildAttendPokemonSkinMatrices(model->source, globals);
            float matrix[16];
            placementMatrix(actor, matrix);
            for (const attend::AttendPokemonPrimitive& primitive : model->source.primitives) {
                if (!attend::attendPrimitiveVisibleForDefaultForm(
                        model->source, primitive, actor.form) ||
                    primitive.indices.empty()) continue;
                const int presentation_material = attend::attendMaterialForDefaultPresentation(
                    model->source, primitive.material, actor.form);
                const Material* material = presentation_material >= 0 &&
                    presentation_material < static_cast<int>(model->materials.size())
                        ? &model->materials[static_cast<std::size_t>(presentation_material)] : nullptr;
                const attend::AttendPokemonMaterial* source_material =
                    presentation_material >= 0 &&
                    presentation_material < static_cast<int>(model->source.materials.size())
                        ? &model->source.materials[static_cast<std::size_t>(presentation_material)]
                        : nullptr;
                const bool blended = material && material->blended;
                if (blended != blended_pass) continue;
                std::vector<attend::AttendPokemonVertex> skinned;
                attend::skinAttendPokemonPrimitiveWithPose(
                    model->source, primitive, globals, skin_matrices, skinned);
                if (skinned.empty() || skinned.size() > UINT16_MAX || primitive.indices.size() > UINT16_MAX) continue;
                bgfx::TransientVertexBuffer vertices;
                bgfx::TransientIndexBuffer indices;
                if (!bgfx::allocTransientBuffers(
                        &vertices, layout_, static_cast<std::uint32_t>(skinned.size()),
                        &indices, static_cast<std::uint32_t>(primitive.indices.size()))) continue;
                auto* out_vertices = reinterpret_cast<Vertex*>(vertices.data);
                for (std::size_t i = 0; i < skinned.size(); ++i) {
                    const auto& source = skinned[i];
                    const auto uv = attend::attendPokemonUvForDefaultExpression(
                        source, source_material);
                    out_vertices[i] = Vertex{
                        source.x, source.y, source.z,
                        packAbgr(source.r, source.g, source.b, source.a),
                        uv.first, uv.second, source.nx, source.ny, source.nz};
                }
                auto* out_indices = reinterpret_cast<std::uint16_t*>(indices.data);
                for (std::size_t i = 0; i < primitive.indices.size(); ++i) {
                    out_indices[i] = static_cast<std::uint16_t>(primitive.indices[i]);
                }
                const float base_color[4] = {
                    material ? static_cast<float>(material->color & 0xffU) / 255.0f : 1.0f,
                    material ? static_cast<float>((material->color >> 8U) & 0xffU) / 255.0f : 1.0f,
                    material ? static_cast<float>((material->color >> 16U) & 0xffU) / 255.0f : 1.0f,
                    material ? material->alpha_cutoff : 0.0f};
                const float adjust[4]{1.0f, 1.0f, 1.0f, 0.0f};
                const float zeros[4]{};
                const float light_dir[4]{-0.3f, 0.8f, -0.45f, 0.0f};
                const float light_params[4]{0.72f, 0.28f, 0.0f, 0.0f};
                bgfx::setTransform(matrix);
                bgfx::setVertexBuffer(0, &vertices);
                bgfx::setIndexBuffer(&indices);
                bgfx::setTexture(
                    0, texture_uniform_,
                    material && bgfx::isValid(material->texture.handle)
                        ? material->texture.handle : white_.handle,
                    material ? material->sampler_flags : samplerFlags(10497, 10497));
                bgfx::setUniform(tint_cutoff_uniform_, base_color);
                bgfx::setUniform(color_adjust_uniform_, adjust);
                bgfx::setUniform(texture_blur_uniform_, zeros);
                bgfx::setUniform(uv_offset_uniform_, zeros);
                bgfx::setUniform(light_dir_uniform_, light_dir);
                bgfx::setUniform(light_params_uniform_, light_params);
                const std::uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                    BGFX_STATE_DEPTH_TEST_LESS |
                    (material && material->additive
                        ? BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE)
                        : (blended ? BGFX_STATE_BLEND_ALPHA : BGFX_STATE_WRITE_Z)) |
                    (material && material->cull_backface ? BGFX_STATE_CULL_CCW : 0);
                bgfx::setState(state);
                bgfx::submit(view_id, program_);
            }
        }
    }

    bgfx::VertexLayout layout_{};
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle texture_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle tint_cutoff_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle color_adjust_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle texture_blur_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uv_offset_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_dir_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_params_uniform_ = BGFX_INVALID_HANDLE;
    Texture white_;
    std::unordered_map<std::string, Model> models_;
    std::vector<AquariumPokemonActor> actors_;
    bool initialized_ = false;
    std::string last_error_;
};

AquariumPokemonBgfxRenderer::AquariumPokemonBgfxRenderer() : impl_(std::make_unique<Impl>()) {}
AquariumPokemonBgfxRenderer::~AquariumPokemonBgfxRenderer() = default;
void AquariumPokemonBgfxRenderer::initialize(
    const bgfx::VertexLayout& layout, bgfx::ProgramHandle program,
    bgfx::UniformHandle texture_uniform, bgfx::UniformHandle tint_cutoff_uniform,
    bgfx::UniformHandle color_adjust_uniform, bgfx::UniformHandle texture_blur_uniform,
    bgfx::UniformHandle uv_offset_uniform, bgfx::UniformHandle light_dir_uniform,
    bgfx::UniformHandle light_params_uniform) {
    impl_->initialize(layout, program, texture_uniform, tint_cutoff_uniform, color_adjust_uniform,
        texture_blur_uniform, uv_offset_uniform, light_dir_uniform, light_params_uniform);
}
void AquariumPokemonBgfxRenderer::shutdown() { impl_->shutdown(); }
void AquariumPokemonBgfxRenderer::setActors(const std::vector<AquariumPokemonActor>& actors) { impl_->setActors(actors); }
void AquariumPokemonBgfxRenderer::submit(std::uint16_t view_id, bool blended_pass) { impl_->submit(view_id, blended_pass); }
const std::string& AquariumPokemonBgfxRenderer::lastError() const { return impl_->last_error_; }

} // namespace pr::gameplay::world3d::aquarium::rendering
