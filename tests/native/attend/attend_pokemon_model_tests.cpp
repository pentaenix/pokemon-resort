#include "gameplay/attend/rendering/AttendPokemonModel.hpp"

#include <SDL_image.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

fs::path repositoryRoot() {
    fs::path current = fs::current_path();
    while (!current.empty()) {
        if (fs::exists(current / "assets" / "pokemon_attend" / "pokemon_models" / "psyduck.glb")) {
            return current;
        }
        current = current.parent_path();
    }
    return {};
}

struct PngStats {
    bool valid = false;
    bool white_only = true;
    int nonzero_alpha = 0;
    int nonwhite_alpha = 0;
    int alpha_zero_nonzero_rgb = 0;
    int min_alpha = 255;
    int max_alpha = 0;
};

PngStats inspectPng(const std::vector<std::uint8_t>& bytes) {
    PngStats stats;
    SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
    if (!rw) return stats;
    SDL_Surface* loaded = IMG_Load_RW(rw, 1);
    if (!loaded) return stats;
    SDL_Surface* rgba = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(loaded);
    if (!rgba) return stats;
    stats.valid = true;
    const auto* pixels = static_cast<const std::uint8_t*>(rgba->pixels);
    for (int i = 0; i < rgba->w * rgba->h; ++i) {
        const std::uint8_t r = pixels[i * 4 + 0];
        const std::uint8_t g = pixels[i * 4 + 1];
        const std::uint8_t b = pixels[i * 4 + 2];
        const std::uint8_t a = pixels[i * 4 + 3];
        stats.min_alpha = std::min(stats.min_alpha, static_cast<int>(a));
        stats.max_alpha = std::max(stats.max_alpha, static_cast<int>(a));
        if (a > 0) {
            ++stats.nonzero_alpha;
            if (r != 255 || g != 255 || b != 255) {
                ++stats.nonwhite_alpha;
                stats.white_only = false;
            }
        } else if (r != 0 || g != 0 || b != 0) {
            ++stats.alpha_zero_nonzero_rgb;
        }
    }
    SDL_FreeSurface(rgba);
    return stats;
}

bool nameContainsAscii(std::string value, const std::string& needle) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value.find(needle) != std::string::npos;
}

const pr::gameplay::attend::rendering::AttendPokemonMaterial* findEyeScleraMaterial(
    const pr::gameplay::attend::rendering::AttendPokemonModel& model) {
    for (const auto& material : model.materials) {
        if (material.material_role == pr::gameplay::attend::rendering::AttendMaterialRole::EyeSclera) {
            return &material;
        }
    }
    return nullptr;
}

const pr::gameplay::attend::rendering::AttendPokemonMaterial* findEyeMaterial(
    const pr::gameplay::attend::rendering::AttendPokemonModel& model) {
    if (const auto* eye_sclera = findEyeScleraMaterial(model)) {
        return eye_sclera;
    }
    for (const auto& material : model.materials) {
        if (material.pokemon_eye || nameContainsAscii(material.name, "eye")) {
            return &material;
        }
    }
    return nullptr;
}

const pr::gameplay::attend::rendering::AttendPokemonMaterial* findMouthMaterial(
    const pr::gameplay::attend::rendering::AttendPokemonModel& model) {
    for (const auto& material : model.materials) {
        if (material.material_role == pr::gameplay::attend::rendering::AttendMaterialRole::Mouth) {
            return &material;
        }
    }
    return nullptr;
}

