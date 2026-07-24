#include "gameplay/attend/rendering/AttendPokemonModel.hpp"
#include "gameplay/attend/rendering/AttendPokemonMaterialPolicy.hpp"

#include <algorithm>
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
        if (fs::exists(current / "CMakeLists.txt") &&
            fs::exists(current / "assets" / "pokemon_attend" / "pokemon_models" / "pm0487_00_Giratina.glbz")) {
            return current;
        }
        const fs::path parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
    return {};
}

bool nameContainsAscii(std::string value, const std::string& needle) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value.find(needle) != std::string::npos;
}

const pr::gameplay::attend::rendering::AttendPokemonAnimation* expectUsableModel(
    const pr::gameplay::attend::rendering::AttendPokemonModel& model,
    const std::string& label,
    int minimum_joints) {
    expect(model.valid, label + " GLB should load through attend Pokemon loader");
    expect(!model.primitives.empty(), label + " should expose renderable primitives");
    expect(!model.skins.empty(), label + " should expose skin data");
    if (!model.skins.empty()) {
        expect(static_cast<int>(model.skins.front().joints.size()) >= minimum_joints,
               label + " should expose enough joints for attend animation");
    }
    const auto* animation = pr::gameplay::attend::rendering::findAttendPokemonAnimation(model, "");
    expect(animation && !animation->channels.empty(),
           label + " should expose a usable fallback animation without per-species config");
    return animation;
}

bool skinnedPrimitiveMoves(
    const pr::gameplay::attend::rendering::AttendPokemonModel& model,
    const pr::gameplay::attend::rendering::AttendPokemonAnimation* animation,
    float loop_duration_seconds) {
    const pr::gameplay::attend::rendering::AttendPokemonPrimitive* skinned_primitive = nullptr;
    for (const auto& primitive : model.primitives) {
        if (primitive.skin >= 0) {
            skinned_primitive = &primitive;
            break;
        }
    }
    if (!skinned_primitive || !animation) return false;

    const auto globals_zero = pr::gameplay::attend::rendering::buildAttendPokemonGlobals(
        model,
        animation,
        0.0,
        loop_duration_seconds);
    const auto skins_zero = pr::gameplay::attend::rendering::buildAttendPokemonSkinMatrices(model, globals_zero);
    std::vector<pr::gameplay::attend::rendering::AttendPokemonVertex> at_zero;
    pr::gameplay::attend::rendering::skinAttendPokemonPrimitiveWithPose(
        model,
        *skinned_primitive,
        globals_zero,
        skins_zero,
        at_zero);

    const auto globals_later = pr::gameplay::attend::rendering::buildAttendPokemonGlobals(
        model,
        animation,
        0.25,
        loop_duration_seconds);
    const auto skins_later = pr::gameplay::attend::rendering::buildAttendPokemonSkinMatrices(model, globals_later);
    std::vector<pr::gameplay::attend::rendering::AttendPokemonVertex> later;
    pr::gameplay::attend::rendering::skinAttendPokemonPrimitiveWithPose(
        model,
        *skinned_primitive,
        globals_later,
        skins_later,
        later);

    if (at_zero.size() != later.size() || at_zero.empty()) return false;
    for (std::size_t i = 0; i < at_zero.size(); ++i) {
        const float delta = std::abs(at_zero[i].x - later[i].x) +
            std::abs(at_zero[i].y - later[i].y) +
            std::abs(at_zero[i].z - later[i].z);
        if (delta > 0.0001f) return true;
    }
    return false;
}

bool hasIncludedHiddenVcoLayer(
    const pr::gameplay::attend::rendering::AttendPokemonModel& model) {
    for (const auto& primitive : model.primitives) {
        if (primitive.default_visible || primitive.material < 0 ||
            primitive.material >= static_cast<int>(model.materials.size())) {
            continue;
        }
        if (nameContainsAscii(
                model.materials[static_cast<std::size_t>(primitive.material)].name,
                "vco") &&
            pr::gameplay::attend::rendering::shouldRenderAttendPokemonPrimitive(model, primitive)) {
            return true;
        }
    }
    return false;
}

} // namespace

