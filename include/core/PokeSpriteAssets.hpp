#pragma once

#include "core/Assets.hpp"
#include "core/PcSlotSpecies.hpp"

#include <SDL.h>

#include <memory>
#include <optional>
#include <string>

namespace pr {

enum class ItemIconUsage {
    Default,
    Held,
    Bag,
};

struct PokemonSpriteRequest {
    int species_id = -1;
    std::string species_slug;
    std::string form_key;
    int gender = -1;
    bool is_shiny = false;
};

struct ResolvedPokemonSprite {
    std::string cache_key;
    std::string relative_path;
    std::string species_slug;
    std::string resolved_form_key;
    bool used_female_variant = false;
    bool used_fallback = false;
};

struct ResolvedItemIcon {
    std::string cache_key;
    std::string relative_path;
    std::string item_key;
    bool used_fallback = false;
};

struct ResolvedMiscIcon {
    std::string cache_key;
    std::string relative_path;
    std::string category;
    std::string icon_key;
    bool used_fallback = false;
};

class PokeSpriteAssets {
public:
    static std::shared_ptr<PokeSpriteAssets> create(const std::string& project_root);

    const std::string& pokemonStyleDirectory() const;

    ResolvedPokemonSprite resolvePokemon(const PokemonSpriteRequest& request) const;
    ResolvedPokemonSprite resolvePokemon(const PcSlotSpecies& slot) const;
    ResolvedItemIcon resolveItemIcon(int item_id, ItemIconUsage usage = ItemIconUsage::Default) const;
    ResolvedMiscIcon resolveMiscIcon(const std::string& category, const std::string& icon_key) const;

    TextureHandle loadPokemonTexture(SDL_Renderer* renderer, const PokemonSpriteRequest& request) const;
    TextureHandle loadPokemonTexture(SDL_Renderer* renderer, const PcSlotSpecies& slot) const;
    TextureHandle loadItemTexture(SDL_Renderer* renderer, int item_id, ItemIconUsage usage = ItemIconUsage::Default) const;
    TextureHandle loadMiscTexture(SDL_Renderer* renderer, const std::string& category, const std::string& icon_key) const;

private:
    explicit PokeSpriteAssets(std::string project_root);

    class Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace pr
