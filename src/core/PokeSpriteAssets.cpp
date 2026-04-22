#include "core/PokeSpriteAssets.hpp"

#include "core/Json.hpp"

#include <SDL_image.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <map>
#include <regex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fs = std::filesystem;

namespace pr {

namespace {

std::string asciiLowerCopy(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

std::string normalizeToken(const std::string& raw) {
    if (raw.empty()) {
        return {};
    }
    if (raw == "$") {
        return "$";
    }
    if (raw == "!") {
        return "exclamation";
    }
    if (raw == "?") {
        return "question";
    }

    std::string out;
    out.reserve(raw.size());
    bool last_dash = false;
    for (const unsigned char c : raw) {
        char ch = static_cast<char>(std::tolower(c));
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            out.push_back(ch);
            last_dash = false;
        } else if (ch == '-' || ch == '_' || std::isspace(c)) {
            if (!last_dash && !out.empty()) {
                out.push_back('-');
                last_dash = true;
            }
        }
    }

    while (!out.empty() && out.back() == '-') {
        out.pop_back();
    }
    return out;
}

std::string stripKnownFormSuffixes(std::string key) {
    // Keep this in sync with bridge-side normalization; we defensively strip a few common suffixes
    // so outdated bridge binaries still resolve seasonal/sea/etc. forms against pokemon.json keys.
    static const std::string kSuffixes[] = {
        "-forme",
        "-form",
        "-mode",
        "-style",
        "-pattern",
        "-trim",
        "-cloak",
        "-flower",
        "-plumage",
        "-size",
        "-coat",
        "-sea",
        "-season",
    };
    for (const auto& suf : kSuffixes) {
        if (key.size() > suf.size() &&
            key.compare(key.size() - suf.size(), suf.size(), suf) == 0) {
            key.resize(key.size() - suf.size());
        }
    }
    return key;
}

std::string normalizeSpeciesSlug(const std::string& raw) {
    return normalizeToken(raw);
}

TextureHandle loadTextureOptional(SDL_Renderer* renderer, const fs::path& path) {
    TextureHandle texture;
    if (!renderer || !fs::exists(path)) {
        return texture;
    }
    SDL_Texture* raw = IMG_LoadTexture(renderer, path.string().c_str());
    if (!raw) {
        std::cerr << "Warning: failed to load texture: " << path.string() << " | " << IMG_GetError() << '\n';
        return texture;
    }
    texture.texture.reset(raw, SDL_DestroyTexture);
    if (SDL_QueryTexture(texture.texture.get(), nullptr, nullptr, &texture.width, &texture.height) != 0) {
        texture.texture.reset();
        texture.width = 0;
        texture.height = 0;
        return texture;
    }
    SDL_SetTextureBlendMode(texture.texture.get(), SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(texture.texture.get(), SDL_ScaleModeNearest);
#endif
    return texture;
}

} // namespace

class PokeSpriteAssets::Impl {
public:
    explicit Impl(std::string project_root)
        : project_root_(std::move(project_root)),
          root_(fs::path(project_root_) / "assets" / "pokesprite") {
        loadMetadata();
    }

    const std::string& pokemonStyleDirectory() const {
        return pokemon_style_directory_;
    }

