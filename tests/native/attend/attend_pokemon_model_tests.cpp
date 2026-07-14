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
    const auto translucent_body = std::find_if(
        dewpider.materials.begin(),
        dewpider.materials.end(),
        [](const auto& material) {
            return material.render_class == pr::gameplay::attend::rendering::AttendRenderClass::Opaque &&
                material.nitro_texture_alpha == "translucent";
        });
    expect(translucent_body != dewpider.materials.end(),
           "Dewpider should preserve the 3DS translucent texture signal on its opaque-classified body");
    if (translucent_body != dewpider.materials.end()) {
        expect(
            pr::gameplay::attend::rendering::shouldPromoteAttendTextureToBlend(
                *translucent_body,
                pr::gameplay::attend::rendering::AttendTextureAlphaSummary{45, 0.25f}),
            "Dewpider's strongly translucent body texture should be promoted to alpha blending");
        expect(
            !pr::gameplay::attend::rendering::shouldPromoteAttendTextureToBlend(
                *translucent_body,
                pr::gameplay::attend::rendering::AttendTextureAlphaSummary{240, 0.25f}),
            "weak ETC alpha variation should remain opaque");
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
    for (std::size_t i = 0; i < dewpider.primitives.size(); ++i) {
        const auto& primitive = dewpider.primitives[i];
        if (pr::gameplay::attend::rendering::shouldRenderAttendPokemonPrimitive(dewpider, primitive)) {
            dewpider_order.push_back(i);
            if (!primitive.default_visible &&
                primitive.material >= 0 &&
                primitive.material < static_cast<int>(dewpider.materials.size()) &&
                nameContainsAscii(
                    dewpider.materials[static_cast<std::size_t>(primitive.material)].name,
                    "vco")) {
                ++included_vco_layers;
            }
        }
    }
    expect(included_vco_layers >= 4,
           "Dewpider's required VCO feature layers should remain in the draw list");
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
