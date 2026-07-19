#include "gameplay/attend/rendering/AttendPokemonMaterialPolicy.hpp"

#include <algorithm>
#include <cctype>

namespace pr::gameplay::attend::rendering {

bool shouldPromoteAttendTextureToBlend(
    const AttendPokemonMaterial& material,
    const AttendTextureAlphaSummary& alpha) {
    constexpr std::uint8_t kStrongTranslucencyMaximumAlpha = 127;
    constexpr float kMinimumPartialAlphaFraction = 0.05f;
    return !material.has_authoritative_pica &&
        material.render_class == AttendRenderClass::Opaque &&
        material.nitro_texture_alpha == "translucent" &&
        alpha.minimum_alpha <= kStrongTranslucencyMaximumAlpha &&
        alpha.partial_alpha_fraction >= kMinimumPartialAlphaFraction;
}

bool shouldTreatLegacyBinaryAlphaBlendAsMask(
    bool declared_blend,
    bool has_zero_alpha,
    bool has_partial_alpha,
    float base_alpha) {
    return declared_blend &&
        has_zero_alpha &&
        !has_partial_alpha &&
        base_alpha >= 0.999f;
}

namespace {

bool containsVco(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value.find("vco") != std::string::npos;
}

} // namespace

bool shouldRenderAttendPokemonPrimitive(
    const AttendPokemonModel& model,
    const AttendPokemonPrimitive& primitive) {
    if (primitive.default_visible || !primitive.visible_for_forms.empty()) return true;
    if (primitive.material >= 0 && primitive.material < static_cast<int>(model.materials.size())) {
        const AttendPokemonMaterial& material =
            model.materials[static_cast<std::size_t>(primitive.material)];
        if (material.has_authoritative_pica) return false;
        if (containsVco(material.name)) return true;
    }
    return primitive.mesh_node >= 0 &&
        primitive.mesh_node < static_cast<int>(model.nodes.size()) &&
        containsVco(model.nodes[static_cast<std::size_t>(primitive.mesh_node)].name);
}

bool shouldRenderAttendSeparateEyeIris(
    int current_eye_expression_frame,
    int normal_eye_expression_frame) {
    return current_eye_expression_frame == normal_eye_expression_frame;
}

bool shouldCullAttendPokemonMaterial(const AttendPokemonMaterial& material) {
    if (material.double_sided) return false;
    return material.material_role != AttendMaterialRole::EyeSclera &&
        material.material_role != AttendMaterialRole::EyeIris &&
        material.material_role != AttendMaterialRole::Mouth;
}

bool attendModelUsesSeparateEyeIris(const AttendPokemonModel& model) {
    return std::any_of(
        model.materials.begin(),
        model.materials.end(),
        [](const AttendPokemonMaterial& material) {
            return material.material_role == AttendMaterialRole::EyeIris;
        });
}

void sortAttendPokemonDrawOrder(
    const AttendPokemonModel& model,
    const std::vector<bool>& material_blends,
    std::vector<std::size_t>& primitive_order) {
    std::stable_sort(
        primitive_order.begin(),
        primitive_order.end(),
        [&model](std::size_t a, std::size_t b) {
            const AttendPokemonPrimitive& lhs = model.primitives[a];
            const AttendPokemonPrimitive& rhs = model.primitives[b];
            if (lhs.render_order != rhs.render_order) return lhs.render_order < rhs.render_order;
            return lhs.scene_order < rhs.scene_order;
        });
    std::stable_partition(
        primitive_order.begin(),
        primitive_order.end(),
        [&model, &material_blends](std::size_t primitive_index) {
            const int material = model.primitives[primitive_index].material;
            return material < 0 ||
                material >= static_cast<int>(material_blends.size()) ||
                !material_blends[static_cast<std::size_t>(material)];
        });
}

} // namespace pr::gameplay::attend::rendering
