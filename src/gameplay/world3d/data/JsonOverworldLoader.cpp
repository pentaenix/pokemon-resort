#include "gameplay/world3d/data/JsonOverworldLoader.hpp"

#include "core/config/Json.hpp"
#include "gameplay/world3d/data/OwmapOverworldLoader.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pr::gameplay::world3d::data {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kCharbinHeaderSize = 16u;
constexpr std::size_t kCharbinMagicSize = 8u;
constexpr const char* kCharbinMagic = "SPMKCHAR";
constexpr std::uint32_t kCharbinFormatVersion = 1u;

std::uint32_t readU32Le(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

std::vector<std::uint8_t> readBinaryFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error("Could not open character package: " + path);
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size < 0) {
        throw std::runtime_error("Could not determine character package size: " + path);
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        in.read(reinterpret_cast<char*>(bytes.data()), size);
    }
    return bytes;
}

std::string strOr(const JsonValue* v, const std::string& fallback) {
    return (v && v->isString()) ? v->asString() : fallback;
}

double numOr(const JsonValue* v, double fallback) {
    return (v && v->isNumber()) ? v->asNumber() : fallback;
}

int intOr(const JsonValue* v, int fallback) {
    return (v && v->isNumber()) ? static_cast<int>(v->asNumber()) : fallback;
}

const JsonValue* profileObjectById(
    const std::string& project_root,
    const std::string& profile_id,
    JsonValue& cache_root,
    bool& cache_loaded) {
    if (!cache_loaded) {
        const std::string profile_path =
            (fs::path(project_root) / "config" / "gameplay" / "sprites" / "sprite_profiles.json").string();
        cache_root = parseJsonFile(profile_path);
        cache_loaded = true;
    }
    if (!cache_root.isObject()) return nullptr;
    const JsonValue* profiles = cache_root.get("profiles");
    if (!profiles || !profiles->isObject()) return nullptr;
    const JsonValue* profile = profiles->get(profile_id);
    if (!profile || !profile->isObject()) return nullptr;
    return profile;
}

CharacterAnimationDef parseAnimation(const JsonValue* root, const std::string& key, const CharacterAnimationDef& fallback) {
    CharacterAnimationDef out = fallback;
    if (!root || !root->isObject()) return out;

    const JsonValue* anim = root->get(key);
    if (!anim || !anim->isObject()) return out;

    if (const JsonValue* frames = anim->get("frames"); frames && frames->isArray()) {
        out.frames.clear();
        for (const JsonValue& item : frames->asArray()) {
            if (item.isNumber()) {
                out.frames.push_back(static_cast<int>(item.asNumber()));
            }
        }
        if (out.frames.empty()) {
            out.frames = fallback.frames;
        }
    }

    out.frame_time_ms = intOr(anim->get("frameTimeMs"), fallback.frame_time_ms);
    return out;
}

CharacterAnimationDef parseAnimationByName(
    const JsonValue* primary_root,
    const JsonValue* fallback_root,
    const std::string& key,
    const CharacterAnimationDef& default_fallback) {
    CharacterAnimationDef out = parseAnimation(fallback_root, key, default_fallback);
    out = parseAnimation(primary_root, key, out);
    return out;
}

std::string readLengthPrefixedString(const std::vector<std::uint8_t>& file, std::size_t& offset, const std::string& path) {
    if (offset + 4 > file.size()) {
        throw std::runtime_error("Invalid charbin asset string length header: " + path);
    }
    const std::uint32_t len = readU32Le(file.data() + offset);
    offset += 4;
    if (offset + static_cast<std::size_t>(len) > file.size()) {
        throw std::runtime_error("Invalid charbin asset string payload length: " + path);
    }
    std::string out(reinterpret_cast<const char*>(file.data() + offset), static_cast<std::size_t>(len));
    offset += static_cast<std::size_t>(len);
    return out;
}

