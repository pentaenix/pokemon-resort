#include "gameplay/attend/rendering/AttendPokemonPresentation.hpp"

#include "gameplay/attend/rendering/AttendPokemonMaterialPolicy.hpp"

#include <SDL_image.h>

#include <algorithm>
#include <cmath>

namespace pr::gameplay::attend::rendering {

AttendRgbaImage decodeAttendRgba(const std::vector<std::uint8_t>& bytes) {
    AttendRgbaImage out;
    if (bytes.empty()) return out;
    SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
    SDL_Surface* loaded = rw ? IMG_Load_RW(rw, 1) : nullptr;
    if (!loaded) return out;
    SDL_Surface* rgba = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(loaded);
    if (!rgba) return out;
    out.width = rgba->w;
    out.height = rgba->h;
    const auto* source = static_cast<const std::uint8_t*>(rgba->pixels);
    out.pixels.assign(source, source + static_cast<std::size_t>(rgba->w * rgba->h * 4));
    int partial = 0;
    for (std::size_t i = 3; i < out.pixels.size(); i += 4U) {
        const std::uint8_t alpha = out.pixels[i];
        out.minimum_alpha = std::min(out.minimum_alpha, alpha);
        out.has_zero_alpha = out.has_zero_alpha || alpha == 0;
        if (alpha > 0 && alpha < 255) {
            out.has_partial_alpha = true;
            ++partial;
        }
    }
    const int count = rgba->w * rgba->h;
    out.partial_alpha_fraction = count > 0
        ? static_cast<float>(partial) / static_cast<float>(count)
        : 0.0f;
    SDL_FreeSurface(rgba);
    return out;
}

AttendRgbaImage composeAttendPokemonEye(
    const std::vector<std::uint8_t>& base_bytes,
    const std::vector<std::uint8_t>& emissive_bytes) {
    AttendRgbaImage base = decodeAttendRgba(base_bytes);
    AttendRgbaImage mask = decodeAttendRgba(emissive_bytes);
    if (!base.valid() || !mask.valid()) return {};
    int visible = 0;
    int nonwhite = 0;
    for (std::size_t i = 0; i < base.pixels.size(); i += 4U) {
        if (base.pixels[i + 3U] == 0) continue;
        ++visible;
        if (base.pixels[i] < 245 || base.pixels[i + 1U] < 245 ||
            base.pixels[i + 2U] < 245) ++nonwhite;
    }
    if (visible > 0 && static_cast<float>(nonwhite) / static_cast<float>(visible) > 0.05f) {
        return base;
    }

    int min_x = mask.width;
    int min_y = mask.height;
    int max_x = -1;
    int max_y = -1;
    for (int y = 0; y < mask.height; ++y) {
        for (int x = 0; x < mask.width; ++x) {
            const std::size_t index = static_cast<std::size_t>((y * mask.width + x) * 4);
            if (mask.pixels[index + 3U] == 0) continue;
            min_x = std::min(min_x, x); min_y = std::min(min_y, y);
            max_x = std::max(max_x, x); max_y = std::max(max_y, y);
        }
    }
    const bool has_mask = max_x >= min_x && max_y >= min_y;
    const float center_x = has_mask ? static_cast<float>(min_x + max_x) * 0.5f : 0.0f;
    const float center_y = has_mask ? static_cast<float>(min_y + max_y) * 0.5f : 0.0f;
    const float radius_x = has_mask
        ? std::max(3.5f, static_cast<float>(max_x - min_x + 1) * 0.62f) : 1.0f;
    const float radius_y = has_mask
        ? std::max(3.5f, static_cast<float>(max_y - min_y + 1) * 0.70f) : 1.0f;
    AttendRgbaImage out;
    out.width = mask.width;
    out.height = mask.height;
    out.pixels.resize(static_cast<std::size_t>(out.width * out.height * 4));
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            const int bx = std::clamp(static_cast<int>(
                static_cast<float>(x) / static_cast<float>(std::max(1, out.width - 1)) *
                static_cast<float>(base.width - 1) + 0.5f), 0, base.width - 1);
            const int by = std::clamp(static_cast<int>(
                static_cast<float>(y) / static_cast<float>(std::max(1, out.height - 1)) *
                static_cast<float>(base.height - 1) + 0.5f), 0, base.height - 1);
            const std::size_t dst = static_cast<std::size_t>((y * out.width + x) * 4);
            const std::size_t src = static_cast<std::size_t>((by * base.width + bx) * 4);
            const float dx = (static_cast<float>(x) - center_x) / radius_x;
            const float dy = (static_cast<float>(y) - center_y) / radius_y;
            const bool pupil = has_mask && dx * dx + dy * dy <= 1.0f;
            out.pixels[dst] = pupil ? 22 : base.pixels[src];
            out.pixels[dst + 1U] = pupil ? 14 : base.pixels[src + 1U];
            out.pixels[dst + 2U] = pupil ? 9 : base.pixels[src + 2U];
            out.pixels[dst + 3U] = 255;
        }
    }
    return out;
}

