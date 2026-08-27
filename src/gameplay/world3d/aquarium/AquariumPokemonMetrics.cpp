#include "gameplay/world3d/aquarium/AquariumPokemonMetrics.hpp"

#include "gameplay/attend/rendering/AttendPokemonMaterialPolicy.hpp"
#include "gameplay/attend/rendering/AttendPokemonModel.hpp"

#include <algorithm>
#include <limits>

namespace pr::gameplay::world3d::aquarium {

AquariumPokemonMetrics measureAquariumPokemon(
    const std::string& model_path,
    const std::string& form,
    std::string* error) {
    namespace attend = gameplay::attend::rendering;
    const attend::AttendPokemonModel model = attend::loadAttendPokemonModel(model_path, error);
    AquariumPokemonMetrics out;
    if (!model.valid) return out;

    const auto globals = attend::buildAttendPokemonGlobals(model, nullptr, 0.0);
    const auto skins = attend::buildAttendPokemonSkinMatrices(model, globals);
    const std::string selected_form = !form.empty()
        ? form
        : (!model.default_form_variant.empty()
            ? model.default_form_variant
            : (model.form_variants.empty() ? std::string{} : model.form_variants.front().id));
    out.min_x = out.min_y = out.min_z = std::numeric_limits<float>::max();
    out.max_x = out.max_y = out.max_z = std::numeric_limits<float>::lowest();
    for (const attend::AttendPokemonPrimitive& primitive : model.primitives) {
        if (!attend::shouldRenderAttendPokemonPrimitive(model, primitive)) continue;
        if (!primitive.visible_for_forms.empty() &&
            std::find(primitive.visible_for_forms.begin(), primitive.visible_for_forms.end(), selected_form) ==
                primitive.visible_for_forms.end()) continue;
        std::vector<attend::AttendPokemonVertex> posed;
        attend::skinAttendPokemonPrimitiveWithPose(model, primitive, globals, skins, posed);
        for (const attend::AttendPokemonVertex& vertex : posed) {
            out.min_x = std::min(out.min_x, vertex.x);
            out.max_x = std::max(out.max_x, vertex.x);
            out.min_y = std::min(out.min_y, vertex.y);
            out.max_y = std::max(out.max_y, vertex.y);
            out.min_z = std::min(out.min_z, vertex.z);
            out.max_z = std::max(out.max_z, vertex.z);
            out.valid = true;
        }
    }
    if (!out.valid && error) *error = "Attend Pokemon contains no visible geometry";
    return out;
}

} // namespace pr::gameplay::world3d::aquarium