JsonValue readCharbinJsonRoot(const std::string& character_package_path) {
    const fs::path input_path(character_package_path);
    if (input_path.extension() != ".charbin") {
        throw std::runtime_error(
            "Overworld character loader now supports only .charbin packages: " + character_package_path);
    }

    const std::vector<std::uint8_t> file = readBinaryFile(character_package_path);
    if (file.size() < kCharbinHeaderSize) {
        throw std::runtime_error("Character package too small: " + character_package_path);
    }
    if (std::memcmp(file.data(), kCharbinMagic, kCharbinMagicSize) != 0) {
        throw std::runtime_error("Character package has invalid magic: " + character_package_path);
    }
    const std::uint32_t version = readU32Le(file.data() + 8);
    if (version != kCharbinFormatVersion) {
        throw std::runtime_error("Unsupported charbin version in: " + character_package_path);
    }
    const std::uint32_t json_len = readU32Le(file.data() + 12);
    if (kCharbinHeaderSize + static_cast<std::size_t>(json_len) + 4 > file.size()) {
        throw std::runtime_error("Character package truncated at metadata: " + character_package_path);
    }

    const std::size_t json_start = kCharbinHeaderSize;
    const std::string json_blob(reinterpret_cast<const char*>(file.data() + json_start), static_cast<std::size_t>(json_len));
    JsonValue root = parseJsonText(json_blob);
    if (!root.isObject()) {
        throw std::runtime_error("Character package metadata root must be an object: " + character_package_path);
    }
    return root;
}

} // namespace

SceneConfig loadSceneConfig(const std::string& project_root, const std::string& scene_json_path) {
    if (!isOwmapFile(scene_json_path)) {
        throw std::runtime_error(
            "Overworld scene loader now supports only .owmap files: " + scene_json_path);
    }
    return loadOwmapScene(project_root, scene_json_path);
}

