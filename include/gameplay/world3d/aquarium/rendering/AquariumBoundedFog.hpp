#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace pr::gameplay::world3d::aquarium::rendering {

struct FogRayBoxHit {
    bool hit = false;
    float near_distance = 0.0f;
    float far_distance = 0.0f;
};

inline float aquariumFogVisibilityWorld(int murkiness_level) {
    // One grid cell is 16 world units / one metre. The perceptual steps are
    // deliberately non-linear so the final ticks can create genuinely dense
    // water without compressing the useful clear-water range.
    // Original levels 0..5 resampled over nine controls. The last point stays
    // at the former level-5 visibility instead of entering the over-dense
    // former 6..8 range.
    constexpr std::array<float, 9> visibility_metres{
        20.0f, 16.25f, 13.0f, 10.5f, 8.5f,
        6.75f, 5.5f, 4.4375f, 3.5f};
    murkiness_level = std::clamp(murkiness_level, 0, 8);
    return visibility_metres[static_cast<std::size_t>(murkiness_level)] * 16.0f;
}

inline float aquariumFogMaximumOpacity(int murkiness_level) {
    constexpr std::array<float, 9> opacity{
        0.12f, 0.1825f, 0.25f, 0.325f, 0.41f,
        0.4975f, 0.585f, 0.66875f, 0.75f};
    murkiness_level = std::clamp(murkiness_level, 0, 8);
    return opacity[static_cast<std::size_t>(murkiness_level)];
}

inline float aquariumFogOpticalDistance(
    float water_path_world,
    float clear_distance_world,
    float visibility_distance_world) {
    const float span = std::max(
        visibility_distance_world - clear_distance_world, 1.0e-6f);
    return std::max(
        0.0f, (water_path_world - clear_distance_world) / span);
}

inline float aquariumFogCoverage(
    float water_path_world,
    float clear_distance_world,
    float visibility_distance_world,
    float falloff_gamma,
    float maximum_opacity) {
    // Do not clamp at the authored visibility distance. Exponential
    // extinction reaches 63% there, then continues changing asymptotically so
    // long tanks retain a readable near-to-far gradient.
    const float optical_distance = aquariumFogOpticalDistance(
        water_path_world, clear_distance_world, visibility_distance_world);
    const float extinction = 1.0f - std::exp(-std::pow(
        optical_distance, std::max(falloff_gamma, 0.05f)));
    return std::clamp(
        std::clamp(maximum_opacity, 0.0f, 1.0f) * extinction,
        0.0f,
        1.0f);
}

inline float aquariumFogPathTransmittance(
    float water_path_world,
    float clear_distance_world,
    float visibility_distance_world) {
    const float optical_distance = aquariumFogOpticalDistance(
        water_path_world, clear_distance_world, visibility_distance_world);
    // Use a gentle exponential rather than a hard transmission floor. Nearby
    // substrate stays readable, but genuinely deep sight lines can continue
    // getting darker instead of flattening at an arbitrary clamp.
    constexpr float kAbsorptionStrength = 0.22f;
    return std::exp(-optical_distance * kAbsorptionStrength);
}

inline float aquariumFogCompositeOpacity(
    float fog_coverage,
    float path_transmittance) {
    // One premultiplied-alpha draw represents both colored haze and absorbed
    // light. Repeating this operator composes multiple tanks instead of
    // replacing the result of the previous tank.
    return std::clamp(
        1.0f - std::clamp(path_transmittance, 0.0f, 1.0f) *
            (1.0f - std::clamp(fog_coverage, 0.0f, 1.0f)),
        0.0f,
        1.0f);
}

inline FogRayBoxHit intersectFogRayBox(
    const std::array<float, 3>& origin,
    const std::array<float, 3>& direction,
    const std::array<float, 3>& box_min,
    const std::array<float, 3>& box_max) {
    constexpr float kParallelEpsilon = 1.0e-7f;
    float near_distance = -std::numeric_limits<float>::infinity();
    float far_distance = std::numeric_limits<float>::infinity();
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < kParallelEpsilon) {
            if (origin[axis] < box_min[axis] || origin[axis] > box_max[axis]) {
                return {};
            }
            continue;
        }
        float a = (box_min[axis] - origin[axis]) / direction[axis];
        float b = (box_max[axis] - origin[axis]) / direction[axis];
        if (a > b) std::swap(a, b);
        near_distance = std::max(near_distance, a);
        far_distance = std::min(far_distance, b);
        if (near_distance > far_distance) return {};
    }
    return {true, near_distance, far_distance};
}

inline float aquariumFogWaterPath(
    const FogRayBoxHit& hit,
    float visible_surface_distance) {
    if (!hit.hit) return 0.0f;
    const float start = std::max(hit.near_distance, 0.0f);
    const float end = std::min(hit.far_distance, visible_surface_distance);
    return std::max(0.0f, end - start);
}

inline bool aquariumFogProxyIsVisible(
    float proxy_distance,
    float opaque_surface_distance,
    bool has_opaque_surface,
    float depth_epsilon = 0.5f) {
    return !has_opaque_surface ||
        proxy_distance <= opaque_surface_distance + std::max(0.0f, depth_epsilon);
}

} // namespace pr::gameplay::world3d::aquarium::rendering
