#pragma once

#include "core/Types.hpp"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace pr {

enum class LoadingEase {
    Linear,
    EaseInCubic,
    EaseOutCubic,
    EaseInOutCubic
};

struct LoadingStageConfig {
    double start_seconds = 0.0;
    double duration_seconds = 0.5;
    LoadingEase ease = LoadingEase::EaseOutCubic;
};

struct ResortTransferWaveConfig {
    int bottom_offset = 120;
    Color color{74, 155, 156, 255};
    double amplitude = 10.0;
    double wavelength = 500.0;
    double horizontal_speed = 34.0;
    double phase = 0.0;
};

enum class TemporalLoadingDemoType {
    Pokeball,
    QuickBoatPass,
    BoatLoading
};

struct TemporalLoadingDemoConfig {
    TemporalLoadingDemoType loading_type = TemporalLoadingDemoType::QuickBoatPass;
    std::string message_key = "message_transport_pokemon";
    double simulated_load_duration_seconds = 0.0;
};

struct ResortTransferLoadingConfig {
    struct Assets {
        std::string boat = "assets/loading/boat.png";
        std::string cloud1 = "assets/loading/cloud1.png";
        std::string cloud2 = "assets/loading/cloud2.png";
    } assets;

    struct Palette {
        Color sky{248, 244, 232, 255};
        Color sun{253, 219, 153, 255};
        Color foam{255, 255, 255, 255};
        Color clouds{255, 255, 255, 255};
        Color border{255, 255, 255, 255};
    } colors;

    struct Timing {
        LoadingStageConfig intro_water_sun{0.0, 0.55, LoadingEase::EaseOutCubic};
        LoadingStageConfig intro_clouds{0.10, 0.70, LoadingEase::EaseOutCubic};
        LoadingStageConfig intro_boat{0.55, 0.45, LoadingEase::EaseInOutCubic};
        LoadingStageConfig outro_boat{0.0, 0.45, LoadingEase::EaseInCubic};
        LoadingStageConfig outro_water_sun{0.45, 0.55, LoadingEase::EaseInCubic};
        LoadingStageConfig outro_clouds{0.0, 1.0, LoadingEase::Linear};
    } timing;

    struct QuickPass {
        double duration_seconds = 1.65;
        LoadingEase ease = LoadingEase::EaseInOutCubic;
        double water_in_fraction = 0.25;
        double water_out_start = 0.72;
        double water_out_fraction = 0.28;
        double cloud_exit_start = 0.55;
        double cloud_exit_fraction = 0.45;
    } quick_pass;

    double minimum_loop_seconds = 1.0;

    struct Border {
        int inset = 25;
        int radius = 34;
        int stroke = 8;
    } border;

    struct Sun {
        int diameter = 512;
        double center_x_ratio = 0.5;
        int bottom_offset = 460;
        int offscreen_padding = 24;
    } sun;

    struct Boat {
        double center_x_ratio = 0.5;
        int bottom_offset = 220;
        double scale = 0.9;
        double bob_pixels = 5.0;
        double bob_seconds = 2.8;
        double moving_bob_pixels = 2.0;
    } boat;

    struct Clouds {
        int cloud1_bottom_offset = 570;
        int cloud2_bottom_offset = 480;
        int spacing = 600;
        double base_x_ratio = 0.42;
        double drift_speed = 42.0;
        double exit_speed = 190.0;
        // When clouds exit is synced to the boat outro, scales how far clouds move vs boat travel (1 = same distance).
        double exit_boat_distance_scale = 1.0;
    } clouds;

    struct Message {
        std::string font = "assets/fonts/loading_card.TTF";
        int font_size = 54;
        Color color{74, 155, 156, 255};
        double center_x_ratio = 0.32;
        int center_bottom_offset = 610;
        int line_spacing = 0;
        double max_width_ratio = 0.46;
        bool show_in_idle = false;
        LoadingStageConfig intro{0.62, 0.38, LoadingEase::EaseOutCubic};
        LoadingStageConfig outro{0.0, 0.22, LoadingEase::EaseInCubic};
        double enter_y_offset = 24.0;
        std::string default_key = "message_transport_pokemon";
        std::map<std::string, std::string> texts{
            {"message_transport_pokemon", "Transporting\nPokemon..."}
        };
    } message;

    struct Foam {
        Color color{255, 255, 255, 255};
        double y_offset_from_boat_bottom = 5.0;
        double overlap_with_hull = 7.0;
        double total_width_percent_of_boat_width = 0.62;
        double front_extension = 36.0;
        double back_extension = 78.0;
        double thickness = 24.0;
        double bow_splash_width = 74.0;
        double bow_splash_height = 18.0;
        int speed_crest_count = 3;
        double speed_crest_spacing = 24.0;
        double speed_crest_front_x_offset = 18.0;
        double speed_crest_front_extension = 18.0;
        double speed_crest_height = 34.0;
        std::vector<double> speed_crest_heights;
        // Vertical offset applied to the whole crest (legacy configs used speed_crest_angle_offset for this).
        double speed_crest_y_offset = 0.0;
        std::vector<double> speed_crest_y_offsets;
        double speed_crest_thickness = 7.0;
        double speed_crest_angle_offset = 15.0;
        std::vector<double> speed_crest_angle_offsets;
        // Angle (in degrees) skews the crest's shape without changing its path.
        double speed_crest_angle_degrees = 0.0;
        std::vector<double> speed_crest_angle_degrees_list;
        double speed_crest_front_pulse_amount = 0.06;
        double speed_crest_front_pulse_seconds = 0.85;
        // Independent crest scroll speed (pixels/sec). If 0, the renderer derives it from foam phase (legacy behavior).
        double speed_crest_scroll_speed = 0.0;
        int main_scallop_count = 6;
        double front_scallop_amplitude = 7.5;
        double back_scallop_amplitude = 2.0;
        double front_scallop_wavelength = 46.0;
        double back_scallop_wavelength = 92.0;
        double scroll_speed = 92.0;
        double velocity_influence = 0.06;
        // Add (boat center X * this) to foam/crest phase so ripples track world motion while the hull moves.
        // Legacy JSON key: foam_outro_world_phase_coupling. Set 0 to disable.
        double foam_world_phase_coupling = 1.0;
        double trailing_stretch_amount = 120.0;
        double stern_taper = 0.34;
        int segments = 96;
    } foam;

    struct Waves {
        int segments = 64;
        double sink_padding = 120.0;
        std::array<ResortTransferWaveConfig, 3> layers{
            ResortTransferWaveConfig{255, Color{139, 195, 190, 255}, 8.0, 360.0, 46.0, 40.0},
            ResortTransferWaveConfig{180, Color{103, 174, 173, 255}, 10.0, 430.0, 34.0, 130.0},
            ResortTransferWaveConfig{120, Color{74, 155, 156, 255}, 12.0, 500.0, 26.0, 260.0}
        };
    } waves;

    std::map<std::string, TemporalLoadingDemoConfig> temporal{
        {"trade_button", TemporalLoadingDemoConfig{}}
    };
};

ResortTransferLoadingConfig loadResortTransferLoadingConfig(const std::string& project_root);
double applyLoadingEase(LoadingEase ease, double value);
double loadingStageProgress(const LoadingStageConfig& stage, double elapsed_seconds);
double loadingSequenceDuration(const LoadingStageConfig& a, const LoadingStageConfig& b, const LoadingStageConfig& c);

} // namespace pr