    ResolvedPokemonSprite resolvePokemon(const PokemonSpriteRequest& request) const {
        ResolvedPokemonSprite resolved;

        const SpeciesMetadata* species = findSpecies(request.species_id);
        std::string slug = normalizeSpeciesSlug(request.species_slug);
        if (slug.empty() && species) {
            slug = species->slug;
        }
        if (slug.empty()) {
            resolved.cache_key = "pokemon|unknown";
            resolved.relative_path = (fs::path(pokemon_style_directory_) / "unknown.png").generic_string();
            resolved.used_fallback = true;
            return resolved;
        }

        resolved.species_slug = slug;
        std::string form_key = stripKnownFormSuffixes(normalizeToken(request.form_key));
        if (form_key.empty()) {
            form_key = "$";
        }

        bool form_has_female = false;
        if (species) {
            const FormMetadata* form = species->findForm(form_key);
            if (!form && form_key != "$") {
                form = species->findForm("$");
                form_key = "$";
            }
            if (form) {
                form_key = form->canonical_key;
                form_has_female = form->has_female;
            }
        }
        resolved.resolved_form_key = form_key;

        // Debug: help diagnose form mismatches for common multi-form species.
        if (request.species_id == 412 || request.species_id == 413 || // burmy/wormadam
            request.species_id == 422 || request.species_id == 423 || // shellos/gastrodon
            request.species_id == 585 || request.species_id == 586) { // deerling/sawsbuck
            static std::unordered_set<std::string> warned;
            const std::string warn_key =
                std::to_string(request.species_id) + "|" + slug + "|" + request.form_key + "|" + form_key;
            if (warned.insert(warn_key).second) {
                std::cerr << "[PokeSpriteAssets] form_debug species_id=" << request.species_id
                          << " slug=" << slug
                          << " request_form_key=\"" << request.form_key << "\""
                          << " normalized_form_key=\"" << stripKnownFormSuffixes(normalizeToken(request.form_key)) << "\""
                          << " resolved_form_key=\"" << form_key << "\""
                          << " gender=" << request.gender
                          << " shiny=" << (request.is_shiny ? "true" : "false")
                          << "\n";
            }
        }

        std::vector<std::pair<std::string, bool>> candidates;
        auto push_candidate = [&](const std::string& stem, bool female_variant) {
            if (stem.empty()) {
                return;
            }
            const auto it = std::find_if(
                candidates.begin(),
                candidates.end(),
                [&](const std::pair<std::string, bool>& candidate) { return candidate.first == stem; });
            if (it == candidates.end()) {
                candidates.emplace_back(stem, female_variant);
            }
        };

        const std::string base = form_key == "$" ? slug : (slug + "-" + form_key);
        const bool female = request.gender == 1;
        if (female && form_has_female) {
            push_candidate(base + "-f", true);
        }
        push_candidate(base, false);
        if (female && species && species->default_has_female && form_key != "$") {
            push_candidate(slug + "-f", true);
        }
        push_candidate(slug, false);

        for (const auto& candidate : candidates) {
            const fs::path base_dir =
                fs::path(pokemon_style_directory_) / (request.is_shiny ? "shiny" : "regular");

            // New sprite pack layout: gender variants live under `regular/female/` and `shiny/female/`.
            // Only attempt these when the metadata indicates a female variant exists.
            if (female && form_has_female) {
                const fs::path female_relative = base_dir / "female" / (candidate.first + ".png");
                if (fs::exists(root_ / female_relative)) {
                    resolved.cache_key =
                        std::string("pokemon|") + (request.is_shiny ? "shiny|" : "regular|") + "female|" + candidate.first;
                    resolved.relative_path = female_relative.generic_string();
                    resolved.used_female_variant = true;
                    return resolved;
                }
            }

            const fs::path relative = base_dir / (candidate.first + ".png");
            if (fs::exists(root_ / relative)) {
                resolved.cache_key =
                    std::string("pokemon|") + (request.is_shiny ? "shiny|" : "regular|") + candidate.first;
                resolved.relative_path = relative.generic_string();
                resolved.used_female_variant = candidate.second;
                return resolved;
            }
        }

        const fs::path fallback_relative = fs::path(pokemon_style_directory_) / "unknown.png";
        resolved.cache_key =
            std::string("pokemon|fallback|") + (request.is_shiny ? "shiny|" : "regular|") + slug + "|" + form_key;
        resolved.relative_path = fallback_relative.generic_string();
        resolved.used_fallback = true;
        return resolved;
    }

    ResolvedItemIcon resolveItemIcon(int item_id, ItemIconUsage usage) const {
        ResolvedItemIcon resolved;
        const std::string item_key = itemKey(item_id);
        resolved.item_key = item_key;

        std::string mapped = {};
        auto it = item_icon_paths_.find(item_key);
        if (it != item_icon_paths_.end()) {
            mapped = it->second;
        }
        if (!mapped.empty() && usage == ItemIconUsage::Bag) {
            mapped = replaceSuffix(mapped, "--held", "--bag");
        }

        fs::path relative;
        if (!mapped.empty()) {
            relative = fs::path("items") / (mapped + ".png");
            if (fs::exists(root_ / relative)) {
                resolved.cache_key = "item|" + mapped;
                resolved.relative_path = relative.generic_string();
                return resolved;
            }
        }

        relative = fs::path("items") / "etc" / "unknown-item.png";
        resolved.cache_key = "item|unknown|" + item_key;
        resolved.relative_path = relative.generic_string();
        resolved.used_fallback = true;
        return resolved;
    }

