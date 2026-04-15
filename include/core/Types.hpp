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
    std::vector<std::string> forward_keys{"M", "RETURN", "SPACE"};
    std::vector<std::string> back_keys{"N", "ESCAPE", "BACKSPACE"};
};

struct AudioConfig {
    std::string menu_music = "assets/title/menu_music.mp3";
    std::string button_sfx = "assets/title/btn.mp3";
    std::string rip_sfx = "assets/transfer_select_save/rip.mp3";
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

} // namespace pr
