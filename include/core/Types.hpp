#pragma once

#include <array>
#include <string>
#include <vector>

namespace pr {

struct Color {
    int r = 255;
    int g = 255;
    int b = 255;
    int a = 255;
};

struct Point {
    int x = 0;
    int y = 0;
};

struct WindowConfig {
    int width = 1280;
    int height = 800;
    int virtual_width = 1280;
    int virtual_height = 800;
    int design_width = 1280;
    int design_height = 800;
    std::string title = "Pokemon Resort - Title Screen";
};

struct AssetPaths {
    std::string background_a = "assets/title/background_a.png";
    std::string background_b = "assets/title/background_b.png";
    std::string button_main = "assets/title/btn_main.png";
    std::string logo_splash = "assets/title/logo_splash.png";
    std::string logo_main = "assets/title/logo_main.png";
    std::string logo_main_mask = "";
    std::string font = "assets/fonts/Arial.ttf";
};

struct TimingConfig {
    double splash_fade_in = 0.55;
    double splash_hold = 0.18;
    double splash_fade_out = 0.55;
    double main_logo_on_black = 0.22;
    double white_flash = 0.10;
    double title_hold_before_prompt = 1.80;
    double start_transition = 0.60;
};

struct PromptConfig {
    std::string text = "PRESS START";
    int center_x = 640;
    int baseline_y = 754;
    int font_pt_size = 75;
    Color color{160, 160, 160, 255};
    double blink_cycle_seconds = 1.6;
};

struct LayoutConfig {
    Point splash_logo_center{640, 400};
    Point main_logo_center{640, 400};
    Point background_a_top_left{0, 0};
    Point background_b_top_left{0, 0};
};

struct TransitionConfig {
    int main_logo_end_y = -267;
    int background_a_end_y = -800;
    double background_a_speed_scale = 1.0;
    double main_logo_speed_scale = 1.15;
    bool fade_prompt_out = true;
};

struct MenuAnimationConfig {
    double intro_duration = 0.45;
    double outro_duration = 0.35;
    int slide_distance = 525;
    double bounce = 1.70158;
};

struct MenuSelectionConfig {
    double beat_speed = 4.5;
    double beat_magnitude = 0.08;
};

struct MenuSectionTransitionConfig {
    double button_out_duration = 0.30;
    double fade_duration = 0.25;
};

struct MenuConfig {
    std::array<std::string, 4> items{
        "GO TO RESORT",
        "TRANSFER",
        "TRADE",
        "OPTIONS"
    };
    int center_x = 640;
    int top_y = 42;
    int vertical_spacing = 42;
    int font_pt_size = 44;
    Color text_color{255, 255, 255, 255};
    MenuAnimationConfig animation;
    MenuSelectionConfig selection;
    MenuSectionTransitionConfig section_transition;
};

struct ShineConfig {
    bool enabled = true;
    double delay_seconds = 0.20;
    double duration_seconds = 0.65;
    int repeat_count = 1;
    double gap_seconds = 0.35;
    int band_width = 104;
    int max_alpha = 170;
    int travel_padding = 125;
    bool use_additive_blend = true;
};

struct InputConfig {
    bool accept_any_key = true;
    bool accept_mouse = true;
    bool accept_controller = true;
    std::vector<std::string> navigate_up_keys{"UP", "W"};
    std::vector<std::string> navigate_down_keys{"DOWN", "S"};
    std::vector<std::string> navigate_left_keys{"LEFT", "A"};
    std::vector<std::string> navigate_right_keys{"RIGHT", "D"};
    std::vector<std::string> forward_keys{"M", "RETURN", "SPACE"};
    std::vector<std::string> back_keys{"N", "ESCAPE", "BACKSPACE"};
};

struct AudioConfig {
    /// Paths are relative to the project root (joined at runtime).
    std::string menu_music = "assets/music/ui_menu_music.mp3";
    std::string button_sfx = "assets/sfx/btn.mp3";
    std::string rip_sfx = "assets/sfx/rip.mp3";
    std::string ui_move_sfx = "assets/sfx/ui_move.mp3";
    std::string pickup_sfx = "assets/sfx/pickup.mp3";
    std::string putdown_sfx = "assets/sfx/putdown.mp3";
    int music_volume = 7;
    int sfx_volume = 8;
};

struct PersistenceConfig {
    bool save_options = true;
    std::string organization = "VantaStudio";
    std::string application = "PokemonResort";
    std::string save_file_name = "pokemon_resort.sav";
    std::string backup_file_name = "pokemon_resort.sav.bak";
};

struct UserSettings {
    int text_speed_index = 2;
    int music_volume = 7;
    int sfx_volume = 8;
};

struct SaveData {
    int version = 1;
    UserSettings options;
};

struct SkipConfig {
    bool splash_fade_in = true;
    bool splash_hold = true;
    bool splash_fade_out = true;
    bool main_logo_on_black = true;
    bool white_flash = true;
    bool title_hold = true;
    bool waiting_for_start = false;
    bool start_transition = false;
    bool main_menu_intro = false;
    bool main_menu_idle = false;
};

struct TitleScreenConfig {
    WindowConfig window;
    AssetPaths assets;
    TimingConfig timings;
    PromptConfig prompt;
    LayoutConfig layout;
    TransitionConfig transition;
    MenuConfig menu;
    ShineConfig shine;
    InputConfig input;
    AudioConfig audio;
    PersistenceConfig persistence;
    SkipConfig skip;
};

struct AppConfig {
    WindowConfig window;
    InputConfig input;
    AudioConfig audio;
};

/// Authoring for `BoxViewport` (`config/game_transfer.json` key `box_viewport`).
struct GameTransferBoxViewportStyle {
    /// Path relative to project root; PNG points left (26×48); right/down use rotation.
    std::string arrow_texture = "assets/game_transfer/arrow.png";
    /// Applied via `SDL_SetTextureColorMod` so the asset reads at `#FBFBFB` on the gray chrome.
    Color arrow_mod_color{251, 251, 251, 255};
    int box_name_font_pt = 36;
    Color box_name_color{26, 26, 26, 255};
    int box_space_font_pt = 22;
    Color box_space_color{26, 26, 26, 255};
    /// Vertical nudge for the footer scroll chevron only (negative moves up). Pill L/R arrows unchanged.
    int footer_scroll_arrow_offset_y = -6;
    /// Exponential approach for per-box content slide (higher = snappier).
    double content_slide_smoothing = 18.0;
    /// Desired sprite magnification inside slots (2.0 = classic crisp look); clamped by slot bounds.
    double sprite_scale = 2.0;
    /// Vertical offset applied to sprite center within each slot (positive moves down).
    int sprite_offset_y = 0;