    TextureHandle loadPokemonTexture(SDL_Renderer* renderer, const PokemonSpriteRequest& request) const {
        return loadTexture(renderer, resolvePokemon(request));
    }

    TextureHandle loadItemTexture(SDL_Renderer* renderer, int item_id, ItemIconUsage usage) const {
        return loadTexture(renderer, resolveItemIcon(item_id, usage));
    }

private:
    struct FormMetadata {
        std::string canonical_key;
        bool has_female = false;
    };

    struct SpeciesMetadata {
        std::string slug;
        std::map<std::string, FormMetadata> forms;
        bool default_has_female = false;

        const FormMetadata* findForm(const std::string& key) const {
            auto it = forms.find(key);
            return it == forms.end() ? nullptr : &it->second;
        }
    };

    static std::string itemKey(int item_id) {
        if (item_id < 0) {
            return "item_unknown";
        }
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "item_%04d", item_id);
        return buffer;
    }

    static std::string replaceSuffix(std::string value, const std::string& from, const std::string& to) {
        if (value.size() >= from.size() &&
            value.compare(value.size() - from.size(), from.size(), from) == 0) {
            value.replace(value.size() - from.size(), from.size(), to);
        }
        return value;
    }

    const SpeciesMetadata* findSpecies(int species_id) const {
        auto it = species_.find(species_id);
        return it == species_.end() ? nullptr : &it->second;
    }

    void loadMetadata() {
        discoverPokemonStyleDirectory();

        const fs::path pokemon_metadata_path = root_ / "data" / "pokemon.json";
        if (fs::exists(pokemon_metadata_path)) {
            const JsonValue pokemon_metadata = parseJsonFile(pokemon_metadata_path.string());
            if (pokemon_metadata.isObject()) {
                for (const auto& [species_key, value] : pokemon_metadata.asObject()) {
                    if (!value.isObject()) {
                        continue;
                    }
                    SpeciesMetadata species;
                    if (const JsonValue* slug_value = value.get("slug"); slug_value && slug_value->isObject()) {
                        if (const JsonValue* eng_slug = slug_value->get("eng"); eng_slug && eng_slug->isString()) {
                            species.slug = normalizeSpeciesSlug(eng_slug->asString());
                        }
                    }
                    const JsonValue* forms_value = findFormsObject(value);
                    if (forms_value && forms_value->isObject()) {
                        for (const auto& [form_key_raw, form_value] : forms_value->asObject()) {
                            FormMetadata form;
                            form.canonical_key = normalizeToken(form_key_raw);
                            if (form.canonical_key.empty()) {
                                form.canonical_key = "$";
                            }
                            if (form_value.isObject()) {
                                form.has_female =
                                    form_value.get("has_female") && form_value.get("has_female")->isBool()
                                        ? form_value.get("has_female")->asBool()
                                        : false;
                                if (const JsonValue* alias_value = form_value.get("is_alias_of");
                                    alias_value && alias_value->isString()) {
                                    const std::string alias = normalizeToken(alias_value->asString());
                                    if (!alias.empty()) {
                                        form.canonical_key = alias;
                                    }
                                }
                            }
                            if (form_key_raw == "$") {
                                species.default_has_female = form.has_female;
                            }
                            species.forms.emplace(normalizeToken(form_key_raw).empty() ? "$" : normalizeToken(form_key_raw), form);
                        }
                    } else {
                        species.forms.emplace("$", FormMetadata{"$", false});
                    }

                    const int species_id = std::stoi(species_key);
                    species_.emplace(species_id, std::move(species));
                }
            }
        }

        const fs::path item_map_path = root_ / "data" / "item-map.json";
        if (fs::exists(item_map_path)) {
            const JsonValue item_map = parseJsonFile(item_map_path.string());
            if (item_map.isObject()) {
                for (const auto& [item_key, value] : item_map.asObject()) {
                    if (value.isString()) {
                        item_icon_paths_.emplace(item_key, value.asString());
                    }
                }
            }
        }
    }

