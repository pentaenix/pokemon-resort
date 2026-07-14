#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"

#include <optional>
#include <string>
#include <vector>

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
    std::string pokemon_size = "small";
    std::string species_name;
    std::vector<std::string> pokemon_types;
    std::vector<std::string> dialogue_lines;
    std::string npc_interaction_mode = "direct_dialogue";
    std::string movement_speed_profile = "walk";
    std::optional<CharacterPackagePartnerPokemon> partner_pokemon;
};

struct CharacterAppearanceSelection {
    std::string form_id = "default";
    bool shiny = false;
};

/// Unified scene loader.
/// - Preferred: `.owmap` (binary terrain + JSON metadata)
/// - Legacy compatibility: `.map.json`
SceneConfig loadSceneConfig(const std::string& project_root, const std::string& scene_json_path);
CharacterSpriteDefinition loadCharacterDefinition(
    const std::string& project_root,
    const std::string& character_package_path,
    const CharacterAppearanceSelection& appearance = {});
CharacterPackageMetadata loadCharacterPackageMetadata(const std::string& character_package_path);

// Returns nullopt and logs when the package is missing or invalid (no throw).
std::optional<CharacterSpriteDefinition> tryLoadCharacterDefinition(
    const std::string& project_root,
    const std::string& character_package_path,
    const CharacterAppearanceSelection& appearance = {});

} // namespace pr::gameplay::world3d::data