    // --- Item tool overlays ---
    int item_tool_item_size = 42;
    double item_tool_grow_smoothing = 18.0;
    Color item_tool_sprite_mod_color{150, 150, 150, 128};

    // --- Box Space grid sprites (full/empty/noempty) ---
    // Separate from Pokémon sprite sizing so the box icons can be tuned independently.
    double box_space_sprite_scale = 2.0;
    int box_space_sprite_offset_x = 0;
    int box_space_sprite_offset_y = 0;
};

/// Small “mini preview” box that slides in to preview hovered boxes.
struct GameTransferMiniPreviewStyle {
    bool enabled = true;
    int width = 240;
    int height = 200;
    int corner_radius = 14;
    int border_thickness = 6;
    int offset_y = 110;
    int edge_pad = 18;
    double enter_smoothing = 18.0;
    double sprite_scale = 1.2;
};

/// Box Space-only long press interactions (`config/game_transfer.json` → `box_space_long_press`).
struct GameTransferBoxSpaceLongPressStyle {
    /// Long press duration (seconds) to arm "swap PC boxes" while in Box Space mode and not holding a Pokémon.
    double box_swap_hold_seconds = 3.0;
    /// Long press duration (seconds) to drop held Pokémon into the first empty slot of the focused box while in Box Space mode.
    double quick_drop_hold_seconds = 2.0;
};

struct GameTransferPokemonActionMenuStyle {
    int width = 260;
    int row_height = 52;
    int padding_y = 12;
    int gap_from_slot = 14;
    int corner_radius = 12;
    int border_thickness = 4;
    int font_pt = 30;
    double grow_smoothing = 40.0;
    Color background_color{251, 251, 251, 246};
    Color border_color{220, 50, 50, 255};
    Color selected_row_color{191, 191, 191, 150};
    Color text_color{17, 17, 17, 255};
    bool dim_background_sprites = false;
    Color dim_sprite_mod_color{150, 150, 150, 128};
    bool modal_move_swaps_into_hand = false;
    bool swap_tool_swaps_into_hand = true;
    bool held_sprite_shadow_enabled = true;
    Color held_sprite_shadow_color{0, 0, 0, 90};
    int held_sprite_shadow_offset_y = 24;
    int held_sprite_shadow_width = 58;
    int held_sprite_shadow_height = 18;
    /// Applied on top of `box_viewport.sprite_scale` for held Pokémon (1.0 = same as in-box slots).
    double held_sprite_scale_multiplier = 1.0;
};

struct GameTransferInfoBannerFieldStyle {
    std::string field;
    std::string kind = "text";
    std::string label;
    std::string empty_text = "-";
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    std::string flow_group;
    int flow_gap = 2;
    int font_pt = 22;
    Color color{26, 26, 26, 255};
    int label_font_pt = 16;
    Color label_color{76, 76, 76, 255};
    std::vector<std::string> contexts{"pokemon", "empty"};
};

struct GameTransferInfoBannerStyle {
    bool enabled = true;
    int separator_height = 4;
    int info_height = 75;
    Color separator_color{191, 191, 191, 255};
    Color info_background_color{224, 224, 224, 255};
    std::string icon_directory = "assets/game_transfer/pokemon_icons";
    std::string game_icon_directory = "assets/game_transfer/pokemon_icons/game_icons";
    std::string unknown_icon = "assets/game_transfer/pokemon_icons/unknown.png";
    int text_font_pt = 22;
    Color text_color{26, 26, 26, 255};
    int label_font_pt = 16;
    Color label_color{76, 76, 76, 255};
    Color gender_symbol_male_color{140, 203, 255, 255};
    Color gender_symbol_female_color{255, 90, 90, 255};
    int gender_symbol_font_pt = 0;
    int gender_symbol_x_adjust = 0;
    int gender_symbol_y_adjust = 0;
    std::string tool_multiple_title = "Multiple";
    std::string tool_multiple_body = "Move several Pokemon in one trip.";
    std::string tool_basic_title = "Basic";
    std::string tool_basic_body = "Move one Pokemon at a time with the standard flow.";
    std::string tool_swap_title = "Swap";
    std::string tool_swap_body = "Exchange two Pokemon between boxes.";
    std::string tool_items_title = "Items";
    std::string tool_items_body = "Browse held-item and bag features.";
    std::string pill_pokemon_title = "Pokemon View";
    std::string pill_pokemon_body = "Show Pokemon boxes and transfer details.";
    std::string pill_items_title = "Items View";
    std::string pill_items_body = "Switch the transfer screen over to item tools.";
    std::string box_space_title = "Box Space";
    std::string box_space_body = "Browse every PC box at once, then open the box you want to manage.";
    std::vector<GameTransferInfoBannerFieldStyle> fields{};
};

/// Browser-style Pokémon / Items pill (`config/game_transfer.json` → `pill_toggle`). Default aligns with the **right** box column.
struct GameTransferPillToggleStyle {
    int track_width = 530;
    int track_height = 77;
    int pill_width = 270;
    int pill_height = 64;
    /// Inset from the track edges for the sliding pill (match vertical padding: e.g. (track_height - pill_height) / 2).
    int pill_inset = 6;
    /// Pixels between the track bottom and the box tops (both columns share y = 100).
    int gap_above_boxes = 10;
    Color track_color{224, 224, 224, 255};
    Color pill_color{251, 251, 251, 255};
    int font_pt = 28;
    /// Exponential approach rate for the pill slider (higher = snappier; ~14–28 feels good).
    double toggle_smoothing = 22.0;
    /// Exponential approach for box panel slide (slightly lower = heavier than the pill).
    double box_smoothing = 13.0;
};

/// Infinite tool carousel above the left transfer column (`config/game_transfer.json` → `tool_carousel`).
struct GameTransferToolCarouselStyle {
    int viewport_width = 240;
    int viewport_height = 76;
    /// Gap from the left edge of the virtual screen to the carousel viewport.
    int offset_from_left_wall = 50;
    /// Y when the Pokémon panels are fully revealed (slides with `panels_reveal`).
    int rest_y = 12;
    /// Y when the carousel is fully hidden (slides up with Items mode / off-screen start).
    int hidden_y = -96;
    int viewport_corner_radius = 12;
    /// Extra inset for icon clipping inside the rounded panel (px); 0 = derived from corner radius.
    int viewport_clip_inset = 0;
    Color viewport_color{224, 224, 224, 255};
    int icon_size = 62;
    /// Outer size of the hollow rounded selector (`selector_size` JSON alias).
    int selection_frame_size = 66;
    /// Selector border thickness in pixels (`selector_thickness` JSON alias).
    int selection_stroke = 5;
    /// Rounded corners for the selector ring; 0 uses `viewport_corner_radius` (clamped).
    int selector_corner_radius = 0;
    /// Horizontal travel (px) for one carousel step; 0 derives from adjacent slot centers.
    int slide_span_pixels = 0;
    /// Horizontal spacing between belt icons (px); 0 derives from `slot_center_*`.
    /// If set, it also becomes the default for `slide_span_pixels` when that is 0.
    int belt_spacing_pixels = 0;
    /// Exponential approach for horizontal tool slide (higher = snappier; ~16–26).
    double slide_smoothing = 20.0;
    /// Horizontal centers of the three slots within the viewport (left / selected / right).
    int slot_center_left = 52;
    int slot_center_middle = 120;
    int slot_center_right = 188;
    /// Order: multiple, basic, swap, items — default selection index 1 (`basic`).
    std::string texture_multiple = "assets/game_transfer/icon_multiple.png";
    std::string texture_basic = "assets/game_transfer/icon_basic.png";
    std::string texture_swap = "assets/game_transfer/icon_swap.png";
    std::string texture_items = "assets/game_transfer/icon_items.png";
    Color frame_multiple{34, 177, 76, 255};
    Color frame_basic{220, 50, 47, 255};
    Color frame_swap{52, 120, 246, 255};
    Color frame_items{245, 200, 66, 255};
};

/// PC box name dropdown under the external-save name plate (`game_transfer.json` → `box_name_dropdown`).
struct GameTransferBoxNameDropdownStyle {
    bool enabled = true;
    /// Horizontal size matches name pill when set equal to box viewport layout (default 360).
    int panel_width_pixels = 360;
    /// Max list height ≈ this × `reference_name_plate_height_pixels`, clamped to screen space below.
    float max_height_multiplier = 4.f;
    int reference_name_plate_height_pixels = 70;
    int item_font_pt = 22;
    /// Extra vertical space above/below each label within a row.
    int row_padding_y = 8;
    int panel_corner_radius = 14;
    int panel_border_thickness = 1;
    Color panel_color{251, 251, 251, 255};
    Color panel_border_color{210, 210, 210, 255};
    Color item_text_color{26, 26, 26, 255};
    /// Semi-transparent overlay on the keyboard/hovered row.
    Color selected_row_tint{130, 130, 130, 95};
    double open_smoothing = 22.0;
    double close_smoothing = 28.0;
    /// Minimum gap between panel bottom and the virtual screen bottom (avoids banner overlap).
    int bottom_margin_pixels = 48;
    /// Scales vertical drag delta → scroll offset.
    double scroll_drag_multiplier = 1.0;
};

/// Callout bubble for Pokémon slots and game icons (`game_transfer.json` → `selection_cursor.speech_bubble`).
struct GameTransferSpeechBubbleCursorStyle {
    bool enabled = true;
    int font_pt = 22;
    Color text_color{26, 26, 26, 255};
    Color fill_color{251, 251, 251, 255};
    int border_thickness = 5;
    /// Capsule corner radius (clamped to half pill height).
    int corner_radius = 35;
    int padding_x = 16;
    int padding_y = 10;
    int min_width = 100;
    int max_width = 520;
    int min_height = 50;
    /// Used when there is no label (empty slot with empty `empty_slot_label`).
    int empty_min_width = 52;
    int empty_min_height = 44;
    /// Full width of the triangle base where it meets the pill bottom (before clamping to the flat segment).
    int triangle_base_width = 56;
    int triangle_height = 16;
    /// Gap between callout tip and the top of the focused target rect.
    int gap_above_target = 4;
    int screen_margin = 14;
    /// Shown when the footer resort icon is focused (left column).
    std::string resort_game_title = "Pokemon Resort";
    /// `{name}` prefers nickname when present, then species; `{nickname}`, `{species}`, and `{level}` are also supported.
    std::string pokemon_label_format = "{name} (Lv. {level})";
    int default_pokemon_level = 100;
    /// Label for empty PC slots (both columns).
    std::string empty_slot_label = "";
};

/// Focus/selection cursor for controller navigation (`config/game_transfer.json` → `selection_cursor`).
struct GameTransferSelectionCursorStyle {
    bool enabled = true;
    Color color{244, 205, 72, 255};
    int alpha = 230;
    int thickness = 4;
    int padding = 2;
    /// Outer highlight is at least this wide/tall (before outline stroke), centered on the focus bounds.
    int min_width = 45;
    int min_height = 45;
    int corner_radius = 12;
    double beat_speed = 2.2;
    double beat_magnitude = 2.0;
    GameTransferSpeechBubbleCursorStyle speech_bubble{};
};

} // namespace pr
