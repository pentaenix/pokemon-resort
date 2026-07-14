#include "AttendBgfxRendererInternal.hpp"

#include "gameplay/attend/rendering/AttendPokemonMaterialPolicy.hpp"

namespace pr::gameplay::attend::rendering {

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

    std::vector<bool> authored_visible_materials(pokemon_model_.materials.size(), false);
    for (const AttendPokemonPrimitive& primitive : pokemon_model_.primitives) {
        if (!primitive.default_visible && primitive.visible_for_forms.empty()) continue;
        if (primitive.material >= 0 &&
            primitive.material < static_cast<int>(authored_visible_materials.size())) {
            authored_visible_materials[static_cast<std::size_t>(primitive.material)] = true;
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
        dst.blend = dst.blend ||
            (authored_visible_materials[i] &&
             shouldPromoteAttendTextureToBlend(
                 src,
                 AttendTextureAlphaSummary{
                     dst.texture.minimum_alpha,
                     dst.texture.partial_alpha_fraction}));
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
        buildAttendPokemonGlobals(
            pokemon_model_,
            pokemon_animation_,
            0.0,
            attendPokemonAnimationLoopDurationForForm(pokemon_model_, pokemon_animation_, form_variant_index_));
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
    std::vector<bool> material_blends;
    material_blends.reserve(pokemon_mesh_.materials.size());
    for (const MaterialResource& material : pokemon_mesh_.materials) {
        material_blends.push_back(material.blend);
    }
    sortAttendPokemonDrawOrder(pokemon_model_, material_blends, pokemon_draw_order_);
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
    captureCameraBoundsFromCurrentPose();
    return uploadDynamicMesh(pokemon_mesh_, vertices, indices);
}

} // namespace pr::gameplay::attend::rendering
