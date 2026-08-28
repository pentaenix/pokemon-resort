#include "gameplay/world3d/aquarium/AquariumPokemonMetrics.hpp"

#include "gameplay/attend/rendering/AttendPokemonMaterialPolicy.hpp"
#include "gameplay/attend/rendering/AttendPokemonModel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <mutex>
#include <unordered_map>

namespace pr::gameplay::world3d::aquarium {

AquariumPokemonMetrics measureAquariumPokemon(
    const std::string& model_path,
    const std::string& form,
    std::string* error) {
    namespace attend = gameplay::attend::rendering;
    std::error_code time_error;
    const auto write_time = std::filesystem::last_write_time(model_path, time_error);
    const std::string cache_key = model_path + '\n' + form + '\n' +
        (time_error ? std::string{"unknown"}
                    : std::to_string(static_cast<long long>(
                        write_time.time_since_epoch().count())));
    static std::mutex cache_mutex;
    static std::unordered_map<std::string, AquariumPokemonMetrics> cache;
    {
        const std::lock_guard<std::mutex> lock(cache_mutex);
        const auto found = cache.find(cache_key);
        if (found != cache.end()) {
            if (error) error->clear();
            return found->second;
        }
    }

    const auto model_ptr = attend::loadAttendPokemonModelShared(model_path, error);
    AquariumPokemonMetrics out;
    if (!model_ptr || !model_ptr->valid) return out;
    const attend::AttendPokemonModel& model = *model_ptr;

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
    if (out.valid) {
        const std::lock_guard<std::mutex> lock(cache_mutex);
        cache[cache_key] = out;
        if (error) error->clear();
    }
    return out;
}

AquariumPokemonMetrics rotateAquariumPokemonMetrics(
    const AquariumPokemonMetrics& metrics,
    float pitch_degrees) {
    if (!metrics.valid) return metrics;
    constexpr float kPi = 3.14159265358979323846f;
    const float pitch = pitch_degrees * kPi / 180.0f;
    const float c = std::cos(pitch);
    const float s = std::sin(pitch);
    AquariumPokemonMetrics out;
    out.min_x = out.min_y = out.min_z = std::numeric_limits<float>::max();
    out.max_x = out.max_y = out.max_z = std::numeric_limits<float>::lowest();
    for (const float x : {metrics.min_x, metrics.max_x}) {
        for (const float y : {metrics.min_y, metrics.max_y}) {
            for (const float z : {metrics.min_z, metrics.max_z}) {
                const std::array<float, 3> rotated{x, c * y - s * z, s * y + c * z};
                out.min_x = std::min(out.min_x, rotated[0]);
                out.max_x = std::max(out.max_x, rotated[0]);
                out.min_y = std::min(out.min_y, rotated[1]);
                out.max_y = std::max(out.max_y, rotated[1]);
                out.min_z = std::min(out.min_z, rotated[2]);
                out.max_z = std::max(out.max_z, rotated[2]);
            }
        }
    }
    out.valid = true;
    return out;
}

} // namespace pr::gameplay::world3d::aquarium
