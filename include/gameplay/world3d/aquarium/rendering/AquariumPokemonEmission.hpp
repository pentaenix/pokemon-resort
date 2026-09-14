#pragma once

#include "gameplay/world3d/aquarium/AquariumConfig.hpp"

#include <string_view>
#include <array>
#include <algorithm>
#include <cmath>

namespace pr::gameplay::world3d::aquarium::rendering {

enum class AquariumPokemonEmission { None, Bulb, GlowShell };

// Off 3s, outward-on 1.2s, lit 2s, outward-off 1.2s.
inline std::array<float,4> aquariumEmissionPulse(double seconds) {
    const double phase=std::fmod(std::max(0.0,seconds),7.4);
    if(phase<3.0) return {-0.1f,0,0,0};
    if(phase<4.2) return {float((phase-3.0)/1.2)*1.2f-.1f,0,0,0};
    if(phase<6.2) return {1.1f,0,0,0};
    return {float((phase-6.2)/1.2)*1.2f-.1f,1,0,0};
}

// These legacy exports omit the bulb/shell material semantics. Scope the
// compatibility rule to their model IDs: other species reuse these material
// names, and RAE eye emissive textures are masks, not actual light sources.
inline AquariumPokemonEmission aquariumPokemonEmission(
    std::string_view model_path, std::string_view material_name) {
    const auto slash = model_path.find_last_of("/\\");
    const auto file = slash == std::string_view::npos ? model_path : model_path.substr(slash + 1);
    if (file.substr(0, 7) == "pm0382_") {
        // Kyogre's black-backed red line overlays, not the body or EyeInc.
        if (material_name == "BodyANeolant_Inc" || material_name == "BodyBNeolant_Inc")
            return AquariumPokemonEmission::GlowShell;
        return AquariumPokemonEmission::None;
    }
    if (file.substr(0, 7) != "pm0170_" && file.substr(0, 7) != "pm0171_")
        return AquariumPokemonEmission::None;
    if (material_name == "BodyNeolantInc") return AquariumPokemonEmission::Bulb;
    if (material_name == "BodyChonchieNon" || material_name == "BodyChoncheNon")
        return AquariumPokemonEmission::GlowShell;
    return AquariumPokemonEmission::None;
}

inline AquariumPokemonPresentationConfig aquariumEmissionPresentation(
    const AquariumPokemonPresentationConfig& scene, AquariumPokemonEmission emission) {
    if (emission == AquariumPokemonEmission::None) return scene;
    auto out = scene;
    out.brightness = emission == AquariumPokemonEmission::GlowShell
        ? scene.emission.halo_brightness : scene.emission.bulb_brightness;
    out.pokemon_brightness = out.saturation = out.contrast = 1.0f;
    out.ambient = 1.0f;
    out.directional = out.form_shadow = 0.0f;
    out.tint = {1.0f, 1.0f, 1.0f};
    return out;
}

} // namespace pr::gameplay::world3d::aquarium::rendering
