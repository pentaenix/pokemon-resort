#pragma once

#include "gameplay/attend/rendering/AttendPokemonModel.hpp"

#include <cstdint>
#include <cstddef>
#include <vector>

namespace pr::gameplay::attend::rendering {

struct AttendTextureAlphaSummary {
    std::uint8_t minimum_alpha = 255;
    float partial_alpha_fraction = 0.0f;
};

// Recover strong authored 3DS translucency that an opaque RAE fallback suppressed.
bool shouldPromoteAttendTextureToBlend(
    const AttendPokemonMaterial& material,
    const AttendTextureAlphaSummary& alpha);

// Older map GLBs labeled binary-alpha foliage and props as BLEND. Those are
// cutouts and must write depth so adjacent floor pieces cannot draw through them.
bool shouldTreatLegacyBinaryAlphaBlendAsMask(
    bool declared_blend,
    bool has_zero_alpha,
    bool has_partial_alpha,
    float base_alpha);

// Legacy exports hid every VCO mesh; keep those compatibility feature layers. New exports
// carry authoritative PICA/visibility metadata and must keep explicitly hidden alternates off.
bool shouldRenderAttendPokemonPrimitive(
    const AttendPokemonModel& model,
    const AttendPokemonPrimitive& primitive);

// Separate iris meshes complete only the normal-open sheet frame. Authored expression
// frames already contain their full eye shape and must replace those irises.
bool shouldRenderAttendSeparateEyeIris(
    int current_eye_expression_frame,
    int normal_eye_expression_frame);

// Facial expression meshes are thin authored overlays. Rendering both sides
// avoids camera-facing holes without weakening culling on body geometry.
bool shouldCullAttendPokemonMaterial(const AttendPokemonMaterial& material);

// Only models with independently-authored iris meshes need a sclera aperture
// stencil. Models whose pupils are already baked into Eye must bypass it.
bool attendModelUsesSeparateEyeIris(const AttendPokemonModel& model);

// Keep the authored opaque-layer order, then composite alpha-blended ranges last.
void sortAttendPokemonDrawOrder(
    const AttendPokemonModel& model,
    const std::vector<bool>& material_blends,
    std::vector<std::size_t>& primitive_order);

} // namespace pr::gameplay::attend::rendering
