#include "gameplay/world3d/aquarium/rendering/AquariumPokemonBgfxRenderer.hpp"
#include "gameplay/world3d/aquarium/rendering/AquariumPokemonEmission.hpp"
#include "gameplay/world3d/aquarium/rendering/AquariumPokemonRenderPose.hpp"

#include "gameplay/world3d/aquarium/rendering/AquariumPokemonRuntimeLod.hpp"

#include "gameplay/attend/rendering/AttendPokemonModel.hpp"
#include "gameplay/attend/rendering/AttendPokemonMaterialPolicy.hpp"
#include "gameplay/attend/rendering/AttendPokemonPresentation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdint>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
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
        AquariumPokemonEmission emission = AquariumPokemonEmission::None;
        std::uint32_t color = 0xffffffffU;
        float alpha_cutoff = 0.0f;
        bool blended = false;
        bool additive = false;
        bool pulse = false;
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
    struct ActorPose {
        std::vector<PrimitivePose> primitives;
        std::string animation;
        std::string form;
        std::int64_t tick = -1;
        void destroy() {
            for (PrimitivePose& primitive : primitives) primitive.destroy();
            primitives.clear();
        }
    };
    struct Model {
        std::shared_ptr<const attend::AttendPokemonModel> source;
        AquariumPokemonRuntimeLod runtime_lod;
        std::array<float,4> pulse_bounds{0,0,0,1};
        std::vector<Material> materials;
        // Source geometry, materials, and textures remain shared. Only the
        // small runtime-LOD skinned pose buffers are unique per visible actor.
        std::unordered_map<std::string, ActorPose> actor_poses;
        bool valid = false;
        void destroy() {
            for (auto& [id, pose] : actor_poses) pose.destroy();
            actor_poses.clear();
            for (Material& material : materials) material.texture.destroy();
            materials.clear();
            runtime_lod = {};
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
        bgfx::UniformHandle light_params_uniform,
        bgfx::ProgramHandle pulse_program) {
        layout_ = layout;
        program_ = program;
        pulse_program_ = pulse_program;
        pulse_bounds_uniform_=bgfx::createUniform("u_pulseBounds",bgfx::UniformType::Vec4);
        pulse_state_uniform_=bgfx::createUniform("u_pulseState",bgfx::UniformType::Vec4);
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
        if(bgfx::isValid(pulse_bounds_uniform_)) bgfx::destroy(pulse_bounds_uniform_);
        if(bgfx::isValid(pulse_state_uniform_)) bgfx::destroy(pulse_state_uniform_);
        pulse_bounds_uniform_=pulse_state_uniform_=BGFX_INVALID_HANDLE;
        for (auto& [path, model] : models_) model.destroy();
        models_.clear();
        white_.destroy();
        initialized_ = false;
    }

    void setActors(const std::vector<AquariumPokemonActor>& actors) {
        actors_ = actors;
        if (!initialized_) return;
        std::unordered_set<std::string> active_actor_ids;
        for (const AquariumPokemonActor& actor : actors_) {
            active_actor_ids.insert(actor.id);
            ensureModel(actor.model_path);
        }
        for (auto& [path, model] : models_) {
            for (auto it = model.actor_poses.begin(); it != model.actor_poses.end();) {
                if (active_actor_ids.contains(it->first)) {
                    ++it;
                    continue;
                }
                it->second.destroy();
                it = model.actor_poses.erase(it);
            }
        }
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
        model.runtime_lod = buildAquariumPokemonRuntimeLod(*model.source);
        model.pulse_bounds=aquariumPulseBounds(*model.source);
        model.materials.reserve(model.source->materials.size());
        for (const attend::AttendPokemonMaterial& source : model.source->materials) {
            Material material;
            material.emission = aquariumPokemonEmission(path, source.name);
            material.pulse=material.emission!=AquariumPokemonEmission::None &&
                (source.name=="BodyANeolant_Inc" || source.name=="BodyBNeolant_Inc");
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
            if (material.emission == AquariumPokemonEmission::GlowShell) {
                // The black-backed glow texture is an additive shell, not an
                // opaque black cover over the luminous inner bulb.
                material.blended = material.additive = true;
                material.alpha_cutoff = 0.0f;
            }
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
                  << " animations=" << model.source->animations.size()
                  << " runtimeLodTriangles="
                  << model.runtime_lod.statistics.source_triangles << "->"
                  << model.runtime_lod.statistics.render_triangles << '\n';
        return &model;
    }

    ActorPose* updatePose(Model& model, const AquariumPokemonActor& actor) {
        // The source animations were authored for handheld presentation. A
        // 24 Hz pose cache remains smooth while avoiding CPU skinning and GPU
        // allocation at the host display's 60/120 Hz refresh rate.
        constexpr double kPoseFramesPerSecond = 24.0;
        const std::string slot = animationSlot(actor.animation);
        const double playback_time = std::max(0.0, actor.animation_time_seconds) *
            std::max(0.01f, actor.animation_playback_rate);
        const std::int64_t tick = static_cast<std::int64_t>(
            std::floor(playback_time * kPoseFramesPerSecond));
        ActorPose& actor_pose = model.actor_poses[actor.id];
        if (actor_pose.tick == tick && actor_pose.animation == slot &&
            actor_pose.form == actor.form) return &actor_pose;

        if (!model.source) return nullptr;
        const attend::AttendPokemonModel& source_model = *model.source;
        const attend::AttendPokemonAnimation* animation =
            attend::findAttendPokemonAnimation(source_model, slot);
        const double sample_time = static_cast<double>(tick) / kPoseFramesPerSecond;
        const auto globals = attend::buildAttendPokemonGlobals(
            source_model, animation, sample_time);
        const auto skin_matrices = attend::buildAttendPokemonSkinMatrices(source_model, globals);
        if (actor_pose.primitives.size() != source_model.primitives.size()) {
            actor_pose.destroy();
            actor_pose.primitives.resize(source_model.primitives.size());
        }

        std::vector<attend::AttendPokemonVertex> skinned;
        std::vector<Vertex> upload_vertices;
        for (std::size_t primitive_index = 0;
             primitive_index < source_model.primitives.size(); ++primitive_index) {
            const attend::AttendPokemonPrimitive& original =
                source_model.primitives[primitive_index];
            const attend::AttendPokemonPrimitive& source =
                primitive_index < model.runtime_lod.replacements.size() &&
                    model.runtime_lod.replacements[primitive_index]
                    ? *model.runtime_lod.replacements[primitive_index]
                    : original;
            PrimitivePose& pose = actor_pose.primitives[primitive_index];
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
        actor_pose.tick = tick;
        actor_pose.animation = slot;
        actor_pose.form = actor.form;
        return &actor_pose;
    }

    void submit(std::uint16_t view_id, bool blended_pass, bool emission_pass = false) {
        if (!initialized_ || !bgfx::isValid(program_)) return;
        for (const AquariumPokemonActor& actor : actors_) {
            Model* model = ensureModel(actor.model_path);
            ActorPose* pose = model ? updatePose(*model, actor) : nullptr;
            if (!pose) continue;
            float matrix[16];
            aquariumPokemonPlacementMatrix(actor, matrix);
            for (const PrimitivePose& primitive : pose->primitives) {
                if (!primitive.visible) continue;
                const int presentation_material = primitive.material;
                const Material* material = presentation_material >= 0 &&
                    presentation_material < static_cast<int>(model->materials.size())
                        ? &model->materials[static_cast<std::size_t>(presentation_material)] : nullptr;
                const bool blended = material && material->blended;
                if (emission_pass ? (!material || material->emission == AquariumPokemonEmission::None)
                                  : (blended != blended_pass)) continue;
                const auto presentation = aquariumEmissionPresentation(actor.presentation,
                    material ? material->emission : AquariumPokemonEmission::None);
                const float base_color[4] = {
                    (material ? static_cast<float>(material->color & 0xffU) / 255.0f : 1.0f) *
                        presentation.tint[0],
                    (material ? static_cast<float>((material->color >> 8U) & 0xffU) / 255.0f : 1.0f) *
                        presentation.tint[1],
                    (material ? static_cast<float>((material->color >> 16U) & 0xffU) / 255.0f : 1.0f) *
                        presentation.tint[2],
                    material ? material->alpha_cutoff : 0.0f};
                const float adjust[4]{
                    presentation.brightness * presentation.pokemon_brightness,
                    presentation.saturation,
                    presentation.contrast,
                    0.0f};
                const float zeros[4]{};
                const float light_dir[4]{
                    presentation.light_direction[0],
                    presentation.light_direction[1],
                    presentation.light_direction[2],
                    0.0f};
                const float light_params[4]{
                    presentation.ambient,
                    presentation.directional,
                    presentation.form_shadow,
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
                const std::uint64_t state = (emission_pass
                    ? BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LEQUAL |
                        BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_FACTOR,
                            material->emission == AquariumPokemonEmission::GlowShell
                                ? BGFX_STATE_BLEND_ONE : BGFX_STATE_BLEND_INV_FACTOR)
                    : BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                    BGFX_STATE_DEPTH_TEST_LESS |
                    (material && material->additive
                        ? BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE)
                        : (blended ? BGFX_STATE_BLEND_ALPHA : BGFX_STATE_WRITE_Z))) |
                    (material && material->cull_backface
                        ? (aquariumUsesClockwiseBackfaceCull(actor) ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW)
                        : 0);
                const float retention = actor.presentation.emission.fog_retention;
                bgfx::setState(state, packAbgr(retention, retention, retention, retention));
                if(material && material->pulse && bgfx::isValid(pulse_program_)) {
                    const auto pulse=aquariumEmissionPulse(actor.animation_time_seconds);
                    bgfx::setUniform(pulse_bounds_uniform_,model->pulse_bounds.data());
                    bgfx::setUniform(pulse_state_uniform_,pulse.data());
                    bgfx::submit(view_id,pulse_program_);
                } else bgfx::submit(view_id, program_);
            }
        }
    }

    bgfx::VertexLayout layout_{};
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle pulse_program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle pulse_bounds_uniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle pulse_state_uniform_ = BGFX_INVALID_HANDLE;
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
    bgfx::UniformHandle light_params_uniform, bgfx::ProgramHandle pulse_program) {
    impl_->initialize(layout, program, texture_uniform, tint_cutoff_uniform, color_adjust_uniform,
        texture_blur_uniform, uv_offset_uniform, light_dir_uniform, light_params_uniform, pulse_program);
}
void AquariumPokemonBgfxRenderer::shutdown() { impl_->shutdown(); }
void AquariumPokemonBgfxRenderer::setActors(const std::vector<AquariumPokemonActor>& actors) { impl_->setActors(actors); }
void AquariumPokemonBgfxRenderer::submit(std::uint16_t view_id, bool blended_pass) { impl_->submit(view_id, blended_pass); }
void AquariumPokemonBgfxRenderer::submitEmission(std::uint16_t view_id) { impl_->submit(view_id, false, true); }
const std::string& AquariumPokemonBgfxRenderer::lastError() const { return impl_->last_error_; }

} // namespace pr::gameplay::world3d::aquarium::rendering
