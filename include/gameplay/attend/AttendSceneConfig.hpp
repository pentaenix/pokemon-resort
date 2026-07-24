#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace pr::gameplay::attend {

struct Color3 {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

struct Color4 {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct GradientStop {
    float at = 0.0f;
    Color3 color{};
};

struct WeatherModeConfig {
    std::string id = "clear";
    std::string label = "Clear";
    std::vector<std::string> visible_material_substrings;
};

struct IdleMotionConfig {
    bool enabled = true;
    float bob_amplitude = 0.04f;
    float bob_seconds = 2.0f;
    float rock_degrees = 2.0f;
    float rock_seconds = 3.0f;
};

struct PokemonAttendModelConfig {
    std::string id = "psyduck";
    std::string model_path;
    std::string animation_name;
    std::unordered_map<std::string, std::string> sprite_form_keys{};
    float yaw_degrees = 180.0f;
    float pitch_degrees = 0.0f;
    float scale = 0.018f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    IdleMotionConfig idle_motion{};
};

struct AttendInteractionAdapterConfig {
    struct ReactionCombo {
        std::string animation_semantic;
        std::string eye_expression = "normal_open";
        std::string mouth_expression = "closed_normal";
        float duration_seconds = 1.2f;
        float fade_in_seconds = 0.18f;
        float fade_out_seconds = 0.28f;
        float eye_linger_seconds = 0.18f;
        float min_pet_seconds = 0.0f;
        float large_pokemon_height = 1.45f;
        float large_pokemon_fade_scale = 1.0f;
        std::string ready_animation_semantic;
        std::string ready_eye_expression = "normal_open";
        std::string ready_mouth_expression = "closed_normal";
        float ready_weight = 0.0f;
        float ready_fade_seconds = 0.25f;
    };

    std::string source = "auto";
    std::vector<std::string> head_node_names{"head"};
    std::vector<std::string> eyelid_node_substrings{"eyelid"};
    std::string eye_close_animation;
    std::unordered_map<std::string, std::vector<std::string>> semantic_animation_slots{
        {"idle_default", {"slot4_00"}},
        {"emote_vocal", {"slot4_01"}},
        {"emote_happy", {"slot5_21", "slot5_16", "slot5_12", "slot4_01"}},
        {"sleep_start", {"slot5_04"}},
        {"sleep_loop", {"slot5_05"}},
        {"eat_start", {"slot5_22", "slot5_23"}},
        {"eat_loop", {"slot5_23", "slot5_25"}},
        {"eat_end", {"slot5_24", "slot5_23"}},
        {"walk", {"slot6_02"}},
        {"run", {"slot6_03"}}};
    std::unordered_map<std::string, ReactionCombo> reaction_combos{
        {"pet_happy", ReactionCombo{"emote_happy", "happy", "happy_open", 1.7f, 0.35f, 0.55f, 0.35f, 1.0f, 1.45f, 1.55f, "mouth_vocal", "normal_open", "happy_open", 0.24f, 0.35f}}};
    std::unordered_map<std::string, int> eye_expression_frames{
        {"normal_open", 0},
        {"angry", 1},
        {"sick_hurt", 2},
        {"happy", 3},
        {"closed", 4},
        {"sad", 5},
        {"hit", 6},
        {"padding_red_stain", 7}};
    std::unordered_map<std::string, int> mouth_expression_frames{
        {"closed_normal", 0},
        {"angry_open", 1},
        {"happy_open", 2},
        {"sad_closed", 3},
        {"happy_open_wide", 4},
        {"closed_narrow", 6},
        {"unused_5", 5},
        {"unused_7", 7}};
    float eye_close_time_seconds = 0.39f;
    float pet_eye_close_delay_seconds = 0.16f;
    float pet_eye_close_cooldown_seconds = 0.42f;
    float spontaneous_blink_min_seconds = 3.5f;
    float spontaneous_blink_max_seconds = 7.5f;
    float spontaneous_blink_duration_seconds = 0.34f;
    float head_look_strength = 1.0f;
    float head_yaw_axis[3] = {1.0f, 0.0f, 0.0f};
    float head_pitch_axis[3] = {0.0f, 1.0f, 0.0f};
};

struct AttendCameraConfig {
    struct FormFramingAdjustment {
        float target_y_ratio_offset = 0.0f;
        float height_offset = 0.0f;
    };