    void discoverPokemonStyleDirectory() {
        int best_generation = -1;
        std::string best_directory = "pokemon-gen8";
        static const std::regex kStylePattern("^pokemon-gen([0-9]+)$");
        for (const auto& entry : fs::directory_iterator(root_)) {
            if (!entry.is_directory()) {
                continue;
            }
            const std::string name = entry.path().filename().string();
            std::smatch match;
            if (!std::regex_match(name, match, kStylePattern)) {
                continue;
            }
            const int generation = std::stoi(match[1].str());
            if (generation > best_generation) {
                best_generation = generation;
                best_directory = name;
            }
        }
        pokemon_style_directory_ = best_directory;
    }

    const JsonValue* findFormsObject(const JsonValue& species_object) const {
        if (!species_object.isObject()) {
            return nullptr;
        }
        if (const JsonValue* preferred = species_object.get(pokemonGenerationKey()); preferred && preferred->isObject()) {
            if (const JsonValue* forms = preferred->get("forms"); forms && forms->isObject()) {
                return forms;
            }
        }
        for (const auto& [key, value] : species_object.asObject()) {
            if (key.rfind("gen-", 0) != 0 || !value.isObject()) {
                continue;
            }
            if (const JsonValue* forms = value.get("forms"); forms && forms->isObject()) {
                return forms;
            }
        }
        return nullptr;
    }

    std::string pokemonGenerationKey() const {
        if (pokemon_style_directory_.rfind("pokemon-gen", 0) == 0) {
            return "gen-" + pokemon_style_directory_.substr(std::string("pokemon-gen").size());
        }
        return "gen-8";
    }

    template <typename TResolved>
    TextureHandle loadTexture(SDL_Renderer* renderer, const TResolved& resolved) const {
        auto it = texture_cache_.find(resolved.cache_key);
        if (it != texture_cache_.end()) {
            return it->second;
        }
        TextureHandle texture = loadTextureOptional(renderer, root_ / resolved.relative_path);
        texture_cache_.emplace(resolved.cache_key, texture);
        return texture;
    }

    std::string project_root_;
    fs::path root_;
    std::string pokemon_style_directory_ = "pokemon-gen8";
    std::unordered_map<int, SpeciesMetadata> species_{};
    std::unordered_map<std::string, std::string> item_icon_paths_{};
    mutable std::unordered_map<std::string, TextureHandle> texture_cache_{};
};

std::shared_ptr<PokeSpriteAssets> PokeSpriteAssets::create(const std::string& project_root) {
    return std::shared_ptr<PokeSpriteAssets>(new PokeSpriteAssets(project_root));
}

PokeSpriteAssets::PokeSpriteAssets(std::string project_root)
    : impl_(std::make_shared<Impl>(std::move(project_root))) {}

const std::string& PokeSpriteAssets::pokemonStyleDirectory() const {
    return impl_->pokemonStyleDirectory();
}

ResolvedPokemonSprite PokeSpriteAssets::resolvePokemon(const PokemonSpriteRequest& request) const {
    return impl_->resolvePokemon(request);
}

ResolvedPokemonSprite PokeSpriteAssets::resolvePokemon(const PcSlotSpecies& slot) const {
    PokemonSpriteRequest request;
    request.species_id = slot.species_id;
    request.species_slug = slot.slug;
    request.form_key = slot.form_key;
    request.gender = slot.gender;
    request.is_shiny = slot.is_shiny;
    return resolvePokemon(request);
}

ResolvedItemIcon PokeSpriteAssets::resolveItemIcon(int item_id, ItemIconUsage usage) const {
    return impl_->resolveItemIcon(item_id, usage);
}

TextureHandle PokeSpriteAssets::loadPokemonTexture(SDL_Renderer* renderer, const PokemonSpriteRequest& request) const {
    return impl_->loadPokemonTexture(renderer, request);
}

TextureHandle PokeSpriteAssets::loadPokemonTexture(SDL_Renderer* renderer, const PcSlotSpecies& slot) const {
    return impl_->loadPokemonTexture(
        renderer,
        PokemonSpriteRequest{slot.species_id, slot.slug, slot.form_key, slot.gender, slot.is_shiny});
}

TextureHandle PokeSpriteAssets::loadItemTexture(SDL_Renderer* renderer, int item_id, ItemIconUsage usage) const {
    return impl_->loadItemTexture(renderer, item_id, usage);
}

} // namespace pr
