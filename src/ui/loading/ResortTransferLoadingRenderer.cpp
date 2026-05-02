#include "ui/loading/ResortTransferLoadingRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace pr {
namespace {

constexpr double kPi = 3.14159265358979323846;

// C++ `std::fmod` keeps the sign of the dividend; foam phase may be negative when
// `scroll_speed` is negative, so we need a remainder in [0, m).
double positiveMod(double x, double m) {
    if (!(m > 0.0)) {
        return 0.0;
    }
    double r = std::fmod(x, m);
    if (r < 0.0) {
        r += m;
    }
    return r;
}

double lerp(double from, double to, double t) {
    return from + (to - from) * std::clamp(t, 0.0, 1.0);
}

double smoothstep(double edge0, double edge1, double value) {
    const double t = std::clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

void setDrawColor(SDL_Renderer* renderer, const Color& color) {
    SDL_SetRenderDrawColor(renderer, static_cast<Uint8>(color.r), static_cast<Uint8>(color.g),
                           static_cast<Uint8>(color.b), static_cast<Uint8>(color.a));
}

std::pair<int, int> roundedSpanFor(int yy, int x, int y, int w, int h, int radius) {
    int x0 = x;
    int x1 = x + w;
    if (radius <= 0) return {x0, x1};
    if (yy < y + radius) {
        const int dy = yy - (y + radius);
        const int disc = radius * radius - dy * dy;
        if (disc >= 0) {
            const int s = static_cast<int>(std::lround(std::sqrt(static_cast<double>(disc))));
            x0 = x + radius - s;
            x1 = x + w - radius + s;
        }
    } else if (yy >= y + h - radius) {
        const int dy = yy - (y + h - radius);
        const int disc = radius * radius - dy * dy;
        if (disc >= 0) {
            const int s = static_cast<int>(std::lround(std::sqrt(static_cast<double>(disc))));
            x0 = x + radius - s;
            x1 = x + w - radius + s;
        }
    }
    return {x0, x1};
}

void drawRoundedFrameMask(SDL_Renderer* renderer, int viewport_w, int viewport_h, int inset, int radius, const Color& c) {
    if (viewport_w <= 0 || viewport_h <= 0) return;
    inset = std::clamp(inset, 0, std::min(viewport_w, viewport_h) / 2);
    const int x = inset;
    const int y = inset;
    const int w = viewport_w - inset * 2;
    const int h = viewport_h - inset * 2;
    if (w <= 0 || h <= 0) {
        setDrawColor(renderer, c);
        SDL_Rect full{0, 0, viewport_w, viewport_h};
        SDL_RenderFillRect(renderer, &full);
        return;
    }
    radius = std::clamp(radius, 0, std::min(w, h) / 2);
    setDrawColor(renderer, c);

    for (int yy = 0; yy < viewport_h; ++yy) {
        if (yy < y || yy >= y + h) {
            SDL_Rect row{0, yy, viewport_w, 1};
            SDL_RenderFillRect(renderer, &row);
            continue;
        }

        const auto content = roundedSpanFor(yy, x, y, w, h, radius);
        if (content.first > 0) {
            SDL_Rect left{0, yy, content.first, 1};
            SDL_RenderFillRect(renderer, &left);
        }
        if (content.second < viewport_w) {
            SDL_Rect right{content.second, yy, viewport_w - content.second, 1};
            SDL_RenderFillRect(renderer, &right);
        }
    }
}

void fillCircle(SDL_Renderer* renderer, int cx, int cy, int radius, const Color& color) {
    if (radius <= 0) return;
    setDrawColor(renderer, color);
    for (int dy = -radius; dy <= radius; ++dy) {
        const int half = static_cast<int>(std::lround(std::sqrt(static_cast<double>(radius * radius - dy * dy))));
        SDL_Rect row{cx - half, cy + dy, half * 2 + 1, 1};
        SDL_RenderFillRect(renderer, &row);
    }
}

void drawTexture(SDL_Renderer* renderer, const TextureHandle& texture, const SDL_Rect& dst, const Color& mod) {
    if (!texture.texture || dst.w <= 0 || dst.h <= 0) return;
    SDL_SetTextureBlendMode(texture.texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture.texture.get(), static_cast<Uint8>(mod.a));
    SDL_SetTextureColorMod(texture.texture.get(), static_cast<Uint8>(mod.r), static_cast<Uint8>(mod.g), static_cast<Uint8>(mod.b));
    SDL_RenderCopy(renderer, texture.texture.get(), nullptr, &dst);
}

} // namespace

ResortTransferLoadingRenderer::ResortTransferLoadingRenderer(
    ResortTransferLoadingConfig config,
    ResortTransferLoadingTextures textures)
    : config_(std::move(config)),
      textures_(std::move(textures)) {}

void ResortTransferLoadingRenderer::render(SDL_Renderer* renderer, const ResortTransferLoadingFrame& frame) const {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    setDrawColor(renderer, config_.colors.sky);
    SDL_RenderClear(renderer);

    if (!frame.idle) {
        drawSun(renderer, frame);
        drawClouds(renderer, frame);
        drawMessage(renderer, frame);
        drawWave(renderer, frame, 0);
        const BoatPlacement boat = boatPlacement(frame);
        drawBoat(renderer, boat);
        drawFoam(renderer, frame, boat);
        drawWave(renderer, frame, 1);
        drawWave(renderer, frame, 2);
    } else if (config_.message.show_in_idle) {
        drawMessage(renderer, frame);
    }
    drawBorder(renderer, frame.viewport_w, frame.viewport_h);
}

void ResortTransferLoadingRenderer::drawBorder(SDL_Renderer* renderer, int w, int h) const {
    drawRoundedFrameMask(renderer, w, h, config_.border.inset, config_.border.radius, config_.colors.border);
}

void ResortTransferLoadingRenderer::drawSun(SDL_Renderer* renderer, const ResortTransferLoadingFrame& frame) const {
    const double target_y = static_cast<double>(frame.viewport_h - config_.sun.bottom_offset);
    const double offscreen_y = -static_cast<double>(config_.sun.diameter + config_.sun.offscreen_padding);
    const double y = lerp(offscreen_y, target_y, frame.water_sun);
    fillCircle(renderer, static_cast<int>(std::lround(frame.viewport_w * config_.sun.center_x_ratio)),
               static_cast<int>(std::lround(y)), config_.sun.diameter / 2, config_.colors.sun);
}

void ResortTransferLoadingRenderer::drawClouds(SDL_Renderer* renderer, const ResortTransferLoadingFrame& frame) const {
    const int spacing = std::max(1, config_.clouds.spacing);
    const double offscreen = static_cast<double>(frame.viewport_w + spacing + textures_.cloud1.width);
    const double enter_offset = (1.0 - frame.clouds_enter) * offscreen;
    const double cycle_width = static_cast<double>(spacing * 2);
    const double looped_drift = std::fmod(frame.cloud_drift, cycle_width);
    const double base_x_without_exit = frame.viewport_w * config_.clouds.base_x_ratio - looped_drift + enter_offset;
    const double legacy_exit_distance = std::max(
        static_cast<double>(frame.viewport_w + spacing + textures_.cloud1.width),
        config_.clouds.exit_speed * std::max(0.01, config_.timing.outro_clouds.duration_seconds));
    const double scaled_boat_w = static_cast<double>(textures_.boat.width) * config_.boat.scale;
    const double target_x = static_cast<double>(frame.viewport_w) * config_.boat.center_x_ratio;
    const double foam_tail_clearance = std::max(
        0.0,
        static_cast<double>(config_.border.inset) +
            scaled_boat_w * (config_.foam.total_width_percent_of_boat_width - 0.36) +
            config_.foam.back_extension +
            config_.foam.trailing_stretch_amount -
            scaled_boat_w * 0.5);
    const double boat_exit_travel_pixels =
        std::max(1.0, static_cast<double>(frame.viewport_w) + scaled_boat_w * 0.5 + foam_tail_clearance - target_x);
    const double rightmost_repeated_cloud =
        base_x_without_exit + cycle_width * 2.0 + spacing +
        static_cast<double>(std::max(textures_.cloud1.width, textures_.cloud2.width));
    const double cloud_clear_distance = std::max(legacy_exit_distance, rightmost_repeated_cloud + 1.0);
    const double exit_distance = frame.boat_exit > 0.0
        ? std::max(boat_exit_travel_pixels * config_.clouds.exit_boat_distance_scale, cloud_clear_distance)
        : legacy_exit_distance;
    const double exit_offset = frame.clouds_exit * exit_distance;
    const double base_x = base_x_without_exit - exit_offset;

    for (int i = -1; i <= 2; ++i) {
        const double x1 = base_x + static_cast<double>(i * spacing * 2);
        const double x2 = x1 + spacing;
        SDL_Rect c1{static_cast<int>(std::lround(x1)), frame.viewport_h - config_.clouds.cloud1_bottom_offset,
                    textures_.cloud1.width, textures_.cloud1.height};
        SDL_Rect c2{static_cast<int>(std::lround(x2)), frame.viewport_h - config_.clouds.cloud2_bottom_offset,
                    textures_.cloud2.width, textures_.cloud2.height};
        drawTexture(renderer, textures_.cloud1, c1, config_.colors.clouds);
        drawTexture(renderer, textures_.cloud2, c2, config_.colors.clouds);
    }
}

void ResortTransferLoadingRenderer::drawMessage(SDL_Renderer* renderer, const ResortTransferLoadingFrame& frame) const {
    if (!textures_.message.texture || textures_.message.width <= 0 || textures_.message.height <= 0) {
        return;
    }
    const double alpha = std::clamp(frame.message_alpha, 0.0, 1.0);
    if (alpha <= 0.0) {
        return;
    }
    const double max_width = std::max(1.0, static_cast<double>(frame.viewport_w) * config_.message.max_width_ratio);
    const double scale = std::min(1.0, max_width / static_cast<double>(textures_.message.width));
    const int width = static_cast<int>(std::lround(static_cast<double>(textures_.message.width) * scale));
    const int height = static_cast<int>(std::lround(static_cast<double>(textures_.message.height) * scale));
    const int center_x = static_cast<int>(std::lround(static_cast<double>(frame.viewport_w) * config_.message.center_x_ratio));
    const int center_y = static_cast<int>(std::lround(
        static_cast<double>(frame.viewport_h - config_.message.center_bottom_offset) + frame.message_y_offset));
    SDL_Rect dst{center_x - width / 2, center_y - height / 2, width, height};
    drawTexture(renderer, textures_.message, dst, Color{255, 255, 255, static_cast<int>(std::lround(alpha * 255.0))});
}

void ResortTransferLoadingRenderer::drawWave(SDL_Renderer* renderer, const ResortTransferLoadingFrame& frame, int index) const {
    const auto& wave = config_.waves.layers[static_cast<std::size_t>(index)];
    const double target_y = static_cast<double>(frame.viewport_h - wave.bottom_offset);
    const double avg_y = lerp(static_cast<double>(frame.viewport_h) + config_.waves.sink_padding, target_y, frame.water_sun);
    const int segments = std::max(8, config_.waves.segments);
    const int step = std::max(1, frame.viewport_w / segments);
    const double phase = wave.phase + frame.loop_time * wave.horizontal_speed;
    setDrawColor(renderer, wave.color);

    for (int x = -step; x < frame.viewport_w + step; x += step) {
        const int next_x = std::min(frame.viewport_w, x + step);
        const double mid_x = static_cast<double>(x + next_x) * 0.5;
        const double top_y = avg_y + std::sin((mid_x + phase) * 2.0 * kPi / wave.wavelength) * wave.amplitude;
        const int top = static_cast<int>(std::lround(top_y));
        if (frame.viewport_h - top + 4 > 0) {
            SDL_Rect strip{std::max(0, x), top, std::max(1, next_x - std::max(0, x)), frame.viewport_h - top + 4};
            SDL_RenderFillRect(renderer, &strip);
        }
    }
}

ResortTransferLoadingRenderer::BoatPlacement ResortTransferLoadingRenderer::boatPlacement(const ResortTransferLoadingFrame& frame) const {
    const double scaled_w = textures_.boat.width * config_.boat.scale;
    const double scaled_h = textures_.boat.height * config_.boat.scale;
    const double target_x = frame.viewport_w * config_.boat.center_x_ratio;
    const double foam_tail_clearance = std::max(
        0.0,
        static_cast<double>(config_.border.inset) +
            scaled_w * (config_.foam.total_width_percent_of_boat_width - 0.36) +
            config_.foam.back_extension +
            config_.foam.trailing_stretch_amount -
            scaled_w * 0.5);
    const double enter_x = lerp(-scaled_w * 0.5, target_x, frame.boat_enter);
    const double exit_x = lerp(
        target_x,
        static_cast<double>(frame.viewport_w) + scaled_w * 0.5 + foam_tail_clearance,
        frame.boat_exit);
    const double center_x = frame.use_boat_center_x ? frame.boat_center_x : (frame.boat_exit > 0.0 ? exit_x : enter_x);
    const double bob_amp = frame.boat_enter >= 1.0 && frame.boat_exit <= 0.0 ? config_.boat.bob_pixels : config_.boat.moving_bob_pixels;
    const double bob = std::sin(frame.loop_time * 2.0 * kPi / config_.boat.bob_seconds) * bob_amp;
    return {center_x, static_cast<double>(frame.viewport_h - config_.boat.bottom_offset) + bob, scaled_w, scaled_h};
}

void ResortTransferLoadingRenderer::drawBoat(SDL_Renderer* renderer, const BoatPlacement& boat) const {
    SDL_Rect dst{static_cast<int>(std::lround(boat.center_x - boat.width * 0.5)),
                 static_cast<int>(std::lround(boat.bottom_y - boat.height)),
                 static_cast<int>(std::lround(boat.width)),
                 static_cast<int>(std::lround(boat.height))};
    drawTexture(renderer, textures_.boat, dst, Color{255, 255, 255, 255});
}

void ResortTransferLoadingRenderer::drawFoam(SDL_Renderer* renderer, const ResortTransferLoadingFrame& frame, const BoatPlacement& boat) const {
    const double world_scroll = boat.center_x * config_.foam.foam_world_phase_coupling;
    const double trail = frame.boat_exit * config_.foam.trailing_stretch_amount;
    const double front_x = boat.center_x + boat.width * 0.36 + config_.foam.front_extension;
    const double back_x = front_x -
        boat.width * config_.foam.total_width_percent_of_boat_width -
        config_.foam.front_extension -
        config_.foam.back_extension -
        trail;
    const double width = std::max(1.0, front_x - back_x);
    if (back_x > static_cast<double>(frame.viewport_w + config_.border.inset) || front_x < 0.0) {
        return;
    }
    const double baseline_y = boat.bottom_y +
        config_.foam.y_offset_from_boat_bottom -
        config_.foam.overlap_with_hull;
    const int segments = std::max(8, config_.foam.segments);
    const double bow_t = std::clamp(config_.foam.bow_splash_width / width, 0.04, 0.45);
    const double scallop_count = std::max(1, config_.foam.main_scallop_count);
    const double front_wavelength = std::max(1.0, config_.foam.front_scallop_wavelength);
    const double back_wavelength = std::max(1.0, config_.foam.back_scallop_wavelength);
    const double wavelength_delta = back_wavelength - front_wavelength;
    const auto accumulated_cycles = [&](double distance) {
        distance = std::clamp(distance, 0.0, width);
        double integral = 0.0;
        double total_integral = 1.0;
        if (std::abs(wavelength_delta) < 0.001) {
            integral = distance / front_wavelength;
            total_integral = width / front_wavelength;
        } else {
            const double at_distance = front_wavelength + wavelength_delta * distance / width;
            integral = width / wavelength_delta * std::log(at_distance / front_wavelength);
            total_integral = width / wavelength_delta * std::log(back_wavelength / front_wavelength);
        }
        return total_integral > 0.0 ? integral / total_integral * scallop_count : 0.0;
    };
    setDrawColor(renderer, config_.foam.color);

    for (int i = 0; i < segments; ++i) {
        const double t0 = static_cast<double>(i) / segments;
        const double t1 = static_cast<double>(i + 1) / segments;
        const double mid_t = (t0 + t1) * 0.5;
        const double x0 = front_x - width * t0;
        const double x1 = front_x - width * t1;
        const double stern_taper = 1.0 - (1.0 - config_.foam.stern_taper) * smoothstep(0.62, 1.0, mid_t);
        const double amplitude = lerp(config_.foam.front_scallop_amplitude, config_.foam.back_scallop_amplitude, mid_t);
        const double distance_from_bow = width * mid_t;
        const double phase_distance = positiveMod(frame.foam_phase + world_scroll, width);
        const double sampled_distance = positiveMod(distance_from_bow + phase_distance, width);
        const double phase_cycles = accumulated_cycles(sampled_distance);
        const double scallop = 0.5 - 0.5 * std::cos(2.0 * kPi * phase_cycles);
        const double bow_splash = mid_t <= bow_t
            ? config_.foam.bow_splash_height * (0.5 + 0.5 * std::cos(kPi * mid_t / bow_t))
            : 0.0;
        const double front_curve = smoothstep(0.0, bow_t * 0.95, mid_t);
        const double bow_point_y = baseline_y - config_.foam.bow_splash_height * 0.35;
        const double shaped_top = baseline_y - bow_splash + scallop * amplitude * stern_taper;
        const double lower_ripple =
            std::cos(2.0 * kPi * (phase_cycles * 0.55)) *
            amplitude * 0.18 * stern_taper;
        const double shaped_bottom = baseline_y + config_.foam.thickness * stern_taper + lower_ripple;
        const double top = lerp(bow_point_y, shaped_top, front_curve);
        const double bottom = lerp(bow_point_y + 2.0, shaped_bottom, front_curve);
        SDL_Rect strip{
            static_cast<int>(std::floor(std::min(x0, x1))),
            static_cast<int>(std::floor(top)),
            std::max(1, static_cast<int>(std::ceil(std::abs(x1 - x0)))),
            std::max(1, static_cast<int>(std::ceil(bottom - top)))
        };
        SDL_RenderFillRect(renderer, &strip);
    }

    const int crest_count = std::max(0, config_.foam.speed_crest_count);
    const double moving_spacing = std::max(1.0, config_.foam.speed_crest_spacing);
    const double crest_scroll = positiveMod(frame.speed_crest_phase + world_scroll, moving_spacing);
    for (int crest = 0; crest < crest_count; ++crest) {
        const double crest_height = crest < static_cast<int>(config_.foam.speed_crest_heights.size())
            ? config_.foam.speed_crest_heights[static_cast<std::size_t>(crest)]
            : config_.foam.speed_crest_height;
        const double crest_y_offset = crest < static_cast<int>(config_.foam.speed_crest_y_offsets.size())
            ? config_.foam.speed_crest_y_offsets[static_cast<std::size_t>(crest)]
            : (config_.foam.speed_crest_y_offsets.empty() && config_.foam.speed_crest_y_offset == 0.0
                    ? config_.foam.speed_crest_angle_offset
                    : config_.foam.speed_crest_y_offset);
        const double crest_angle_degrees = crest < static_cast<int>(config_.foam.speed_crest_angle_degrees_list.size())
            ? config_.foam.speed_crest_angle_degrees_list[static_cast<std::size_t>(crest)]
            : config_.foam.speed_crest_angle_degrees;
        const double trail_index = crest == 0
            ? 0.0
            : static_cast<double>(crest) + crest_scroll / moving_spacing;
        const double front_pulse = crest == 0
            ? 1.0 + std::sin(frame.loop_time * 2.0 * kPi / config_.foam.speed_crest_front_pulse_seconds) *
                config_.foam.speed_crest_front_pulse_amount
            : 1.0;
        const double scale = std::max(0.42, 1.0 - trail_index * 0.23) * front_pulse;
        const double origin_x = front_x +
            config_.foam.speed_crest_front_x_offset -
            trail_index * moving_spacing;
        const double base_y = baseline_y + crest_y_offset + trail_index * 2.0;
        const double crest_angle_radians = crest_angle_degrees * kPi / 180.0;
        const double crest_skew = std::tan(crest_angle_radians);
        const int crest_segments = 24;
        for (int i = 0; i < crest_segments; ++i) {
            const double t0 = static_cast<double>(i) / crest_segments;
            const double t1 = static_cast<double>(i + 1) / crest_segments;
            const double mid_t = (t0 + t1) * 0.5;
            const double hook0 = 1.0 - smoothstep(0.0, 0.34, t0);
            const double hook1 = 1.0 - smoothstep(0.0, 0.34, t1);
            const double hook_mid = 1.0 - smoothstep(0.0, 0.34, mid_t);
            const double lean0 = smoothstep(0.20, 1.0, t0);
            const double lean1 = smoothstep(0.20, 1.0, t1);
            const double center_x0 = origin_x -
                config_.foam.speed_crest_spacing * 0.78 * hook0 * scale -
                crest_height * 0.22 * lean0 * scale +
                crest_skew * (crest_height * t0 * scale);
            const double center_x1 = origin_x -
                config_.foam.speed_crest_spacing * 0.78 * hook1 * scale -
                crest_height * 0.22 * lean1 * scale +
                crest_skew * (crest_height * t1 * scale);
            const double top = base_y - crest_height * mid_t * scale;
            const double body = 0.34 + 0.66 * std::sin(kPi * mid_t);
            const double width_boost = 1.0 + 0.35 * hook_mid;
            const double stroke = config_.foam.speed_crest_thickness * body * width_boost * scale;
            SDL_Rect strip{
                static_cast<int>(std::floor(std::min(center_x0, center_x1) - stroke * 0.5)),
                static_cast<int>(std::floor(top)),
                std::max(1, static_cast<int>(std::ceil(std::abs(center_x1 - center_x0) + stroke))),
                std::max(1, static_cast<int>(std::ceil(stroke)))
            };
            SDL_RenderFillRect(renderer, &strip);
        }
    }
}

} // namespace pr