std::string defaultAttendPokemonForm(const AttendPokemonModel& model) {
    if (!model.default_form_variant.empty()) return model.default_form_variant;
    return model.form_variants.empty() ? std::string{} : model.form_variants.front().id;
}

bool attendPrimitiveVisibleForDefaultForm(
    const AttendPokemonModel& model,
    const AttendPokemonPrimitive& primitive,
    const std::string& requested_form) {
    if (!shouldRenderAttendPokemonPrimitive(model, primitive)) return false;
    if (primitive.visible_for_forms.empty()) return true;
    const std::string form = requested_form.empty()
        ? defaultAttendPokemonForm(model) : requested_form;
    return !form.empty() && std::find(
        primitive.visible_for_forms.begin(), primitive.visible_for_forms.end(), form) !=
        primitive.visible_for_forms.end();
}

int attendMaterialForDefaultPresentation(
    const AttendPokemonModel& model,
    int material_index,
    const std::string& requested_form) {
    if (material_index < 0 || material_index >= static_cast<int>(model.materials.size())) {
        return material_index;
    }
    const AttendPokemonMaterial& material =
        model.materials[static_cast<std::size_t>(material_index)];
    const std::string form = requested_form.empty()
        ? defaultAttendPokemonForm(model) : requested_form;
    for (const auto& [id, candidate] : material.form_material_indices) {
        if (id == form && candidate >= 0 && candidate < static_cast<int>(model.materials.size())) {
            material_index = candidate;
            break;
        }
    }
    if (model.default_texture_variant == "shiny") {
        const AttendPokemonMaterial& selected =
            model.materials[static_cast<std::size_t>(material_index)];
        if (selected.shiny_material_index >= 0 &&
            selected.shiny_material_index < static_cast<int>(model.materials.size())) {
            material_index = selected.shiny_material_index;
        }
    }
    return material_index;
}

std::pair<float, float> attendPokemonUvForDefaultExpression(
    const AttendPokemonVertex& vertex,
    const AttendPokemonMaterial* material) {
    if (!material || !material->eye_sheet.enabled ||
        (material->material_role != AttendMaterialRole::EyeSclera &&
         material->material_role != AttendMaterialRole::Mouth)) {
        return {vertex.u, vertex.v};
    }
    const AttendEyeSheet& sheet = material->eye_sheet;
    const int frame = std::clamp(
        sheet.default_frame, 0,
        std::max(0, static_cast<int>(sheet.frame_offsets.size()) - 1));
    const float offset_x = sheet.frame_offsets.empty()
        ? 0.0f : sheet.frame_offsets[static_cast<std::size_t>(frame)][0];
    const float offset_y = sheet.frame_offsets.empty()
        ? 0.0f : sheet.frame_offsets[static_cast<std::size_t>(frame)][1];
    return {vertex.u * std::abs(sheet.scale_x) + offset_x, vertex.v + offset_y};
}

} // namespace pr::gameplay::attend::rendering