CharacterSpriteDefinition loadCharacterDefinition(const std::string& project_root, const std::string& character_package_path) {
    const fs::path input_path(character_package_path);
    if (input_path.extension() != ".charbin") {
        throw std::runtime_error(
            "Overworld character loader now supports only .charbin packages: " + character_package_path);
    }

    const std::vector<std::uint8_t> file = readBinaryFile(character_package_path);
    if (file.size() < kCharbinHeaderSize) {
        throw std::runtime_error("Character package too small: " + character_package_path);
    }
    if (std::memcmp(file.data(), kCharbinMagic, kCharbinMagicSize) != 0) {
        throw std::runtime_error("Character package has invalid magic: " + character_package_path);
    }
    const std::uint32_t version = readU32Le(file.data() + 8);
    if (version != kCharbinFormatVersion) {
        throw std::runtime_error("Unsupported charbin version in: " + character_package_path);
    }
    const std::uint32_t json_len = readU32Le(file.data() + 12);
    if (kCharbinHeaderSize + static_cast<std::size_t>(json_len) + 4 > file.size()) {
        throw std::runtime_error("Character package truncated at metadata: " + character_package_path);
    }

    const std::size_t json_start = kCharbinHeaderSize;
    const std::size_t json_end = json_start + static_cast<std::size_t>(json_len);
    const std::string json_blob(reinterpret_cast<const char*>(file.data() + json_start), static_cast<std::size_t>(json_len));
    const JsonValue root = parseJsonText(json_blob);
    if (!root.isObject()) {
        throw std::runtime_error("Character package metadata root must be an object: " + character_package_path);
    }

    std::size_t offset = json_end;
    const std::uint32_t asset_count = readU32Le(file.data() + offset);
    offset += 4;

    std::unordered_map<std::string, std::vector<std::uint8_t>> assets;
    assets.reserve(asset_count);
    for (std::uint32_t i = 0; i < asset_count; ++i) {
        const std::string asset_id = readLengthPrefixedString(file, offset, character_package_path);
        (void)readLengthPrefixedString(file, offset, character_package_path); // mime
        if (offset + 4 > file.size()) {
            throw std::runtime_error("Character package truncated at asset length: " + character_package_path);
        }
        const std::uint32_t blob_len = readU32Le(file.data() + offset);
        offset += 4;
        if (offset + static_cast<std::size_t>(blob_len) > file.size()) {
            throw std::runtime_error("Character package truncated at asset blob: " + character_package_path);
        }
        std::vector<std::uint8_t> blob(static_cast<std::size_t>(blob_len));
        if (blob_len > 0) {
            std::memcpy(blob.data(), file.data() + offset, static_cast<std::size_t>(blob_len));
        }
        offset += static_cast<std::size_t>(blob_len);
        assets.emplace(asset_id, std::move(blob));
    }
    if (offset != file.size()) {
        throw std::runtime_error("Character package has trailing bytes: " + character_package_path);
    }

    CharacterSpriteDefinition out;
    out.id = strOr(root.get("id"), "character");
    out.texture_path = character_package_path;

    const std::string profile_id = strOr(root.get("baseProfile"), "character");
    static JsonValue profile_cache_root;
    static bool profile_cache_loaded = false;
    const JsonValue* profile = profileObjectById(project_root, profile_id, profile_cache_root, profile_cache_loaded);

    out.frame_width = intOr(profile ? profile->get("frameWidth") : nullptr, 32);
    out.frame_height = intOr(profile ? profile->get("frameHeight") : nullptr, 32);
    out.columns = intOr(profile ? profile->get("columns") : nullptr, 4);
    out.rows = intOr(profile ? profile->get("rows") : nullptr, 4);

    if (const JsonValue* dirs = profile ? profile->get("directions") : nullptr; dirs && dirs->isObject()) {
        if (const JsonValue* v = dirs->get("south"); v && v->isObject()) out.row_south = intOr(v->get("row"), out.row_south);
        if (const JsonValue* v = dirs->get("west"); v && v->isObject()) out.row_west = intOr(v->get("row"), out.row_west);
        if (const JsonValue* v = dirs->get("east"); v && v->isObject()) out.row_east = intOr(v->get("row"), out.row_east);
        if (const JsonValue* v = dirs->get("north"); v && v->isObject()) out.row_north = intOr(v->get("row"), out.row_north);
    }

    const JsonValue* profile_anims = profile ? profile->get("animations") : nullptr;
    out.idle = parseAnimation(profile_anims, "idle", CharacterAnimationDef{{0}, 250});
    out.walk = parseAnimation(profile_anims, "walk", CharacterAnimationDef{{0, 1, 2, 3}, 120});
    out.run = parseAnimation(profile_anims, "run", out.walk);
    out.pause = parseAnimation(profile_anims, "pause", CharacterAnimationDef{{0}, 400});
    out.play = parseAnimation(profile_anims, "play", CharacterAnimationDef{{0,1,2,3,4,5,6,7,8,9}, 120});

    const JsonValue* sheet_array = root.get("spriteSheets");
    const JsonValue* action_array = root.get("actions");
    if (!sheet_array || !sheet_array->isArray() || !action_array || !action_array->isArray()) {
        throw std::runtime_error("Character package missing spriteSheets/actions arrays: " + character_package_path);
    }

    std::string walk_sheet_id;
    std::string idle_sheet_id;
    std::string run_sheet_id;
    std::string walk_anim_name = "walk";
    std::string idle_anim_name = "idle";
    std::string run_anim_name = "run";
    for (const JsonValue& action : action_array->asArray()) {
        if (!action.isObject()) continue;
        const std::string action_id = strOr(action.get("id"), "");
        const std::string sheet_id = strOr(action.get("sheetId"), "");
        const std::string anim_name = strOr(action.get("animationName"), "");
        if (action_id == "walk") {
            walk_sheet_id = sheet_id;
            if (!anim_name.empty()) walk_anim_name = anim_name;
        } else if (action_id == "idle") {
            idle_sheet_id = sheet_id;
            if (!anim_name.empty()) idle_anim_name = anim_name;
        } else if (action_id == "run") {
            run_sheet_id = sheet_id;
            if (!anim_name.empty()) run_anim_name = anim_name;
        }
    }

    std::string selected_sheet_id = !walk_sheet_id.empty() ? walk_sheet_id : idle_sheet_id;
    if (selected_sheet_id.empty() && !sheet_array->asArray().empty() && sheet_array->asArray()[0].isObject()) {
        selected_sheet_id = strOr(sheet_array->asArray()[0].get("id"), "");
    }
    if (selected_sheet_id.empty()) {
        throw std::runtime_error("Character package has no usable sprite sheet selection: " + character_package_path);
    }

    const auto findSheetById = [sheet_array](const std::string& sheet_id) -> const JsonValue* {
        if (sheet_id.empty()) {
            return nullptr;
        }
        for (const JsonValue& sheet : sheet_array->asArray()) {
            if (!sheet.isObject()) continue;
            if (strOr(sheet.get("id"), "") == sheet_id) {
                return &sheet;
            }
        }
        return nullptr;
    };

    const JsonValue* selected_sheet = findSheetById(selected_sheet_id);
    if (!selected_sheet || !selected_sheet->isObject()) {
        throw std::runtime_error("Character package selected sheet not found: " + selected_sheet_id + " in " + character_package_path);
    }

    const auto findSheetAsset = [&assets, &character_package_path](const JsonValue& sheet) {
        const std::string asset_id = strOr(sheet.get("assetId"), "");
        auto asset_it = assets.find(asset_id);
        if (asset_it == assets.end()) {
            throw std::runtime_error("Character package selected sheet asset not found: " + asset_id + " in " + character_package_path);
        }
        return asset_it;
    };

    auto asset_it = findSheetAsset(*selected_sheet);
    out.texture_png_bytes = asset_it->second;

    std::string selected_profile_id = strOr(selected_sheet->get("profile"), profile_id);
    const JsonValue* selected_profile = profileObjectById(project_root, selected_profile_id, profile_cache_root, profile_cache_loaded);
    const JsonValue* sheet_anims = selected_sheet->get("animations");
    const JsonValue* anim_root = selected_profile ? selected_profile->get("animations") : profile_anims;

    out.idle = parseAnimationByName(sheet_anims, anim_root, idle_anim_name, out.idle);
    out.walk = parseAnimationByName(sheet_anims, anim_root, walk_anim_name, out.walk);
    if (!run_sheet_id.empty()) {
        const JsonValue* run_sheet = findSheetById(run_sheet_id);
        if (run_sheet && run_sheet->isObject()) {
            const std::string run_profile_id = strOr(run_sheet->get("profile"), profile_id);
            const JsonValue* run_profile = profileObjectById(project_root, run_profile_id, profile_cache_root, profile_cache_loaded);
            const JsonValue* run_sheet_anims = run_sheet->get("animations");
            const JsonValue* run_anim_root = run_profile ? run_profile->get("animations") : profile_anims;
            out.run = parseAnimationByName(run_sheet_anims, run_anim_root, run_anim_name, out.run);
            out.has_run = !out.run.frames.empty();
            if (out.has_run && run_sheet_id != selected_sheet_id) {
                out.run_texture_png_bytes = findSheetAsset(*run_sheet)->second;
            }
        }
    }
    if (!out.has_run) {
        out.run = out.walk;
        out.run_texture_png_bytes.clear();
    }
    out.pause = parseAnimationByName(sheet_anims, anim_root, "pause", out.pause);
    out.play = parseAnimationByName(sheet_anims, anim_root, "play", out.play);
    if (selected_profile) {
        out.frame_width = intOr(selected_profile->get("frameWidth"), out.frame_width);
        out.frame_height = intOr(selected_profile->get("frameHeight"), out.frame_height);
        out.columns = intOr(selected_profile->get("columns"), out.columns);
        out.rows = intOr(selected_profile->get("rows"), out.rows);
        if (const JsonValue* dirs = selected_profile->get("directions"); dirs && dirs->isObject()) {
            if (const JsonValue* v = dirs->get("south"); v && v->isObject()) out.row_south = intOr(v->get("row"), out.row_south);
            if (const JsonValue* v = dirs->get("west"); v && v->isObject()) out.row_west = intOr(v->get("row"), out.row_west);
            if (const JsonValue* v = dirs->get("east"); v && v->isObject()) out.row_east = intOr(v->get("row"), out.row_east);
            if (const JsonValue* v = dirs->get("north"); v && v->isObject()) out.row_north = intOr(v->get("row"), out.row_north);
        }

        const JsonValue* rendering = selected_profile->get("rendering");
        if (rendering && rendering->isObject()) {
            out.world_height = static_cast<float>(numOr(rendering->get("worldHeight"), 32.0));
            out.sprite_scale = static_cast<float>(numOr(rendering->get("spriteScale"), 1.0));
            out.anchor = strOr(rendering->get("anchor"), "bottom_center");
            if (const JsonValue* world_offset = rendering->get("worldOffset"); world_offset && world_offset->isArray()) {
                const auto& arr = world_offset->asArray();
                if (arr.size() > 0 && arr[0].isNumber()) out.world_offset_x = static_cast<float>(arr[0].asNumber());
                if (arr.size() > 1 && arr[1].isNumber()) out.world_offset_y = static_cast<float>(arr[1].asNumber());
                if (arr.size() > 2 && arr[2].isNumber()) out.world_offset_z = static_cast<float>(arr[2].asNumber());
            }
            if (const JsonValue* screen_offset = rendering->get("screenOffsetPx"); screen_offset && screen_offset->isArray()) {
                const auto& arr = screen_offset->asArray();
                if (arr.size() > 0 && arr[0].isNumber()) out.screen_offset_x_px = static_cast<int>(arr[0].asNumber());
                if (arr.size() > 1 && arr[1].isNumber()) out.screen_offset_y_px = static_cast<int>(arr[1].asNumber());
            }
        }
    }
    if (const JsonValue* sheet_overrides = selected_sheet->get("profileOverrides");
        sheet_overrides && sheet_overrides->isObject()) {
        out.frame_width = intOr(sheet_overrides->get("frameWidth"), out.frame_width);
        out.frame_height = intOr(sheet_overrides->get("frameHeight"), out.frame_height);
        out.columns = intOr(sheet_overrides->get("columns"), out.columns);
        out.rows = intOr(sheet_overrides->get("rows"), out.rows);
    }

    return out;
}

