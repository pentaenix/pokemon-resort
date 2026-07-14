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

// VCO meshes carry required vertex-color feature layers even when GF bind visibility
// marks them false; keep those compatibility layers without treating them as alpha shells.
bool shouldRenderAttendPokemonPrimitive(
    const AttendPokemonModel& model,
    const AttendPokemonPrimitive& primitive);

// Keep the authored opaque-layer order, then composite alpha-blended ranges last.
void sortAttendPokemonDrawOrder(
    const AttendPokemonModel& model,
    const std::vector<bool>& material_blends,
    std::vector<std::size_t>& primitive_order);

} // namespace pr::gameplay::attend::rendering
