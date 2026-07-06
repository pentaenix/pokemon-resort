#include "gameplay/attend/AttendSceneConfig.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

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
        if (fs::exists(current / "config" / "gameplay" / "pokemon_attend" / "scene.json") &&
            fs::exists(current / "config" / "gameplay" / "pokemon_attend" / "pokemon.json") &&
            fs::exists(current / "config" / "gameplay" / "pokemon_attend" / "floors.json") &&
            fs::exists(current / "config" / "gameplay" / "pokemon_attend" / "walls.json") &&
            fs::is_directory(current / "assets" / "pokemon_attend" / "pokemon_models")) {
            return current;
        }
        current = current.parent_path();
    }
    return {};
}

} // namespace

int main() {
    const fs::path root = repositoryRoot();
    expect(!root.empty(), "repository root with pokemon_attend assets should be discoverable");
    if (root.empty()) {
        return EXIT_FAILURE;
    }

    const pr::gameplay::attend::AttendSceneConfig config =
        pr::gameplay::attend::loadAttendSceneConfig(root.string());

    expect(config.id == "grass_field_environment",
           "TEST ATTEND should resolve the single grass field environment scene");
    expect(config.pokemon.id == "giratina", "active Pokemon should resolve directly from species name");
    expect(fs::exists(config.pokemon.model_path), "active Pokemon GLB path should resolve to an existing asset");
    expect(config.pokemon.model_path.find("pm0487_00_Giratina.glb") != std::string::npos,
           "active Pokemon should infer its RAE pm#### form GLB from assets by species name without a per-Pokemon config entry");
    expect(config.pokemon.animation_name.empty(),
           "dex-less species should allow renderer animation fallback instead of requiring per-Pokemon animation config");
    expect(config.pokemon.scale > 0.0f, "active Pokemon scale should be positive");
    expect(config.pokemon.z >= -2.0f && config.pokemon.z <= 2.0f,
           "active Pokemon start depth should stay in the authored attend setup range");
    expect(config.pokemon.pitch_degrees == 0.0f, "active Pokemon default attend pose should stand upright");
    expect(!config.pokemon.idle_motion.enabled, "active Pokemon should not use placeholder transform animation");
    expect(config.pokemon.idle_motion.bob_amplitude == 0.0f, "active Pokemon idle should not bob vertically");
    expect(config.pokemon.idle_motion.rock_degrees == 0.0f, "active Pokemon idle should not rock without GLB animation");
    expect(config.interaction_adapter.source == "gen7", "active Pokemon adapter source should be declared in config");
    expect(!config.interaction_adapter.head_node_names.empty() &&
               config.interaction_adapter.head_node_names.front() == "head",
           "interaction adapter should define semantic head node candidates");
    expect(!config.interaction_adapter.eyelid_node_substrings.empty() &&
               config.interaction_adapter.eyelid_node_substrings.front() == "eyelid",
           "interaction adapter should define semantic eyelid node matching");
    expect(config.interaction_adapter.eye_close_animation.empty(),
           "dex-less species should allow renderer eye animation fallback instead of requiring per-Pokemon eye config");
    expect(config.interaction_adapter.eye_expression_frames.at("normal_open") == 0,
           "eye expression frame dictionary should map frame 0 to normal open eyes");
    expect(config.interaction_adapter.eye_expression_frames.at("closed") == 4,
           "eye expression frame dictionary should map frame 4 to closed eyes for blinking and petting");
    expect(config.interaction_adapter.eye_expression_frames.at("angry") == 1 &&
               config.interaction_adapter.eye_expression_frames.at("sick_hurt") == 2 &&
               config.interaction_adapter.eye_expression_frames.at("happy") == 3 &&
               config.interaction_adapter.eye_expression_frames.at("sad") == 5 &&
               config.interaction_adapter.eye_expression_frames.at("hit") == 6 &&
               config.interaction_adapter.eye_expression_frames.at("padding_red_stain") == 7,
           "eye expression frame dictionary should preserve the known 3DS semantic frame order");
    expect(config.interaction_adapter.mouth_expression_frames.at("closed_normal") == 0,
           "mouth expression frame dictionary should map frame 0 to the normal closed mouth");
    expect(config.interaction_adapter.mouth_expression_frames.at("angry_open") == 1 &&
               config.interaction_adapter.mouth_expression_frames.at("happy_open") == 2 &&
               config.interaction_adapter.mouth_expression_frames.at("sad_closed") == 3 &&
               config.interaction_adapter.mouth_expression_frames.at("happy_open_wide") == 4 &&
               config.interaction_adapter.mouth_expression_frames.at("unused_5") == 5 &&
               config.interaction_adapter.mouth_expression_frames.at("closed_narrow") == 6 &&
               config.interaction_adapter.mouth_expression_frames.at("unused_7") == 7,
           "mouth expression frame dictionary should preserve the known Gen 7 sheet frame order");
    expect(!config.interaction_adapter.semantic_animation_slots.at("idle_default").empty() &&
               config.interaction_adapter.semantic_animation_slots.at("idle_default").front() == "slot4_00",
           "Gen 7 semantic animation map should identify slot4_00 as default idle");
    expect(config.interaction_adapter.semantic_animation_slots.at("emote_happy").front() == "slot5_21",
           "happy pet emote should prefer slot5_21 before older fallback emotes");
    expect(config.interaction_adapter.semantic_animation_slots.at("eat_start").front() == "slot5_22" &&
               config.interaction_adapter.semantic_animation_slots.at("eat_loop").front() == "slot5_23" &&
               config.interaction_adapter.semantic_animation_slots.at("eat_end").front() == "slot5_24",
           "eat semantic animation map should preserve start/loop/end slot ordering");
    expect(config.interaction_adapter.semantic_animation_slots.at("walk").front() == "slot6_02" &&
               config.interaction_adapter.semantic_animation_slots.at("run").front() == "slot6_03",
           "locomotion semantic animation map should preserve walk and run slots");
    const auto pet_happy = config.interaction_adapter.reaction_combos.at("pet_happy");
    expect(pet_happy.animation_semantic == "emote_happy" &&
               pet_happy.eye_expression == "happy" &&
               pet_happy.mouth_expression == "happy_open" &&
               pet_happy.duration_seconds > 0.5f,
           "pet_happy combo should pair a happy emote animation with happy eyes and mouth");
    expect(pet_happy.duration_seconds >= 1.5f,
           "pet_happy combo should leave enough time for a readable happy emote");
    expect(pet_happy.fade_in_seconds >= 0.3f &&
               pet_happy.fade_out_seconds > pet_happy.fade_in_seconds,
           "pet_happy combo should crossfade slowly enough to avoid snapping");
    expect(pet_happy.large_pokemon_fade_scale > 1.0f,
           "pet_happy combo should use longer blend timing for large Pokemon");
    expect(pet_happy.eye_linger_seconds >= 0.3f,
           "pet_happy combo should keep happy eyes briefly after body animation returns to idle");
    expect(pet_happy.min_pet_seconds == 1.0f,
           "pet_happy combo should trigger after one full second of petting");
    expect(pet_happy.ready_animation_semantic == "mouth_vocal" &&
               pet_happy.ready_mouth_expression == "happy_open" &&
               pet_happy.ready_weight > 0.0f &&
               pet_happy.ready_weight < 0.5f,
           "pet_happy combo should expose a small mouth cue after the pet threshold is reached");
    expect(config.interaction_adapter.eye_close_time_seconds > 0.0f,
           "interaction adapter should provide eye-close sample time");
    expect(config.interaction_adapter.pet_eye_close_delay_seconds > 0.0f &&
               config.interaction_adapter.pet_eye_close_delay_seconds < 0.5f,
           "pet eye-close delay should add polish without making petting feel unresponsive");
    expect(config.interaction_adapter.pet_eye_close_cooldown_seconds > 0.0f &&
               config.interaction_adapter.pet_eye_close_cooldown_seconds < 1.0f,
           "pet eye-close cooldown should prevent repeated tap flutter");
    expect(config.interaction_adapter.spontaneous_blink_min_seconds >= 3.0f,
           "spontaneous blinking should stay comfortably spaced");
    expect(config.interaction_adapter.spontaneous_blink_max_seconds >
               config.interaction_adapter.spontaneous_blink_min_seconds,
           "spontaneous blinking should use a randomized interval range");
    expect(config.interaction_adapter.spontaneous_blink_duration_seconds > 0.1f &&
               config.interaction_adapter.spontaneous_blink_duration_seconds < 0.8f,
           "spontaneous blink duration should be a short close-open pulse");
    expect(config.interaction_adapter.head_look_strength > 1.0f &&
               config.interaction_adapter.head_look_strength <= 3.0f,
           "head look strength should expose one readable scalar for mouse-tracking intensity");
    expect(config.camera.auto_focus, "TEST ATTEND camera should auto-frame the active Pokemon");
    expect(config.camera.screen_height_ratio > 0.2f && config.camera.screen_height_ratio < 0.9f,
           "auto-focus camera should have a sensible on-screen Pokemon size target");
    expect(config.camera.distance_scale > 0.60f && config.camera.distance_scale < 0.66f,
           "auto-focus camera distance scale should allow a single closer/farther framing tweak");
    expect(config.camera.depth_padding_scale > 0.25f && config.camera.depth_padding_scale < 0.45f,
           "full-body auto-focus should expose a moderate depth padding scale for bulky Pokemon");
    expect(config.camera.face_depth_padding_scale > 0.0f &&
               config.camera.face_depth_padding_scale < config.camera.depth_padding_scale,
           "face auto-focus should use less depth padding than full-body framing");
    expect(config.camera.face_screen_height_ratio > config.camera.screen_height_ratio,
           "face view should use a tighter screen-height target than full view");
    expect(config.camera.face_distance_scale > 0.15f &&
               config.camera.face_distance_scale < config.camera.distance_scale,
           "face view should move the auto-focus camera closer than full view");
    expect(config.camera.face_target_y_ratio > config.camera.target_y_ratio,
           "face view should target higher in the Pokemon bounds");
    expect(config.camera.face_target_y_ratio >= 0.78f &&
               config.camera.face_target_y_ratio <= 0.88f,
           "face view should target the upper head instead of large Pokemon necks");
    expect(config.camera.face_height_offset <= 0.10f,
           "face view camera should use a low angle instead of looking down from above");
    expect(config.camera.face_view_min_model_height > 1.0f,
           "face view button should be gated to large Pokemon by a single height threshold");
    expect(config.camera.min_distance > 0.0f && config.camera.max_distance > config.camera.min_distance,
           "auto-focus camera should clamp computed distance to a valid authored range");
    expect(config.camera.min_distance <= 1.5f,
           "auto-focus camera should allow small Pokemon to frame larger without per-species config");
    expect(config.camera.max_distance >= 12.0f,
           "auto-focus camera should allow large Pokemon to frame without per-species camera config");
    expect(config.camera.target_y_ratio >= 0.0f && config.camera.target_y_ratio <= 1.0f,
           "auto-focus camera should target a normalized height within the Pokemon bounds");
    expect(config.camera.distance > 0.0f, "manual camera fallback distance should remain data-driven and positive");
    expect(config.camera.target_z == config.pokemon.z,
           "manual fallback camera should target the active Pokemon start depth");

    expect(config.floor.enabled, "grass field environment floor should be enabled");
    expect(config.floor.id == "grass_field", "TEST ATTEND should use the grass field environment floor");
    expect(config.floor.placement_anchor == "world",
           "environment attend floor should keep authored world placement");
    expect(!config.floor.model_path.empty(), "environment floor should resolve a model path");
    expect(fs::exists(config.floor.model_path), "grass field environment GLB should exist");
    expect(config.floor.model_scale < 0.02f,
           "environment floor should use a large-map scale instead of platform scale");
    expect(config.floor.model_y < 0.0f && config.floor.model_y > -0.1f,
           "environment floor should place the field surface near the Pokemon feet");
    expect(config.floor.model_yaw_degrees >= -360.0f && config.floor.model_yaw_degrees <= 360.0f,
           "environment floor should support authored yaw rotation for initial map setup");
    expect(config.floor.extensions.size() == 1,
           "grass field environment should include its authored large-map extension");
    if (!config.floor.extensions.empty()) {
        const auto& extension = config.floor.extensions.front();
        expect(extension.id == "grass_extension",
               "grass field extension should keep its data-driven id");
        expect(fs::exists(extension.model_path),
               "grass field extension GLB should resolve to an existing asset");
        expect(extension.model_x == config.floor.model_x &&
                   extension.model_y == config.floor.model_y &&
                   extension.model_z == config.floor.model_z &&
                   extension.model_yaw_degrees == config.floor.model_yaw_degrees &&
                   extension.model_scale == config.floor.model_scale,
               "floor extensions without placement overrides should inherit the main floor placement");
    }
    expect(config.viewport_look.enabled, "environment attend scene should enable mouse edge-look controls");
    expect(config.viewport_look.max_x > 0.0f && config.viewport_look.max_x < 0.5f,
           "mouse edge look should use a subtle horizontal target nudge");
    expect(config.viewport_look.max_y > 0.0f && config.viewport_look.max_y < 0.25f,
           "mouse edge look should use a subtle vertical target nudge");
    expect(config.viewport_look.edge_margin_ratio >= 0.05f && config.viewport_look.edge_margin_ratio <= 0.25f,
           "mouse edge look should define a clear screen-edge activation band");
    expect(config.viewport_look.smooth_seconds >= 0.2f && config.viewport_look.smooth_seconds <= 0.6f,
           "mouse edge look should have enough weight without feeling delayed");
    expect(config.shadow.enabled, "attend scene should draw the simple contact shadow");
    expect(config.shadow.strength > 0.0f && config.shadow.strength <= 0.5f,
           "contact shadow strength should be data-driven and subtle");
    expect(config.shadow.radius_x > config.shadow.radius_z,
           "contact shadow config should keep a wider-than-deep minimum oval under the Pokemon");
    expect(config.lighting.pokemon_brightness > 1.0f &&
               config.lighting.pokemon_brightness - config.lighting.backdrop_brightness <= 0.10f,
           "attend lighting should make the Pokemon readable without looking pasted over the environment");
    expect(config.lighting.backdrop_brightness >= 0.95f &&
               config.lighting.backdrop_saturation >= 0.95f &&
               config.lighting.backdrop_contrast >= 0.95f,
           "attend lighting should keep the environment lively instead of dulling it into fog");
    expect(config.lighting.ambient > 0.5f && config.lighting.ambient < 1.0f,
           "attend Pokemon form lighting should keep a soft ambient base instead of harsh darkness");
    expect(config.lighting.directional > 0.0f && config.lighting.directional < 0.6f,
           "attend Pokemon form lighting should add readable top light without washing white Pokemon out");
    expect(config.lighting.form_shadow > 0.0f && config.lighting.form_shadow < 0.5f,
           "attend Pokemon form lighting should expose a subtle underside/side shadow control");
    expect(config.lighting.light_direction[1] > 0.0f,
           "attend Pokemon light direction should come from above for DS/Amie-style shape readability");
    expect(config.depth_of_field.enabled && config.depth_of_field.strength > 0.0f &&
               config.depth_of_field.strength < 1.0f,
           "attend camera focus should expose configurable stylized depth of field");
    expect(config.depth_of_field.max_radius > 0.0f,
           "attend depth of field should expose a configurable texture blur radius");
    expect(config.depth_of_field.focus_depth > 0.0f &&
               config.depth_of_field.falloff > config.depth_of_field.focus_depth,
           "attend depth of field should keep a crisp focus band before distance blur ramps in");
    expect(config.floor.weather_modes.size() == 3,
           "environment floor should expose clear/rain/snow weather modes");
    expect(config.floor.weather_material_substrings.size() == 3,
           "environment floor should declare weather material filters");
    expect(config.ui.weather_button.enabled, "environment attend scene should show the weather overlay button");
    expect(config.ui.weather_button.anchor == "top_right",
           "weather overlay button should default to the top-right corner");
    expect(config.ui.weather_button.corner_radius > 0,
           "weather overlay button should use rounded corners by default");
    expect(config.ui.weather_button.text.r == 1.0f &&
               config.ui.weather_button.text.g == 1.0f &&
               config.ui.weather_button.text.b == 1.0f,
           "weather overlay button text should default to white");
    expect(config.ui.view_button.enabled, "environment attend scene should show the view overlay button");
    expect(config.ui.view_button.margin_y > config.ui.weather_button.margin_y,
           "view overlay button should stack below the weather button");
    expect(config.ui.view_button.label_prefix == "VIEW: ",
           "view overlay button should clearly label full/face camera mode");
    expect(config.ui.pokemon_button.enabled, "environment attend scene should show the Pokemon cycle overlay button");
    expect(config.ui.pokemon_button.margin_y > config.ui.view_button.margin_y,
           "Pokemon overlay button should stack below the view button");
    expect(config.ui.pokemon_button.label_prefix == "POKEMON: ",
           "Pokemon overlay button should clearly label the active model");
    expect(config.ui.texture_variant_button.enabled,
           "environment attend scene should show the texture variant overlay button");
    expect(config.ui.texture_variant_button.margin_y > config.ui.pokemon_button.margin_y,
           "texture variant overlay button should stack below the Pokemon button");
    expect(config.ui.texture_variant_button.label_prefix == "COLOR: ",
           "texture variant overlay button should clearly label normal/shiny model color");
    expect(config.ui.form_variant_button.enabled,
           "environment attend scene should configure a form variant overlay button for multi-form GLBs");
    expect(config.ui.form_variant_button.margin_y > config.ui.texture_variant_button.margin_y,
           "form variant overlay button should stack below the texture variant button");
    expect(config.ui.form_variant_button.label_prefix == "FORM: ",
           "form variant overlay button should clearly label model form/pattern selection");
    expect(config.ui.hand_cursor_scale == 1.5f,
           "hand cursor scale should be data-driven and default to the current 1.5x visual size");
    expect(config.ui.hand_cursor_hotspot_x_ratio == 0.5f &&
               config.ui.hand_cursor_hotspot_y_ratio == 0.5f,
           "hand cursor hotspot should be configurable and centered for the active point");
    expect(config.ui.hand_cursor_pet_animation_speed == 1.5f,
           "hand cursor pet animation speed should be configurable and faster than the charbin default");
    expect(config.wall.enabled, "attend sky should be enabled");
    expect(config.wall.id == "clear", "TEST ATTEND should use the clear sky preset by default");
    expect(config.wall.shape == "dome", "attend sky should use the edge-proof dome shape");
    expect(config.wall.radius >= 50.0f, "attend sky dome should be large enough to cover wide and tall Pokemon framing");
    expect(config.wall.arc_degrees >= 360.0f, "attend sky dome should keep full horizontal coverage in config");
    expect(config.wall.gradient_colors.size() >= 2, "sky gradient should have multiple stops");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