CharacterPackageMetadata loadCharacterPackageMetadata(const std::string& character_package_path) {
    const JsonValue root = readCharbinJsonRoot(character_package_path);

    CharacterPackageMetadata out;
    out.id = strOr(root.get("id"), "");
    out.display_name = strOr(root.get("displayName"), out.id);

    const JsonValue* metadata = root.get("metadata");
    if (metadata && metadata->isObject()) {
        out.character_type = strOr(metadata->get("characterType"), "npc");
        out.movement_speed_profile = strOr(
            metadata->get("movementSpeedProfile"),
            strOr(metadata->get("movementProfile"), out.movement_speed_profile));
        const JsonValue* partner = metadata->get("partnerPokemon");
        if (partner && partner->isObject()) {
            CharacterPackagePartnerPokemon p;
            p.pokemon_id = strOr(partner->get("pokemonId"), "");
            p.form_id = strOr(partner->get("formId"), "default");
            p.nickname = strOr(partner->get("nickname"), "");
            p.relationship = strOr(partner->get("relationship"), "");
            if (!p.pokemon_id.empty()) {
                out.partner_pokemon = std::move(p);
            }
        }
    }
    return out;
}

std::optional<CharacterSpriteDefinition> tryLoadCharacterDefinition(
    const std::string& project_root,
    const std::string& character_package_path) {
    try {
        return loadCharacterDefinition(project_root, character_package_path);
    } catch (const std::exception& ex) {
        std::cerr << "[Overworld3D] Failed to load character package '" << character_package_path
                  << "': " << ex.what() << std::endl;
        return std::nullopt;
    }
}

} // namespace pr::gameplay::world3d::data