int main() {
    const fs::path root = repositoryRoot();
    expect(!root.empty(), "repository root should be discoverable from the current working directory");
    if (root.empty()) return EXIT_FAILURE;

    std::string error;
    const pr::gameplay::attend::rendering::AttendPokemonModel charmander =
        pr::gameplay::attend::rendering::loadAttendPokemonModel(
            (root / "assets" / "pokemon_attend" / "pokemon_models" / "pm0004_00_Charmander.glbz").string(),
            &error);
    const auto* charmander_wait = expectUsableModel(charmander, "Charmander", 20);
    expect(skinnedPrimitiveMoves(charmander, charmander_wait, 0.0f),
           "Charmander fallback animation should move skinned vertices");

    const auto bulbasaur = pr::gameplay::attend::rendering::loadAttendPokemonModel(
        (root / "assets" / "pokemon_attend" / "pokemon_models" / "pm0001_00_Bulbasaur.glbz").string(),
        &error);
    expect(bulbasaur.valid, "Bulbasaur GLBZ should load for separate-pupil coverage");
    expect(pr::gameplay::attend::rendering::attendModelUsesSeparateEyeIris(bulbasaur),
           "Bulbasaur should retain its separate iris meshes and use the socket stencil");

    const pr::gameplay::attend::rendering::AttendPokemonModel burmy =
        pr::gameplay::attend::rendering::loadAttendPokemonModel(
            (root / "assets" / "pokemon_attend" / "pokemon_models" / "pm0412_00_Burmy.glbz").string(),
            &error);
    const auto* burmy_wait = expectUsableModel(burmy, "Burmy", 20);
    expect(burmy.form_variants.size() >= 3,
           "Burmy should expose all three cloak form variants from RAE metadata");
    if (burmy_wait) {
        bool has_form_01_channel = false;
        bool has_form_02_channel = false;
        for (const auto& channel : burmy_wait->channels) {
            if (channel.target_node < 0 || channel.target_node >= static_cast<int>(burmy.nodes.size())) continue;
            const std::string& node_name = burmy.nodes[static_cast<std::size_t>(channel.target_node)].name;
            has_form_01_channel = has_form_01_channel || nameContainsAscii(node_name, "__form_01");
            has_form_02_channel = has_form_02_channel || nameContainsAscii(node_name, "__form_02");
        }
        expect(has_form_01_channel && has_form_02_channel,
               "Burmy alternate-form rigs should keep animation channels after export/load");
        for (int form_index = 0; form_index < static_cast<int>(burmy.form_variants.size()); ++form_index) {
            const float loop_duration =
                pr::gameplay::attend::rendering::attendPokemonAnimationLoopDurationForForm(
                    burmy,
                    burmy_wait,
                    form_index);
            expect(loop_duration > 0.0f,
                   "Burmy form " + std::to_string(form_index) + " should have a positive loop duration");
        }
    }

    const pr::gameplay::attend::rendering::AttendPokemonModel giratina =
        pr::gameplay::attend::rendering::loadAttendPokemonModel(
            (root / "assets" / "pokemon_attend" / "pokemon_models" / "pm0487_00_Giratina.glbz").string(),
            &error);
    const auto* giratina_wait = expectUsableModel(giratina, "Giratina", 50);
    int giratina_default_form = -1;
    int giratina_origin_form = -1;
    for (std::size_t i = 0; i < giratina.form_variants.size(); ++i) {
        if (giratina.form_variants[i].id == giratina.default_form_variant) giratina_default_form = static_cast<int>(i);
        if (giratina.form_variants[i].id == "01") giratina_origin_form = static_cast<int>(i);
    }
    expect(giratina_default_form >= 0, "Giratina should expose a default form variant");
    expect(giratina_origin_form >= 0, "Giratina should expose Origin form variant 01");
    if (giratina_wait && giratina_default_form >= 0 && giratina_origin_form >= 0) {
        const float default_loop =
            pr::gameplay::attend::rendering::attendPokemonAnimationLoopDurationForForm(
                giratina,
                giratina_wait,
                giratina_default_form);
        const float origin_loop =
            pr::gameplay::attend::rendering::attendPokemonAnimationLoopDurationForForm(
                giratina,
                giratina_wait,
                giratina_origin_form);
        expect(default_loop > origin_loop,
               "Giratina Origin form should loop on visible Origin rig channels, not hidden default-form channels");
        expect(std::abs(origin_loop - 2.333333f) < 0.01f,
               "Giratina Origin form loop duration should match the Origin rig channel length");
    }

    const pr::gameplay::attend::rendering::AttendPokemonModel dewpider =
        pr::gameplay::attend::rendering::loadAttendPokemonModel(
            (root / "assets" / "pokemon_attend" / "pokemon_models" / "pm0751_00_Dewpider.glbz").string(),
            &error);
    expect(dewpider.valid, "Dewpider GLBZ should load for translucent-material policy coverage");
    const auto eye_sclera = std::find_if(
        dewpider.materials.begin(),
        dewpider.materials.end(),
        [](const auto& material) {
            return material.name == "Eye" &&
                material.material_role ==
                    pr::gameplay::attend::rendering::AttendMaterialRole::EyeSclera;
        });
    const auto left_iris = std::find_if(
        dewpider.materials.begin(),
        dewpider.materials.end(),
        [](const auto& material) {
            return material.name == "LIris" &&
                material.material_role ==
                    pr::gameplay::attend::rendering::AttendMaterialRole::EyeIris;
        });
    expect(eye_sclera != dewpider.materials.end() && eye_sclera->eye_sheet.enabled &&
               eye_sclera->eye_sheet.frame_offsets.size() == 8,
           "Dewpider should expose its eight authored eye-expression frames");
    expect(left_iris != dewpider.materials.end() &&
               left_iris->render_class ==
                   pr::gameplay::attend::rendering::AttendRenderClass::Blend,
           "Dewpider's separate iris should preserve its authored blend class");
    expect(
        pr::gameplay::attend::rendering::shouldRenderAttendSeparateEyeIris(0, 0),
        "Dewpider's separate irises should complete the normal-open eye frame");
    expect(
        !pr::gameplay::attend::rendering::shouldRenderAttendSeparateEyeIris(3, 0),
        "Dewpider's happy eye frame should replace, not overlap, its normal irises");
    expect(
        !pr::gameplay::attend::rendering::shouldRenderAttendSeparateEyeIris(4, 0),
        "Dewpider's closed eye frame should keep separate irises hidden");
    expect(
        pr::gameplay::attend::rendering::attendModelUsesSeparateEyeIris(dewpider),
        "Dewpider should enable socket stenciling because it has separate iris meshes");
    if (eye_sclera != dewpider.materials.end()) {
        expect(
            !pr::gameplay::attend::rendering::shouldCullAttendPokemonMaterial(*eye_sclera),
            "thin eye overlays should render double-sided even when PICA marks their back faces culled");
    }
    const auto translucent_body = std::find_if(
        dewpider.materials.begin(),
        dewpider.materials.end(),
        [](const auto& material) {
            return material.name == "BodyUniranNone" &&
                material.render_class == pr::gameplay::attend::rendering::AttendRenderClass::Blend &&
                material.nitro_texture_alpha == "translucent";
        });
    expect(translucent_body != dewpider.materials.end(),
           "Dewpider should consume RAE's authored PICA blend classification for its bubble");
    if (translucent_body != dewpider.materials.end()) {
        expect(!translucent_body->double_sided,
               "Dewpider's translucent bubble should preserve PICA backface culling");
        expect(
            translucent_body->has_authoritative_pica,
            "Dewpider's bubble should expose authoritative PICA state");
        expect(
            !pr::gameplay::attend::rendering::shouldPromoteAttendTextureToBlend(
                *translucent_body,
                pr::gameplay::attend::rendering::AttendTextureAlphaSummary{45, 0.25f}),
            "authoritative PICA blend materials should not need Resort alpha promotion");
    }

    std::vector<bool> dewpider_authored_visible(dewpider.materials.size(), false);
    for (const auto& primitive : dewpider.primitives) {
        if (!primitive.default_visible && primitive.visible_for_forms.empty()) continue;
        if (primitive.material >= 0 && primitive.material < static_cast<int>(dewpider.materials.size())) {
            dewpider_authored_visible[static_cast<std::size_t>(primitive.material)] = true;
        }
    }
    std::vector<bool> dewpider_blends(dewpider.materials.size(), false);
    for (std::size_t i = 0; i < dewpider.materials.size(); ++i) {
        dewpider_blends[i] = dewpider_authored_visible[i] &&
            dewpider.materials[i].nitro_texture_alpha == "translucent" &&
            dewpider.materials[i].material_role ==
                pr::gameplay::attend::rendering::AttendMaterialRole::None;
    }
    std::vector<std::size_t> dewpider_order;
    int included_vco_layers = 0;
    bool hidden_vco_shell_rendered = false;
    for (std::size_t i = 0; i < dewpider.primitives.size(); ++i) {
        const auto& primitive = dewpider.primitives[i];
        if (pr::gameplay::attend::rendering::shouldRenderAttendPokemonPrimitive(dewpider, primitive)) {
            dewpider_order.push_back(i);
            if (primitive.material >= 0 &&
                primitive.material < static_cast<int>(dewpider.materials.size()) &&
                nameContainsAscii(
                    dewpider.materials[static_cast<std::size_t>(primitive.material)].name,
                    "vco")) {
                ++included_vco_layers;
            }
            if (primitive.material >= 0 &&
                primitive.material < static_cast<int>(dewpider.materials.size()) &&
                dewpider.materials[static_cast<std::size_t>(primitive.material)].name ==
                    "BodyUniranVco") {
                hidden_vco_shell_rendered = true;
            }
        }
    }
    expect(included_vco_layers >= 3,
           "Dewpider's authored-visible VCO feature layers should remain in the draw list");
    expect(!hidden_vco_shell_rendered,
           "Dewpider's authoritative hidden duplicate bubble shell should stay out of the draw list");

    const auto opaque_leg = std::find_if(
        dewpider.materials.begin(),
        dewpider.materials.end(),
        [](const auto& material) {
            return material.name == "BodyVco00" &&
                material.render_class ==
                    pr::gameplay::attend::rendering::AttendRenderClass::Opaque;
        });
    expect(opaque_leg != dewpider.materials.end() && opaque_leg->has_authoritative_pica,
           "Dewpider's leg material should expose authoritative opaque PICA state");
    if (opaque_leg != dewpider.materials.end()) {
        expect(
            !pr::gameplay::attend::rendering::shouldPromoteAttendTextureToBlend(
                *opaque_leg,
                pr::gameplay::attend::rendering::AttendTextureAlphaSummary{45, 0.25f}),
            "authoritative opaque legs should keep depth writes despite unused texture alpha");
    }

    pr::gameplay::attend::rendering::AttendPokemonMaterial legacy_translucent;
    legacy_translucent.render_class =
        pr::gameplay::attend::rendering::AttendRenderClass::Opaque;
    legacy_translucent.nitro_texture_alpha = "translucent";
    expect(
        pr::gameplay::attend::rendering::shouldPromoteAttendTextureToBlend(
            legacy_translucent,
            pr::gameplay::attend::rendering::AttendTextureAlphaSummary{45, 0.25f}),
        "legacy GLBs without PICA metadata should retain strong-alpha compatibility promotion");
    expect(
        pr::gameplay::attend::rendering::shouldTreatLegacyBinaryAlphaBlendAsMask(
            true, true, false, 1.0f),
        "legacy binary-alpha floor props should become depth-writing cutouts");
    expect(
        !pr::gameplay::attend::rendering::shouldTreatLegacyBinaryAlphaBlendAsMask(
            true, true, true, 1.0f),
        "partially translucent weather textures should remain blended");

    const auto elekid = pr::gameplay::attend::rendering::loadAttendPokemonModel(
        (root / "assets" / "pokemon_attend" / "pokemon_models" / "pm0239_00_Elekid.glbz").string(),
        &error);
    expect(elekid.valid, "Elekid GLBZ should load for mouth-expression material coverage");
    const auto elekid_mouth = std::find_if(
        elekid.materials.begin(),
        elekid.materials.end(),
        [](const auto& material) {
            return material.name == "Mouth" &&
                material.material_role ==
                    pr::gameplay::attend::rendering::AttendMaterialRole::Mouth;
        });
    expect(elekid_mouth != elekid.materials.end() && elekid_mouth->eye_sheet.enabled &&
               elekid_mouth->eye_sheet.frame_offsets.size() == 8,
           "Elekid's mouth should expose its authored eight-frame expression sheet");

    const auto araquanid = pr::gameplay::attend::rendering::loadAttendPokemonModel(
        (root / "assets" / "pokemon_attend" / "pokemon_models" / "pm0752_00_Araquanid.glbz").string(),
        &error);
    expect(araquanid.valid,
           "Araquanid GLBZ should remain loadable after authoritative GF UV baking");

    const auto morelull = pr::gameplay::attend::rendering::loadAttendPokemonModel(
        (root / "assets" / "pokemon_attend" / "pokemon_models" / "pm0755_00_Morelull.glbz").string(),
        &error);
    expect(morelull.valid, "Morelull GLBZ should load for additive-glow material coverage");
    const auto morelull_glow = std::find_if(
        morelull.materials.begin(),
        morelull.materials.end(),
        [](const auto& material) {
            return material.name == "GlowInc" &&
                material.render_class ==
                    pr::gameplay::attend::rendering::AttendRenderClass::Additive;
        });
    expect(morelull_glow != morelull.materials.end(),
           "Morelull's GlowInc layer should preserve RAE's additive PICA blend state");
    if (morelull_glow != morelull.materials.end()) {
        expect(
            morelull_glow->texture_mapping ==
                pr::gameplay::attend::rendering::AttendTextureMapping::CameraSphereEnvironment,
            "Morelull's GlowInc layer should preserve its authored camera-sphere mapping");
        expect(!morelull_glow->double_sided,
               "Morelull's GlowInc shell should preserve authored backface culling");
    }
    for (const char* material_name : {"BodyBInc1", "BodyBInc2"}) {
        const auto cap = std::find_if(
            morelull.materials.begin(),
            morelull.materials.end(),
            [material_name](const auto& material) {
                return material.name == material_name &&
                    material.render_class ==
                        pr::gameplay::attend::rendering::AttendRenderClass::Opaque;
            });
        expect(cap != morelull.materials.end(),
               std::string("Morelull's ") + material_name + " cap should remain opaque");
    }

    const auto palossand = pr::gameplay::attend::rendering::loadAttendPokemonModel(
        (root / "assets" / "pokemon_attend" / "pokemon_models" / "pm0770_00_Palossand.glbz").string(),
        &error);
    expect(palossand.valid, "Palossand's regenerated GLBZ should load");
    const auto palossand_eye = std::find_if(
        palossand.materials.begin(),
        palossand.materials.end(),
        [](const auto& material) {
            return material.name == "Eye";
        });
    expect(
        palossand_eye != palossand.materials.end() &&
            palossand_eye->render_class ==
                pr::gameplay::attend::rendering::AttendRenderClass::Opaque &&
            palossand_eye->texture_mapping ==
                pr::gameplay::attend::rendering::AttendTextureMapping::Uv &&
            !palossand_eye->double_sided,
        "Palossand's eye shell should preserve opaque UV mapping and authored backface culling");
    expect(!pr::gameplay::attend::rendering::attendModelUsesSeparateEyeIris(palossand),
           "Palossand's embedded pupils should bypass the separate-iris stencil path");
    if (palossand_eye != palossand.materials.end()) {
        expect(
            !pr::gameplay::attend::rendering::shouldCullAttendPokemonMaterial(*palossand_eye),
            "Palossand's thin eye shell should not develop holes from runtime backface culling");
    }

    pr::gameplay::attend::rendering::AttendPokemonMaterial body_material;
    body_material.material_role = pr::gameplay::attend::rendering::AttendMaterialRole::None;
    body_material.double_sided = false;
    expect(pr::gameplay::attend::rendering::shouldCullAttendPokemonMaterial(body_material),
           "body geometry should retain authored backface culling");

    pr::gameplay::attend::rendering::sortAttendPokemonDrawOrder(
        dewpider,
        dewpider_blends,
        dewpider_order);
    bool saw_blended_body = false;
    bool opaque_face_after_blended_body = false;
    for (std::size_t primitive_index : dewpider_order) {
        const int material_index = dewpider.primitives[primitive_index].material;
        if (material_index < 0 || material_index >= static_cast<int>(dewpider.materials.size())) continue;
        const std::size_t material = static_cast<std::size_t>(material_index);
        if (dewpider_blends[material]) {
            saw_blended_body = true;
        } else if (saw_blended_body &&
                   (dewpider.materials[material].material_role ==
                        pr::gameplay::attend::rendering::AttendMaterialRole::EyeSclera ||
                    dewpider.materials[material].material_role ==
                        pr::gameplay::attend::rendering::AttendMaterialRole::EyeIris)) {
            opaque_face_after_blended_body = true;
        }
    }
    expect(saw_blended_body, "Dewpider should have a final translucent body pass");
    expect(!opaque_face_after_blended_body,
           "Dewpider's sclera and iris should render before its translucent bubble");

    const pr::gameplay::attend::rendering::AttendPokemonModel dialga =
        pr::gameplay::attend::rendering::loadAttendPokemonModel(
            (root / "assets" / "pokemon_attend" / "pokemon_models" / "pm0483_00_Dialga.glbz").string(),
            &error);
    expect(dialga.valid, "Dialga GLBZ should load for VCO feature-layer coverage");
    expect(hasIncludedHiddenVcoLayer(dialga),
           "Dialga's VCO spike layers should remain renderable");

    const pr::gameplay::attend::rendering::AttendPokemonModel bunnelby =
        pr::gameplay::attend::rendering::loadAttendPokemonModel(
            (root / "assets" / "pokemon_attend" / "pokemon_models" / "pm0659_00_Bunnelby.glbz").string(),
            &error);
    expect(bunnelby.valid, "Bunnelby GLBZ should load for VCO feature-layer coverage");
    expect(hasIncludedHiddenVcoLayer(bunnelby),
           "Bunnelby's VCO stomach layer should remain renderable");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
