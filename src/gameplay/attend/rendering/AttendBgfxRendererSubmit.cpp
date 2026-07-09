#include "AttendBgfxRendererInternal.hpp"

namespace pr::gameplay::attend::rendering {

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

} // namespace pr::gameplay::attend::rendering