void expectUsableThreeDsPokemon(
    const pr::gameplay::attend::rendering::AttendPokemonModel& model,
    const std::string& label,
    int minimum_joints) {
    expect(model.valid, label + " GLB should load through attend Pokemon loader");
    expect(!model.primitives.empty(), label + " should expose renderable primitives");
    expect(!model.skins.empty(), label + " should expose skin data");
    bool has_usable_normal = false;
    for (const auto& primitive : model.primitives) {
        for (const auto& vertex : primitive.vertices) {
            const float len = std::sqrt(vertex.nx * vertex.nx + vertex.ny * vertex.ny + vertex.nz * vertex.nz);
            if (len > 0.5f) {
                has_usable_normal = true;
                break;
            }
        }
        if (has_usable_normal) break;
    }
    expect(has_usable_normal, label + " should preserve vertex normals for cheap attend form lighting");
    if (!model.skins.empty()) {
        expect(static_cast<int>(model.skins.front().joints.size()) >= minimum_joints,
               label + " should expose enough joints for interaction semantics");
    }

    const auto* animation = pr::gameplay::attend::rendering::findAttendPokemonAnimation(model, "");
    expect(animation && !animation->channels.empty(),
           label + " should expose a usable fallback animation without per-species config");

    const auto* eye = findEyeScleraMaterial(model);
    expect(eye != nullptr, label + " RAE eye material should be detected from materialRole");
    if (eye) {
        expect(eye->has_base_color_texture, label + " RAE eye material should expose base color texture");
        expect(eye->has_rae_policy, label + " RAE eye material should mark RAE policy as authoritative");
        expect(!eye->pokemon_eye, label + " RAE eye sheet should not use the Violet emissive-mask shader path");
        expect(eye->eye_sheet.enabled, label + " RAE eye material should expose eyeSheet metadata");
        expect(!eye->eye_sheet.frame_offsets.empty(),
               label + " RAE eye material should expose eyeExpression frame offsets");
        expect(eye->base_color_sampler.wrap_s == 33648 && eye->base_color_sampler.wrap_t == 10497,
               label + " eye texture sampler should preserve glTF mirror/repeat wrap constants");
        const PngStats base = inspectPng(eye->base_color_bytes);
        expect(base.valid, label + " eye material should expose a renderable base texture");
    }
}

} // namespace

