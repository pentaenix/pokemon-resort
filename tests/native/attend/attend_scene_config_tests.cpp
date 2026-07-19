#include "gameplay/attend/AttendSceneConfig.hpp"
#include "gameplay/attend/PokemonModelCatalog.hpp"

#include <algorithm>
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
        if (fs::exists(current / "config" / "gameplay" / "pokemon_attend.json") &&
            fs::is_directory(current / "assets" / "pokemon_attend" / "pokemon_models")) {
            return current;
        }
        const fs::path parent = current.parent_path();
        if (parent == current) break;
        current = parent;
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

    expect(config.id == "alola_battle_map_debug",
           "TEST ATTEND should resolve the Alola battle map debug scene");
    expect(config.pokemon.id == "dewpider", "active Pokemon should resolve directly from species name");
    expect(fs::exists(config.pokemon.model_path), "active Pokemon GLB path should resolve to an existing asset");
    expect(config.pokemon.model_path.find("pm0751_00_Dewpider.glbz") != std::string::npos,
           "active Pokemon should infer its compiled RAE pm#### form GLBZ from assets by species name without a per-Pokemon config entry");
    const auto model_catalog = pr::gameplay::attend::discoverPokemonModels(
        root / "assets" / "pokemon_attend" / "pokemon_models");
    expect(!model_catalog.empty(), "Attend Pokemon model catalog should discover compiled assets");
    expect(std::is_sorted(
               model_catalog.begin(),
               model_catalog.end(),
               [](const auto& lhs, const auto& rhs) { return lhs.dex_number < rhs.dex_number; }),
           "Attend Pokemon model catalog should cycle in Pokedex-number order");
    const auto dewpider = std::find_if(model_catalog.begin(), model_catalog.end(), [](const auto& entry) {
        return entry.id == "dewpider";
    });
    expect(dewpider != model_catalog.end() && dewpider->dex_number == 751,
           "Dewpider should retain National Pokedex number 751 in the debug model catalog");
    expect(pr::gameplay::attend::wrappedPokemonModelCatalogIndex(0, -1, 4) == 3,
           "previous Pokemon should wrap from the first Pokedex entry to the last");
    expect(pr::gameplay::attend::wrappedPokemonModelCatalogIndex(2, -1, 4) == 1,
           "previous Pokemon should move backward by one Pokedex entry");
    expect(config.pokemon.animation_name.empty(),
           "profile-less Dewpider should allow renderer animation fallback instead of requiring per-Pokemon animation config");
    expect(config.pokemon.scale > 0.0f, "active Pokemon scale should be positive");
    expect(config.pokemon.z >= -2.0f && config.pokemon.z <= 2.0f,
           "active Pokemon start depth should stay in the authored attend setup range");
    expect(config.pokemon.pitch_degrees == 0.0f, "active Pokemon default attend pose should stand upright");
    expect(!config.pokemon.idle_motion.enabled, "active Pokemon should not use placeholder transform animation");
    expect(config.pokemon.idle_motion.bob_amplitude == 0.0f, "active Pokemon idle should not bob vertically");
    expect(config.pokemon.idle_motion.rock_degrees == 0.0f, "active Pokemon idle should not rock without GLB animation");
    expect(config.interaction_adapter.source == "gen1_7", "active Pokemon adapter source should cover the Gen 1-7 model set");
    expect(!config.interaction_adapter.head_node_names.empty() &&
               config.interaction_adapter.head_node_names.front() == "head",
           "interaction adapter should define semantic head node candidates");
    expect(!config.interaction_adapter.eyelid_node_substrings.empty() &&
               config.interaction_adapter.eyelid_node_substrings.front() == "eyelid",
           "interaction adapter should define semantic eyelid node matching");
    expect(config.interaction_adapter.eye_close_animation.empty(),
           "profile-less Dewpider should allow renderer eye animation fallback instead of requiring per-Pokemon eye config");
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
           "mouth expression frame dictionary should preserve the known Gen 1-7/3DS sheet frame order");
    expect(!config.interaction_adapter.semantic_animation_slots.at("idle_default").empty() &&
               config.interaction_adapter.semantic_animation_slots.at("idle_default").front() == "slot4_00",
           "profile-less Dewpider should use the provider default idle semantic");
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
    expect(config.camera.form_framing_adjustments.empty(),
           "Dewpider should not inherit Giratina-only form framing overrides");
    expect(config.pokemon.sprite_form_keys.empty(),
           "Dewpider should not inherit Giratina-only PokeSprite form suffixes");
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
    expect(config.camera.freecam_move_speed > 0.0f,
           "attend camera should expose Q freecam movement speed");
    expect(config.camera.freecam_mouse_sensitivity > 0.0f,
           "attend camera should expose Q freecam mouse sensitivity");
    expect(config.camera.freecam_pitch_min_degrees < config.camera.freecam_pitch_max_degrees,
           "attend Q freecam should clamp pitch to a valid authored range");
    expect(config.camera.target_z == config.pokemon.z,
           "manual fallback camera should target the active Pokemon start depth");

    expect(config.floor.enabled, "Alola battle map floor should be enabled");
    expect(config.floor.id == "alola_grass_battle_map", "TEST ATTEND should use the Alola grass battle map");
    expect(config.floor.placement_anchor == "world",
           "environment attend floor should keep authored world placement");
    expect(!config.floor.model_path.empty(), "environment floor should resolve a model path");
    expect(fs::exists(config.floor.model_path), "Alola battle map GLB should exist");
    expect(config.floor.model_scale < 0.02f,
           "environment floor should use a large-map scale instead of platform scale");
    expect(config.floor.model_y < 0.0f && config.floor.model_y > -0.1f,
           "environment floor should place the field surface near the Pokemon feet");
    expect(config.floor.model_yaw_degrees >= -360.0f && config.floor.model_yaw_degrees <= 360.0f,
           "environment floor should support authored yaw rotation for initial map setup");
    expect(config.floor.extensions.size() == 1,
           "Alola battle map should include its authored large-map extension");
    if (!config.floor.extensions.empty()) {
        const auto& extension = config.floor.extensions.front();
        expect(extension.id == "alola_grass_battle_map_extension",
               "Alola grass battle map extension should keep its data-driven id");
        expect(fs::exists(extension.model_path),
               "Alola grass battle map extension GLB should resolve to an existing asset");
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
    expect(config.ui.pixelated_overlay,
           "attend UI should composite into the pixel scene by default so it shares the DS-style definition");
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
    expect(config.ui.previous_pokemon_button.enabled,
           "environment attend scene should show the previous-Pokemon overlay button");
    expect(config.ui.previous_pokemon_button.margin_y > config.ui.pokemon_button.margin_y,
           "previous-Pokemon overlay button should stack below the Pokemon button");
    expect(config.ui.previous_pokemon_button.label_prefix == "PREVIOUS POKEMON",
           "previous-Pokemon overlay button should clearly describe its reverse-cycle action");
    expect(config.ui.texture_variant_button.enabled,
           "environment attend scene should show the texture variant overlay button");
    expect(config.ui.texture_variant_button.margin_y > config.ui.previous_pokemon_button.margin_y,
           "texture variant overlay button should stack below the previous-Pokemon button");
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
    expect(config.ui.action_button.enabled && config.ui.items_button.enabled && config.ui.return_button.enabled,
           "attend action/items/return corner buttons should be enabled through shared interaction config");
    expect(fs::exists(config.ui.action_button.icon_path) &&
               fs::exists(config.ui.items_button.icon_path) &&
               fs::exists(config.ui.return_button.icon_path),
           "attend corner button icons should resolve to existing UI assets");
    expect(config.ui.action_button.scale > 0.2f && config.ui.action_button.scale <= 1.5f &&
               config.ui.items_button.scale > 0.2f && config.ui.items_button.scale <= 1.5f &&
               config.ui.return_button.scale > 0.2f && config.ui.return_button.scale <= 1.5f,
           "attend corner button visual scale should remain data-driven and within a sane authored range");
    expect(config.ui.action_button.icon_scale > 0.2f && config.ui.action_button.icon_scale <= 0.8f &&
               config.ui.items_button.icon_scale > 0.2f && config.ui.items_button.icon_scale <= 0.8f &&
               config.ui.return_button.icon_scale > 0.2f && config.ui.return_button.icon_scale <= 0.6f,
           "attend corner button icon scale should be data-driven separately from button scale");
    expect(config.ui.action_button.icon_offset_x != 0.0f ||
               config.ui.action_button.icon_offset_y != 0.0f ||
               config.ui.items_button.icon_offset_x != 0.0f ||
               config.ui.items_button.icon_offset_y != 0.0f ||
               config.ui.return_button.icon_offset_x != 0.0f ||
               config.ui.return_button.icon_offset_y != 0.0f,
           "attend corner button icon offsets should be data-driven for per-icon centering");
    expect(config.ui.normal_render_mode == "hd" && config.ui.debug_render_mode == "hd",
           "attend UI screens should default to HD with screen-level sd/hd render mode switches");
    expect(config.ui.corner_buttons.hide_after_pet_seconds == 0.5f,
           "attend corner buttons should hide only after the configured sustained petting threshold");
    expect(config.ui.corner_buttons.return_delay_seconds > 0.0f &&
               config.ui.corner_buttons.slide_in_seconds > 0.0f &&
               config.ui.corner_buttons.slide_out_seconds > 0.0f,
           "attend corner button return and slide timing should be data-driven");
    expect(config.ui.profile_plate.enabled &&
               config.ui.profile_plate.width > 300 &&
               config.ui.profile_plate.height > 140,
           "attend profile plate should be enabled and sized from shared UI config");
    expect(config.ui.profile_plate.characteristic == "Loves to eat" &&
               config.ui.profile_plate.nature == "Hardy",
           "attend profile plate trait rows should be data-driven until real Pokemon data is wired");
    expect(config.ui.profile_plate.show_sprite,
           "attend profile plate sprite should be enabled for the split overlay sprite preview");
    expect(!config.ui.profile_plate.font_path.empty(),
           "attend profile plate font should remain data-driven");
    expect(config.ui.profile_plate.font_path.find("power clear.ttf") != std::string::npos,
           "attend profile plate should use Power Clear for the current split text overlay pass");
    expect(config.ui.profile_plate.sprite_size > 0 &&
               config.ui.profile_plate.sprite_scale >= 1.0f,
           "attend profile plate should expose a true sprite scale multiplier for final-overlay icons");
    expect(config.ui.profile_plate.name_font_size > config.ui.profile_plate.detail_font_size,
           "attend profile plate name text should remain larger than trait text");
    expect(config.ui.profile_plate.name_row_height == config.ui.profile_plate.detail_row_height &&
               config.ui.profile_plate.row_gap > 0,
           "attend profile plate should keep consistent banner heights with separate three-row spacing controls");
    expect(config.ui.profile_plate.name_row_width > config.ui.profile_plate.characteristic_row_width &&
               config.ui.profile_plate.characteristic_row_width > config.ui.profile_plate.nature_row_width,
           "attend profile plate rows should be independently sized from longest name row to shortest nature row");
    expect(config.ui.profile_plate.left_fade_width > 0 &&
               config.ui.profile_plate.slide_out_x > 0,
           "attend profile plate fade and slide behavior should be configurable with the rest of the UI");
    expect(config.ui.profile_plate.sprite_offset_y < 0 &&
               config.ui.profile_plate.name_text_offset_y == 0 &&
               config.ui.profile_plate.detail_text_offset_y == 0,
           "attend profile plate icon and row text alignment should be data-driven fine-tuning");
    expect(config.idle_behavior.enabled, "attend idle behavior should be data-driven and enabled for the debug scene");
    expect(config.idle_behavior.emote_after_seconds == 30.0f,
           "idle emotes should wait for at least 30 seconds without Pokemon interaction");
    expect(config.idle_behavior.emote_interval_min_seconds == 0.0f &&
               config.idle_behavior.emote_interval_max_seconds == 30.0f,
           "idle emotes should use a random delay after the minimum inactivity threshold");
    expect(config.idle_behavior.sleep_after_seconds == 180.0f,
           "idle sleep should wait for three minutes without Pokemon interaction");
    expect(config.idle_behavior.emote_animation_semantic == "emote_vocal" &&
               config.idle_behavior.sleep_animation_semantic == "sleep_loop",
           "idle behavior should request semantic animations so missing Pokemon slots no-op");
    expect(config.wall.enabled, "attend sky should be enabled");
    expect(config.wall.id == "clear", "TEST ATTEND should use the clear sky preset by default");
    expect(config.wall.shape == "dome", "attend sky should use the edge-proof dome shape");
    expect(config.wall.radius >= 50.0f, "attend sky dome should be large enough to cover wide and tall Pokemon framing");
    expect(config.wall.arc_degrees >= 360.0f, "attend sky dome should keep full horizontal coverage in config");
    expect(config.wall.gradient_colors.size() >= 2, "sky gradient should have multiple stops");
    expect(config.sky_presets.size() == 4,
           "attend sky config should expose clear/sunset/cloudy/night presets for the Time button");
    if (config.sky_presets.size() >= 4) {
        expect(config.sky_presets[0].id == "clear" &&
                   config.sky_presets[1].id == "sunset" &&
                   config.sky_presets[2].id == "cloudy" &&
                   config.sky_presets[3].id == "night",
               "attend sky presets should keep the authored Time button cycle order");
        expect(config.sky_presets[1].lighting.tint.r > config.sky_presets[0].lighting.tint.r &&
                   config.sky_presets[1].lighting.tint.b < config.sky_presets[0].lighting.tint.b,
               "sunset sky preset should carry its own warmer lighting tint");
    }
    expect(config.active_sky == 0, "clear should be the active sky preset at startup");
    expect(config.ui.sky_button.enabled && config.ui.sky_button.anchor == "top_left",
           "Time sky button should be enabled on the opposite side of the debug menu");
    expect(config.ui.emote_button.enabled && config.ui.emote_button.anchor == "top_left" &&
               config.ui.emote_button.margin_y > config.ui.sky_button.margin_y,
           "manual Emote button should sit under Time in the debug menu");
    expect(config.ui.sleep_button.enabled && config.ui.sleep_button.anchor == "top_left" &&
               config.ui.sleep_button.margin_y > config.ui.emote_button.margin_y,
           "manual Sleep button should sit under Emote in the debug menu");
    expect(config.ui.cry_button.enabled && config.ui.cry_button.anchor == "top_left" &&
               config.ui.cry_button.margin_y > config.ui.sleep_button.margin_y,
           "manual Cry button should sit under Sleep in the debug menu");
    expect(config.audio.pet_happy_cry_delay_seconds == 0.2f,
           "pet happy cry should use the data-driven post-petting audio delay");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
