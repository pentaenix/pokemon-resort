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
namespace {

std::string selectedForm(
    const gameplay::attend::rendering::AttendPokemonModel& model,
    const std::string& form) {
    if (!form.empty()) return form;
    if (!model.default_form_variant.empty()) return model.default_form_variant;
    return model.form_variants.empty() ? std::string{} : model.form_variants.front().id;
}

void includePose(
    const gameplay::attend::rendering::AttendPokemonModel& model,
    const std::string& form,
    const gameplay::attend::rendering::AttendPokemonAnimation* animation,
    double time_seconds,
    AquariumPokemonMetrics& out) {
    namespace attend = gameplay::attend::rendering;
    const auto globals = attend::buildAttendPokemonGlobals(model, animation, time_seconds);
    const auto skins = attend::buildAttendPokemonSkinMatrices(model, globals);
    for (const attend::AttendPokemonPrimitive& primitive : model.primitives) {
        if (!attend::shouldRenderAttendPokemonPrimitive(model, primitive)) continue;
        if (!primitive.visible_for_forms.empty() &&
            std::find(primitive.visible_for_forms.begin(), primitive.visible_for_forms.end(), form) ==
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
}

AquariumPokemonMetrics emptyMetrics() {
    AquariumPokemonMetrics out;
    out.min_x = out.min_y = out.min_z = std::numeric_limits<float>::max();
    out.max_x = out.max_y = out.max_z = std::numeric_limits<float>::lowest();
    return out;
}

} // namespace

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

    out = emptyMetrics();
    includePose(model, selectedForm(model, form), nullptr, 0.0, out);
    if (!out.valid && error) *error = "Attend Pokemon contains no visible geometry";
    if (out.valid) {
        const std::lock_guard<std::mutex> lock(cache_mutex);
        cache[cache_key] = out;
        if (error) error->clear();
    }
    return out;
}

AquariumPokemonMetrics measureAquariumPokemonAnimationEnvelope(
    const std::string& model_path,
    const std::string& form,
    const std::vector<std::string>& animation_names,
    int samples_per_animation,
    int* sampled_poses,
    std::string* error) {
    namespace attend = gameplay::attend::rendering;
    if (sampled_poses) *sampled_poses = 0;
    const auto model_ptr = attend::loadAttendPokemonModelShared(model_path, error);
    AquariumPokemonMetrics out = emptyMetrics();
    if (!model_ptr || !model_ptr->valid) return out;
    const attend::AttendPokemonModel& model = *model_ptr;
    const std::string selected_form = selectedForm(model, form);
    std::vector<const attend::AttendPokemonAnimation*> animations;
    for (const std::string& name : animation_names) {
        const auto* animation = attend::findAttendPokemonAnimation(model, name);
        if (animation && std::find(animations.begin(), animations.end(), animation) == animations.end()) {
            animations.push_back(animation);
        }
    }
    if (animations.empty()) animations.push_back(nullptr);
    const int sample_count = std::clamp(samples_per_animation, 2, 60);
    for (const auto* animation : animations) {
        const float duration = animation ? std::max(0.0f, animation->duration_seconds) : 0.0f;
        const int poses = duration > 0.0f ? sample_count : 1;
        for (int sample = 0; sample < poses; ++sample) {
            const double time = poses > 1
                ? static_cast<double>(duration) * static_cast<double>(sample) /
                    static_cast<double>(poses)
                : 0.0;
            includePose(model, selected_form, animation, time, out);
            if (sampled_poses) ++*sampled_poses;
        }
    }
    if (!out.valid && error) *error = "Attend Pokemon contains no visible animated geometry";
    else if (error) error->clear();
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

AquariumPokemonMetrics orientAquariumPokemonMetrics(
    const AquariumPokemonMetrics& metrics,
    float pitch_degrees,
    float yaw_degrees) {
    if (!metrics.valid) return metrics;
    constexpr float kPi = 3.14159265358979323846f;
    const float pitch = pitch_degrees * kPi / 180.0f;
    const float yaw = yaw_degrees * kPi / 180.0f;
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    AquariumPokemonMetrics out = emptyMetrics();
    for (const float x : {metrics.min_x, metrics.max_x}) {
        for (const float y : {metrics.min_y, metrics.max_y}) {
            for (const float z : {metrics.min_z, metrics.max_z}) {
                const float pitched_y = cp * y - sp * z;
                const float pitched_z = sp * y + cp * z;
                const float rotated_x = cy * x + sy * pitched_z;
                const float rotated_z = -sy * x + cy * pitched_z;
                out.min_x = std::min(out.min_x, rotated_x);
                out.max_x = std::max(out.max_x, rotated_x);
                out.min_y = std::min(out.min_y, pitched_y);
                out.max_y = std::max(out.max_y, pitched_y);
                out.min_z = std::min(out.min_z, rotated_z);
                out.max_z = std::max(out.max_z, rotated_z);
                out.valid = true;
            }
        }
    }
    return out;
}

} // namespace pr::gameplay::world3d::aquarium