int main() {
    const fs::path root = repositoryRoot();
    expect(!root.empty(), "repository root should be discoverable");
    if (root.empty()) return EXIT_FAILURE;

    std::string error;
    const pr::gameplay::attend::rendering::AttendPokemonModel model =
        pr::gameplay::attend::rendering::loadAttendPokemonModel(
            (root / "assets" / "pokemon_attend" / "pokemon_models" / "psyduck.glb").string(),
            &error);
    expect(model.valid, "Psyduck GLB should load through attend Pokemon loader: " + error);
    if (!model.valid) return EXIT_FAILURE;

    const auto* animation = pr::gameplay::attend::rendering::findAttendPokemonAnimation(model, "");
    expectUsableThreeDsPokemon(model, "Psyduck", 20);

    const pr::gameplay::attend::rendering::AttendPokemonPrimitive* skinned_primitive = nullptr;
    for (const auto& primitive : model.primitives) {
        if (primitive.skin >= 0) {
            skinned_primitive = &primitive;
            break;
        }
    }
    expect(skinned_primitive != nullptr, "Psyduck should have a skinned primitive");
    bool has_psyduck_iris = false;
    for (const auto& material : model.materials) {
        if (material.material_role == pr::gameplay::attend::rendering::AttendMaterialRole::EyeIris) {
            has_psyduck_iris = true;
            expect(material.render_class == pr::gameplay::attend::rendering::AttendRenderClass::Mask,
                   "Psyduck separate iris material should preserve mask policy for pupil compositing");
        }
    }
    expect(has_psyduck_iris,
           "Psyduck should expose separate iris materials for attend eye-mask compositing coverage");
    if (skinned_primitive && animation) {
        std::vector<pr::gameplay::attend::rendering::AttendPokemonVertex> at_zero;
        std::vector<pr::gameplay::attend::rendering::AttendPokemonVertex> later;
        pr::gameplay::attend::rendering::skinAttendPokemonPrimitive(model, *skinned_primitive, animation, 0.0, at_zero);
        pr::gameplay::attend::rendering::skinAttendPokemonPrimitive(model, *skinned_primitive, animation, 0.35, later);
        expect(at_zero.size() == later.size() && !at_zero.empty(), "skinning should preserve primitive vertex count");
        bool moved = false;
        for (std::size_t i = 0; i < at_zero.size() && i < later.size(); ++i) {
            const float dx = std::abs(at_zero[i].x - later[i].x);
            const float dy = std::abs(at_zero[i].y - later[i].y);
            const float dz = std::abs(at_zero[i].z - later[i].z);
            if (dx + dy + dz > 0.0001f) {
                moved = true;
                break;
            }
        }
        expect(moved, "sampling default wait animation at a later time should move skinned vertices");
    }

    const pr::gameplay::attend::rendering::AttendPokemonModel dratini =
        pr::gameplay::attend::rendering::loadAttendPokemonModel(
            (root / "assets" / "pokemon_attend" / "pokemon_models" / "dratini.glb").string(),
            &error);
    expect(dratini.valid, "Dratini GLB should load through attend Pokemon loader: " + error);
    expectUsableThreeDsPokemon(dratini, "Dratini", 20);

    const pr::gameplay::attend::rendering::AttendPokemonModel eevee =
        pr::gameplay::attend::rendering::loadAttendPokemonModel(
            (root / "assets" / "pokemon_attend" / "pokemon_models" / "eevee.glb").string(),
            &error);
    expect(eevee.valid, "Eevee GLB should load through attend Pokemon loader: " + error);
    const auto* eevee_wait = pr::gameplay::attend::rendering::findAttendPokemonAnimation(eevee, "");
    expect(eevee_wait && !eevee_wait->channels.empty(),
           "Eevee should expose a usable animation, falling back when provider names differ");
    expect(!eevee.skins.empty() && eevee.skins.front().joints.size() >= 20,
           "Eevee skin should expose enough joints for interaction semantics");
    const pr::gameplay::attend::rendering::AttendPokemonMaterial* eevee_eye_material = findEyeMaterial(eevee);
    expect(eevee_eye_material != nullptr, "Eevee eye material should be detected");
    if (eevee_eye_material) {
        const PngStats base = inspectPng(eevee_eye_material->base_color_bytes);
        expect(base.valid, "Eevee eye material should expose a renderable base texture");
        if (eevee_eye_material->has_emissive_texture) {
            const PngStats lym = inspectPng(eevee_eye_material->emissive_bytes);
            expect(lym.valid, "Eevee eye emissive/lym texture should decode when present");
        }
    }
    const pr::gameplay::attend::rendering::AttendPokemonMaterial* eevee_mouth_material = findMouthMaterial(eevee);
    expect(eevee_mouth_material != nullptr, "Eevee mouth sheet material should be detected from RAE metadata/name");
    if (eevee_mouth_material) {
        expect(eevee_mouth_material->eye_sheet.enabled,
               "Eevee mouth material should expose sheet frame offsets like eye expressions");
        expect(eevee_mouth_material->eye_sheet.frame_offsets.size() >= 8,
               "Eevee mouth sheet should expose the shared 8-frame expression layout");
    }

    const pr::gameplay::attend::rendering::AttendPokemonModel reshiram =
        pr::gameplay::attend::rendering::loadAttendPokemonModel(
            (root / "assets" / "pokemon_attend" / "pokemon_models" / "reshiram.glb").string(),
            &error);
    expect(reshiram.valid, "Reshiram GLB should load through attend Pokemon loader: " + error);
    expect(!reshiram.primitives.empty(), "Reshiram should expose renderable primitives");
    expect(!reshiram.skins.empty() && reshiram.skins.front().joints.size() >= 50,
           "Reshiram should expose a large skinned Gen 7 skeleton");
    const auto* reshiram_fallback = pr::gameplay::attend::rendering::findAttendPokemonAnimation(reshiram, "");
    expect(reshiram_fallback && !reshiram_fallback->channels.empty(),
           "Reshiram should expose a usable fallback animation without per-species config");
    const pr::gameplay::attend::rendering::AttendPokemonPrimitive* reshiram_skinned_primitive = nullptr;
    for (const auto& primitive : reshiram.primitives) {
        if (primitive.skin >= 0) {
            reshiram_skinned_primitive = &primitive;
            break;
        }
    }
    expect(reshiram_skinned_primitive != nullptr, "Reshiram should have a skinned primitive for pose-cache coverage");
    if (reshiram_skinned_primitive && reshiram_fallback) {
        std::vector<pr::gameplay::attend::rendering::AttendPokemonVertex> direct;
        std::vector<pr::gameplay::attend::rendering::AttendPokemonVertex> cached;
        pr::gameplay::attend::rendering::skinAttendPokemonPrimitive(
            reshiram,
            *reshiram_skinned_primitive,
            reshiram_fallback,
            0.27,
            direct);
        const auto globals = pr::gameplay::attend::rendering::buildAttendPokemonGlobals(
            reshiram,
            reshiram_fallback,
            0.27);
        const auto skin_matrices = pr::gameplay::attend::rendering::buildAttendPokemonSkinMatrices(reshiram, globals);
        pr::gameplay::attend::rendering::skinAttendPokemonPrimitiveWithPose(
            reshiram,
            *reshiram_skinned_primitive,
            globals,
            skin_matrices,
            cached);
        expect(direct.size() == cached.size() && !cached.empty(),
               "cached pose skinning should preserve primitive vertex count");
        bool same = direct.size() == cached.size();
        for (std::size_t i = 0; same && i < direct.size(); ++i) {
            const float delta = std::abs(direct[i].x - cached[i].x) +
                std::abs(direct[i].y - cached[i].y) +
                std::abs(direct[i].z - cached[i].z);
            same = delta < 0.00001f;
        }
        expect(same, "cached pose skinning should match direct skinning output");
    }
    const pr::gameplay::attend::rendering::AttendPokemonMaterial* reshiram_eye_material = nullptr;
    for (const auto& material : reshiram.materials) {
        if (material.material_role == pr::gameplay::attend::rendering::AttendMaterialRole::EyeSclera) {
            reshiram_eye_material = &material;
            break;
        }
    }
    expect(reshiram_eye_material != nullptr, "Reshiram RAE eye material should be detected from materialRole");
    if (reshiram_eye_material) {
        const PngStats base = inspectPng(reshiram_eye_material->base_color_bytes);
        expect(base.valid, "Reshiram eye material should expose a renderable base texture");
        expect(reshiram_eye_material->render_class == pr::gameplay::attend::rendering::AttendRenderClass::Opaque,
               "Reshiram eye material should honor RAE renderClass instead of PNG-alpha guessing");
        expect(reshiram_eye_material->has_rae_policy,
               "Reshiram eye material should mark RAE policy as authoritative");
        expect(!reshiram_eye_material->pokemon_eye,
               "Reshiram RAE eye sheet should not use the Violet emissive-mask shader path");
        expect(reshiram_eye_material->eye_sheet.enabled,
               "Reshiram eye material should expose RAE eyeSheet metadata");
        expect(reshiram_eye_material->eye_sheet.wrap_s == 3 && reshiram_eye_material->eye_sheet.wrap_t == 2,
               "Reshiram eye sheet should preserve GF mirror/repeat wrap values");
        expect(reshiram_eye_material->base_color_sampler.wrap_s == 33648 &&
                   reshiram_eye_material->base_color_sampler.wrap_t == 10497,
               "Reshiram eye texture sampler should preserve glTF mirror/repeat wrap constants");
        expect(!reshiram_eye_material->eye_sheet.frame_offsets.empty(),
               "Reshiram eye material should expose eyeExpression frame offsets");
    }
    bool has_eye_render_order = false;
    for (const auto& primitive : reshiram.primitives) {
        if (primitive.render_order >= 2 && primitive.default_visible) {
            has_eye_render_order = true;
            break;
        }
    }
    expect(has_eye_render_order, "Reshiram mesh primitives should preserve RAE renderOrder/defaultVisible metadata");

    const pr::gameplay::attend::rendering::AttendPokemonModel giratina =
        pr::gameplay::attend::rendering::loadAttendPokemonModel(
            (root / "assets" / "pokemon_attend" / "pokemon_models" / "giratina.glb").string(),
            &error);
    expect(giratina.valid, "Giratina GLB should load through attend Pokemon loader: " + error);
    expect(!giratina.primitives.empty(), "Giratina should expose renderable primitives");
    const auto* giratina_fallback = pr::gameplay::attend::rendering::findAttendPokemonAnimation(giratina, "");
    expect(giratina_fallback && !giratina_fallback->channels.empty(),
           "Giratina should expose a usable fallback animation without per-species config");
    bool has_giratina_hidden_visibility_mesh = false;
    for (const auto& primitive : giratina.primitives) {
        bool visibility_controlled = false;
        if (primitive.mesh_node >= 0 && primitive.mesh_node < static_cast<int>(giratina.nodes.size())) {
            visibility_controlled =
                nameContainsAscii(giratina.nodes[static_cast<std::size_t>(primitive.mesh_node)].name, "vco");
        }
        if (!visibility_controlled &&
            primitive.material >= 0 &&
            primitive.material < static_cast<int>(giratina.materials.size())) {
            visibility_controlled =
                nameContainsAscii(giratina.materials[static_cast<std::size_t>(primitive.material)].name, "vco");
        }
        if (visibility_controlled && !primitive.default_visible) {
            has_giratina_hidden_visibility_mesh = true;
        }
    }
    expect(has_giratina_hidden_visibility_mesh,
           "Giratina should cover hidden Vco meshes while renderer camera framing handles its large full-body depth");

    const pr::gameplay::attend::rendering::AttendPokemonModel venusaur =
        pr::gameplay::attend::rendering::loadAttendPokemonModel(
            (root / "assets" / "pokemon_attend" / "pokemon_models" / "venusaur.glb").string(),
            &error);
    expect(venusaur.valid, "Venusaur GLB should load through attend Pokemon loader: " + error);
    expect(venusaur.default_texture_variant == "normal",
           "Venusaur combined GLB should declare normal as the default texture variant");
    expect(venusaur.texture_variants.size() >= 2,
           "Venusaur combined GLB should expose normal and shiny texture variant options");
    bool has_normal_variant = false;
    bool has_shiny_variant = false;
    for (const auto& option : venusaur.texture_variants) {
        has_normal_variant = has_normal_variant || option.id == "normal";
        has_shiny_variant = has_shiny_variant || option.id == "shiny";
    }
    expect(has_normal_variant && has_shiny_variant,
           "Venusaur texture variant options should include normal and shiny ids");
    if (!venusaur.form_variants.empty()) {
        expect(!venusaur.default_form_variant.empty(),
               "Any RAE form/pattern appearance axis should declare a default form id");
    }
    bool has_shiny_material_link = false;
    for (const auto& material : venusaur.materials) {
        if (material.shiny_material_index >= 0 &&
            material.shiny_material_index < static_cast<int>(venusaur.materials.size())) {
            has_shiny_material_link = true;
            const auto& shiny = venusaur.materials[static_cast<std::size_t>(material.shiny_material_index)];
            expect(shiny.has_base_color_texture,
                   "Venusaur shiny material link should point to an embedded renderable material texture");
            break;
        }
    }
    expect(has_shiny_material_link,
           "Venusaur normal materials should link to shiny material entries through extras.rae.shinyMaterialIndex");

    const pr::gameplay::attend::rendering::AttendPokemonModel grass_field =
        pr::gameplay::attend::rendering::loadAttendPokemonModel(
            (root / "assets" / "pokemon_attend" / "floors" / "grass_field.glb").string(),
            &error);
    expect(grass_field.valid, "Grass field environment GLB should load through the attend model loader: " + error);
    expect(!grass_field.primitives.empty(), "Grass field environment should expose renderable primitives");
    expect(!grass_field.materials.empty(), "Grass field environment should expose material data for weather filtering");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