    bool auto_focus = true;
    float screen_height_ratio = 0.58f;
    float distance_scale = 1.0f;
    float face_screen_height_ratio = 0.86f;
    float face_distance_scale = 0.42f;
    float face_target_y_ratio = 0.66f;
    float face_height_offset = 0.16f;
    float face_view_min_model_height = 1.45f;
    float min_distance = 2.8f;
    float max_distance = 7.0f;
    float height_offset = 0.72f;
    float target_y_ratio = 0.46f;
    float depth_padding_scale = 0.36f;
    float face_depth_padding_scale = 0.12f;
    float target_x = 0.0f;
    float target_z = 0.0f;
    float distance = 5.4f;
    float height = 1.45f;
    float target_height = 0.82f;
    float fov_y_degrees = 38.0f;
    float near_clip = 0.05f;
    float far_clip = 80.0f;
    float freecam_move_speed = 4.0f;
    float freecam_mouse_sensitivity = 0.12f;
    float freecam_initial_offset_x = 0.0f;
    float freecam_initial_offset_y = 0.0f;
    float freecam_initial_offset_z = 0.0f;
    float freecam_initial_yaw_degrees = 180.0f;
    float freecam_initial_pitch_degrees = -12.0f;
    float freecam_pitch_min_degrees = -75.0f;
    float freecam_pitch_max_degrees = 75.0f;
    std::unordered_map<std::string, FormFramingAdjustment> form_framing_adjustments{};
};

struct AttendViewportLookConfig {
    bool enabled = false;
    float max_x = 0.8f;
    float max_y = 0.35f;
    float edge_margin_ratio = 0.12f;
    float smooth_seconds = 0.22f;
};

struct AttendShadowConfig {
    bool enabled = true;
    float strength = 0.30f;
    float radius_x = 0.42f;
    float radius_z = 0.26f;
    float y_offset = 0.006f;
};

struct AttendLightingConfig {
    float brightness = 1.0f;
    float pokemon_brightness = 1.0f;
    float backdrop_brightness = 1.0f;
    float backdrop_saturation = 1.0f;
    float backdrop_contrast = 1.0f;
    float ambient = 0.78f;
    float directional = 0.28f;
    float form_shadow = 0.20f;
    float light_direction[3] = {-0.35f, 0.82f, 0.45f};
    Color3 tint{1.0f, 1.0f, 1.0f};
};

struct AttendDepthOfFieldConfig {
    bool enabled = false;
    float strength = 0.0f;
    float max_radius = 3.0f;
    float focus_depth = 0.75f;
    float falloff = 3.5f;
};

struct AttendFloorExtensionConfig {
    bool enabled = true;
    std::string id;
    std::string model_path;
    float model_x = 0.0f;
    float model_y = 0.0f;
    float model_z = 0.0f;
    float model_yaw_degrees = 0.0f;
    float model_scale = 1.0f;
};

struct AttendFloorConfig {
    bool enabled = true;
    std::string id = "grass_field";
    std::string placement_anchor = "world";
    std::string model_path;
    std::string animation_name;
    std::string time_of_day = "day";
    std::string weather_id = "clear";
    float model_x = 0.0f;
    float model_y = 0.0f;
    float model_z = 0.0f;
    float model_yaw_degrees = 0.0f;
    float model_scale = 1.0f;
    float anchor_offset_x = 0.0f;
    float anchor_offset_z = 0.0f;
    std::vector<std::string> hidden_material_substrings;
    std::vector<std::string> weather_material_substrings;
    std::vector<WeatherModeConfig> weather_modes;
    std::vector<AttendFloorExtensionConfig> extensions;
    int active_weather = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct AttendEnvironmentOption {
    std::string id;
    std::string label;
};

struct AttendWallConfig {
    bool enabled = true;
    std::string id = "clear";
    std::string shape = "dome";
    float radius = 4.5f;
    float distance = 1.6f;
    float height = 3.0f;
    float bottom_y = 0.0f;
    float arc_degrees = 120.0f;
    int segments = 72;
    int vertical_segments = 16;
    float edge_darkening = 0.25f;
    std::vector<GradientStop> gradient_colors;
};

struct AttendSkyPresetConfig {
    std::string id;
    std::string label;
    AttendWallConfig wall{};
    AttendLightingConfig lighting{};
};

struct AttendOverlayButtonConfig {
    bool enabled = true;
    std::string anchor = "top_right";
    std::string label_prefix = "WEATHER: ";
    int width = 190;
    int height = 40;
    int margin_x = 0;
    int margin_y = 10;
    int padding_x = 14;
    int corner_radius = 10;
    int stroke_width = 2;
    int font_size = 18;
    Color4 fill{0.07f, 0.23f, 0.34f, 0.92f};
    Color4 stroke{0.90f, 0.97f, 1.0f, 0.95f};
    Color4 text{1.0f, 1.0f, 1.0f, 1.0f};
};

struct AttendCornerButtonConfig {
    bool enabled = true;
    std::string icon_path;
    float scale = 1.0f;
    float icon_scale = 0.62f;
    float icon_offset_x = 0.0f;
    float icon_offset_y = 0.0f;
    float side_extension_ratio = 0.38f;
    Color4 outer_border{1.0f, 1.0f, 1.0f, 1.0f};
    Color4 inner_border{0.44f, 0.74f, 0.93f, 1.0f};
    Color4 fill_top{0.63f, 0.86f, 1.0f, 1.0f};
    Color4 fill_bottom{0.33f, 0.72f, 1.0f, 1.0f};
};

struct AttendCornerButtonBehaviorConfig {
    float hide_after_pet_seconds = 0.5f;
    float return_delay_seconds = 0.35f;
    float slide_in_seconds = 0.16f;
    float slide_out_seconds = 0.18f;
};

struct AttendProfilePlateConfig {
    bool enabled = true;
    int width = 286;
    int height = 92;
    int margin_x = 24;
    int margin_y = 22;
    int padding_x = 16;
    int padding_y = 10;
    int corner_radius = 8;
    int outer_stroke_width = 0;
    int inner_stroke_width = 0;
    bool show_sprite = true;
    int sprite_size = 68;
    float sprite_scale = 1.0f;
    int sprite_gap = 10;
    int sprite_offset_x = 0;
    int sprite_offset_y = -6;
    int name_text_offset_x = 0;
    int name_text_offset_y = 0;
    int detail_text_offset_x = 0;
    int detail_text_offset_y = 0;
    int name_font_size = 47;
    int detail_font_size = 35;
    int name_row_height = 42;
    int detail_row_height = 42;
    int name_row_width = 360;
    int characteristic_row_width = 255;
    int nature_row_width = 236;
    int row_gap = 10;
    int left_fade_width = 46;
    int slide_out_x = 72;
    std::string characteristic = "Loves to eat";
    std::string nature = "Hardy";
    std::string font_path = "assets/fonts/power clear.ttf";
    Color4 outer_border{1.0f, 1.0f, 1.0f, 0.96f};
    Color4 inner_border{0.44f, 0.74f, 0.93f, 0.92f};
    Color4 fill_top{0.33f, 0.72f, 1.0f, 0.94f};
    Color4 fill_bottom{0.10f, 0.40f, 0.70f, 0.94f};
    Color4 divider{1.0f, 1.0f, 1.0f, 0.48f};
    Color4 name_text{1.0f, 1.0f, 1.0f, 1.0f};
    Color4 detail_text{0.86f, 0.96f, 1.0f, 0.96f};
};

struct AttendUiConfig {
    bool pixelated_overlay = true;
    std::string normal_render_mode = "hd";
    std::string debug_render_mode = "hd";
    AttendOverlayButtonConfig weather_button{};
    AttendOverlayButtonConfig view_button{};
    AttendOverlayButtonConfig pokemon_button{};
    AttendOverlayButtonConfig previous_pokemon_button = [] {
        AttendOverlayButtonConfig button;
        button.enabled = false;
        button.label_prefix = "PREVIOUS POKEMON";
        return button;
    }();
    AttendOverlayButtonConfig texture_variant_button{};
    AttendOverlayButtonConfig form_variant_button{};
    AttendOverlayButtonConfig sky_button{};
    AttendOverlayButtonConfig map_button = [] {
        AttendOverlayButtonConfig button;
        button.enabled = false;
        button.anchor = "top_left";
        button.label_prefix = "MAP: ";
        return button;
    }();
    AttendOverlayButtonConfig emote_button = [] {
        AttendOverlayButtonConfig button;
        button.enabled = false;
        button.anchor = "top_left";
        button.label_prefix = "EMOTE";
        return button;
    }();
    AttendOverlayButtonConfig sleep_button = [] {
        AttendOverlayButtonConfig button;
        button.enabled = false;
        button.anchor = "top_left";
        button.label_prefix = "SLEEP";
        return button;
    }();
    AttendOverlayButtonConfig cry_button = [] {
        AttendOverlayButtonConfig button;
        button.enabled = false;
        button.anchor = "top_left";
        button.label_prefix = "CRY";
        return button;
    }();
    AttendCornerButtonConfig action_button = [] {
        AttendCornerButtonConfig button;
        button.icon_path = "assets/pokemon_attend/ui/actions.png";
        return button;
    }();
    AttendCornerButtonConfig items_button = [] {
        AttendCornerButtonConfig button;
        button.icon_path = "assets/pokemon_attend/ui/items.png";
        return button;
    }();
    AttendCornerButtonConfig return_button = [] {
        AttendCornerButtonConfig button;
        button.icon_path = "assets/pokemon_attend/ui/return.png";
        return button;
    }();
    AttendCornerButtonBehaviorConfig corner_buttons{};
    AttendProfilePlateConfig profile_plate{};
    float hand_cursor_scale = 1.0f;
    float hand_cursor_hotspot_x_ratio = 0.5f;
    float hand_cursor_hotspot_y_ratio = 0.5f;
    float hand_cursor_pet_animation_speed = 1.0f;
};

struct AttendIdleBehaviorConfig {
    bool enabled = true;
    float emote_after_seconds = 30.0f;
    float emote_interval_min_seconds = 30.0f;
    float emote_interval_max_seconds = 75.0f;
    float sleep_after_seconds = 180.0f;
    std::string emote_animation_semantic = "emote_vocal";
    std::string sleep_animation_semantic = "sleep_loop";
    std::string emote_eye_expression = "normal_open";
    std::string emote_mouth_expression = "closed_normal";
    std::string sleep_eye_expression = "closed";
    std::string sleep_mouth_expression = "closed_narrow";
    float emote_duration_seconds = 0.0f;
    float sleep_duration_seconds = 3600.0f;
    float fade_in_seconds = 0.35f;
    float fade_out_seconds = 0.55f;
};

struct AttendAudioConfig {
    float pet_happy_cry_delay_seconds = 0.1f;
};

struct AttendSceneConfig {
    std::string id = "psyduck_default";
    PokemonAttendModelConfig pokemon{};
    AttendInteractionAdapterConfig interaction_adapter{};
    AttendIdleBehaviorConfig idle_behavior{};
    AttendCameraConfig camera{};
    AttendViewportLookConfig viewport_look{};
    AttendLightingConfig lighting{};
    AttendDepthOfFieldConfig depth_of_field{};
    AttendShadowConfig shadow{};
    AttendFloorConfig floor{};
    std::vector<AttendEnvironmentOption> environment_options;
    AttendWallConfig wall{};
    std::vector<AttendSkyPresetConfig> sky_presets;
    int active_sky = 0;
    AttendUiConfig ui{};
    AttendAudioConfig audio{};
    Color3 clear_color{0.08f, 0.12f, 0.18f};
};

struct AttendSceneLoadOverrides {
    std::string pokemon_id;
    std::string environment_id;
    std::string time_of_day;
    std::string weather;
};

AttendSceneConfig loadAttendSceneConfig(const std::string& project_root);
AttendSceneConfig loadAttendSceneConfig(
    const std::string& project_root,
    const AttendSceneLoadOverrides& overrides);

} // namespace pr::gameplay::attend
