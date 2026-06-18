#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"

#include <optional>
#include <string>

namespace pr::gameplay::world3d::data {

struct CharacterPackagePartnerPokemon {
    std::string pokemon_id;
    std::string form_id = "default";
    std::string nickname;
    std::string relationship;
};

struct CharacterPackageMetadata {
    std::string id;
    std::string display_name;
    std::string character_type;
    std::string movement_speed_profile = "walk";
    std::optional<CharacterPackagePartnerPokemon> partner_pokemon;
};

/// Unified scene loader.
/// - Preferred: `.owmap` (binary terrain + JSON metadata)
/// - Legacy compatibility: `.map.json`
SceneConfig loadSceneConfig(const std::string& project_root, const std::string& scene_json_path);
CharacterSpriteDefinition loadCharacterDefinition(const std::string& project_root, const std::string& character_package_path);
CharacterPackageMetadata loadCharacterPackageMetadata(const std::string& character_package_path);

// Returns nullopt and logs when the package is missing or invalid (no throw).
std::optional<CharacterSpriteDefinition> tryLoadCharacterDefinition(
    const std::string& project_root,
    const std::string& character_package_path);

} // namespace pr::gameplay::world3d::data
