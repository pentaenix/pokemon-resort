#pragma once

#include "gameplay/attend/rendering/AttendPokemonModel.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pr::gameplay::attend::rendering {

struct AttendRgbaImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
    std::uint8_t minimum_alpha = 255;
    float partial_alpha_fraction = 0.0f;
    bool has_zero_alpha = false;
    bool has_partial_alpha = false;
    bool valid() const {
        return width > 0 && height > 0 &&
            pixels.size() == static_cast<std::size_t>(width * height * 4);
    }
};

AttendRgbaImage decodeAttendRgba(const std::vector<std::uint8_t>& bytes);
AttendRgbaImage composeAttendPokemonEye(
    const std::vector<std::uint8_t>& base_bytes,
    const std::vector<std::uint8_t>& emissive_bytes);

std::string defaultAttendPokemonForm(const AttendPokemonModel& model);
bool attendPrimitiveVisibleForDefaultForm(
    const AttendPokemonModel& model,
    const AttendPokemonPrimitive& primitive,
    const std::string& requested_form = {});
int attendMaterialForDefaultPresentation(
    const AttendPokemonModel& model,
    int material_index,
    const std::string& requested_form = {});
std::pair<float, float> attendPokemonUvForDefaultExpression(
    const AttendPokemonVertex& vertex,
    const AttendPokemonMaterial* material);

} // namespace pr::gameplay::attend::rendering
