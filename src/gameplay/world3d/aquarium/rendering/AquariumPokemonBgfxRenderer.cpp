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
    constexpr float kPi = 3.14159265358979323846f;
    const float yaw = actor.world_yaw_degrees * kPi / 180.0f;
    const float pitch = actor.world_pitch_degrees * kPi / 180.0f;
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    matrix[0] = cy * actor.model_scale;
    matrix[2] = -sy * actor.model_scale;
    matrix[4] = sy * sp * actor.model_scale;
    matrix[5] = cp * actor.model_scale;
    matrix[6] = cy * sp * actor.model_scale;
    matrix[8] = sy * cp * actor.model_scale;
    matrix[9] = -sp * actor.model_scale;
    matrix[10] = cy * cp * actor.model_scale;
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
    struct PrimitivePose {
        bgfx::DynamicVertexBufferHandle vertices = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle indices = BGFX_INVALID_HANDLE;
        std::uint32_t index_count = 0;
        int material = -1;
        bool visible = false;
        void destroy() {
            if (bgfx::isValid(vertices)) bgfx::destroy(vertices);
            if (bgfx::isValid(indices)) bgfx::destroy(indices);
            vertices = BGFX_INVALID_HANDLE;
            indices = BGFX_INVALID_HANDLE;
            index_count = 0;
            visible = false;
        }
    };
    struct Model {
        std::shared_ptr<const attend::AttendPokemonModel> source;
        std::vector<Material> materials;
        std::vector<PrimitivePose> pose;
        std::string pose_animation;
        std::string pose_form;
        std::int64_t pose_tick = -1;
        bool valid = false;
        void destroy() {
            for (PrimitivePose& primitive : pose) primitive.destroy();
            pose.clear();
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
        model.source = attend::loadAttendPokemonModelShared(path, &error);
        if (!model.source || !model.source->valid) {
            last_error_ = "Could not load aquarium Pokemon '" + path + "': " + error;
            std::cerr << "[Aquarium] " << last_error_ << '\n';
            return nullptr;
        }
        model.materials.reserve(model.source->materials.size());
        for (const attend::AttendPokemonMaterial& source : model.source->materials) {
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
                  << " primitives=" << model.source->primitives.size()
                  << " animations=" << model.source->animations.size() << '\n';
        return &model;
    }

    bool updatePose(Model& model, const AquariumPokemonActor& actor) {
        // The source animations were authored for handheld presentation. A
        // 24 Hz pose cache remains smooth while avoiding CPU skinning and GPU
        // allocation at the host display's 60/120 Hz refresh rate.
        constexpr double kPoseFramesPerSecond = 24.0;
        const std::string slot = animationSlot(actor.animation);
        const std::int64_t tick = static_cast<std::int64_t>(
            std::floor(std::max(0.0, actor.animation_time_seconds) * kPoseFramesPerSecond));
        if (model.pose_tick == tick && model.pose_animation == slot &&
            model.pose_form == actor.form) return true;

        if (!model.source) return false;
        const attend::AttendPokemonModel& source_model = *model.source;
        const attend::AttendPokemonAnimation* animation =
            attend::findAttendPokemonAnimation(source_model, slot);
        const double sample_time = static_cast<double>(tick) / kPoseFramesPerSecond;
        const auto globals = attend::buildAttendPokemonGlobals(
            source_model, animation, sample_time);
        const auto skin_matrices = attend::buildAttendPokemonSkinMatrices(source_model, globals);
        if (model.pose.size() != source_model.primitives.size()) {
            for (PrimitivePose& primitive : model.pose) primitive.destroy();
            model.pose.clear();
            model.pose.resize(source_model.primitives.size());
        }

        std::vector<attend::AttendPokemonVertex> skinned;
        std::vector<Vertex> upload_vertices;
        for (std::size_t primitive_index = 0;
             primitive_index < source_model.primitives.size(); ++primitive_index) {
            const attend::AttendPokemonPrimitive& source =
                source_model.primitives[primitive_index];
            PrimitivePose& pose = model.pose[primitive_index];
            pose.visible = attend::attendPrimitiveVisibleForDefaultForm(
                source_model, source, actor.form) && !source.indices.empty();
            if (!pose.visible) continue;
            pose.material = attend::attendMaterialForDefaultPresentation(
                source_model, source.material, actor.form);
            const attend::AttendPokemonMaterial* source_material =
                pose.material >= 0 &&
                pose.material < static_cast<int>(source_model.materials.size())
                    ? &source_model.materials[static_cast<std::size_t>(pose.material)]
                    : nullptr;
            attend::skinAttendPokemonPrimitiveWithPose(
                source_model, source, globals, skin_matrices, skinned);
            if (skinned.empty() || skinned.size() > UINT16_MAX ||
                source.indices.size() > UINT16_MAX) {
                pose.visible = false;
                continue;
            }
            upload_vertices.resize(skinned.size());
            for (std::size_t vertex_index = 0; vertex_index < skinned.size(); ++vertex_index) {
                const auto& vertex = skinned[vertex_index];
                const auto uv = attend::attendPokemonUvForDefaultExpression(
                    vertex, source_material);
                upload_vertices[vertex_index] = Vertex{
                    vertex.x, vertex.y, vertex.z,
                    packAbgr(vertex.r, vertex.g, vertex.b, vertex.a),
                    uv.first, uv.second, vertex.nx, vertex.ny, vertex.nz};
            }
            if (!bgfx::isValid(pose.vertices)) {
                pose.vertices = bgfx::createDynamicVertexBuffer(
                    static_cast<std::uint32_t>(upload_vertices.size()), layout_);
            }
            if (!bgfx::isValid(pose.vertices)) {
                pose.visible = false;
                continue;
            }
            bgfx::update(pose.vertices, 0, bgfx::copy(
                upload_vertices.data(),
                static_cast<std::uint32_t>(upload_vertices.size() * sizeof(Vertex))));
            if (!bgfx::isValid(pose.indices)) {
                std::vector<std::uint16_t> indices(source.indices.size());
                std::transform(source.indices.begin(), source.indices.end(), indices.begin(),
                    [](std::uint32_t index) { return static_cast<std::uint16_t>(index); });
                pose.indices = bgfx::createIndexBuffer(bgfx::copy(
                    indices.data(),
                    static_cast<std::uint32_t>(indices.size() * sizeof(std::uint16_t))));
            }
            pose.index_count = static_cast<std::uint32_t>(source.indices.size());
            pose.visible = pose.visible && bgfx::isValid(pose.indices);
        }
        model.pose_tick = tick;
        model.pose_animation = slot;
        model.pose_form = actor.form;
        return true;
    }

    void submit(std::uint16_t view_id, bool blended_pass) {
        if (!initialized_ || !bgfx::isValid(program_)) return;
        for (const AquariumPokemonActor& actor : actors_) {
            Model* model = ensureModel(actor.model_path);
            if (!model || !updatePose(*model, actor)) continue;
            float matrix[16];
            placementMatrix(actor, matrix);
            for (const PrimitivePose& primitive : model->pose) {
                if (!primitive.visible) continue;
                const int presentation_material = primitive.material;
                const Material* material = presentation_material >= 0 &&
                    presentation_material < static_cast<int>(model->materials.size())
                        ? &model->materials[static_cast<std::size_t>(presentation_material)] : nullptr;
                const bool blended = material && material->blended;
                if (blended != blended_pass) continue;
                const float base_color[4] = {
                    (material ? static_cast<float>(material->color & 0xffU) / 255.0f : 1.0f) *
                        actor.presentation.tint[0],
                    (material ? static_cast<float>((material->color >> 8U) & 0xffU) / 255.0f : 1.0f) *
                        actor.presentation.tint[1],
                    (material ? static_cast<float>((material->color >> 16U) & 0xffU) / 255.0f : 1.0f) *
                        actor.presentation.tint[2],
                    material ? material->alpha_cutoff : 0.0f};
                const float adjust[4]{
                    actor.presentation.brightness * actor.presentation.pokemon_brightness,
                    actor.presentation.saturation,
                    actor.presentation.contrast,
                    0.0f};
                const float zeros[4]{};
                const float light_dir[4]{
                    actor.presentation.light_direction[0],
                    actor.presentation.light_direction[1],
                    actor.presentation.light_direction[2],
                    0.0f};
                const float light_params[4]{
                    actor.presentation.ambient,
                    actor.presentation.directional,
                    actor.presentation.form_shadow,
                    0.0f};
                bgfx::setTransform(matrix);
                bgfx::setVertexBuffer(0, primitive.vertices);
                bgfx::setIndexBuffer(primitive.indices, 0, primitive.index_count);
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
