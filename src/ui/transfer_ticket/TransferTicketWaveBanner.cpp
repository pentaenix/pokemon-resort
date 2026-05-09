#include "ui/transfer_ticket/TransferTicketWaveBanner.hpp"

#include "core/config/Json.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace pr {
namespace {

constexpr double kPi = 3.14159265358979323846;

int clampChannel(int value) {
    return std::max(0, std::min(255, value));
}

int hexByte(const std::string& value, std::size_t offset) {
    return std::stoi(value.substr(offset, 2), nullptr, 16);
}

Color parseHexColor(const std::string& value) {
    if ((value.size() != 7 && value.size() != 9) || value[0] != '#') {
        throw std::runtime_error("Expected color in #RRGGBB or #RRGGBBAA format");
    }
    return Color{
        clampChannel(hexByte(value, 1)),
        clampChannel(hexByte(value, 3)),
        clampChannel(hexByte(value, 5)),
        value.size() == 9 ? clampChannel(hexByte(value, 7)) : 255
    };
}

int intFromObjectOrDefault(const JsonValue& obj, const std::string& key, int fallback) {
    const JsonValue* value = obj.get(key);
    return value ? static_cast<int>(value->asNumber()) : fallback;
}

double doubleFromObjectOrDefault(const JsonValue& obj, const std::string& key, double fallback) {
    const JsonValue* value = obj.get(key);
    return value ? value->asNumber() : fallback;
}

bool boolFromObjectOrDefault(const JsonValue& obj, const std::string& key, bool fallback) {
    const JsonValue* value = obj.get(key);
    return value ? value->asBool() : fallback;
}

std::string stringFromObjectOrDefault(const JsonValue& obj, const std::string& key, const std::string& fallback) {
    const JsonValue* value = obj.get(key);
    return value ? value->asString() : fallback;
}

} // namespace

void applyTransferTicketWaveBannerConfig(TransferTicketWaveBannerConfig& out, const JsonValue& obj) {
    if (!obj.isObject()) {
        return;
    }

    out.enabled = boolFromObjectOrDefault(obj, "enabled", out.enabled);
    out.animation_enabled = boolFromObjectOrDefault(obj, "animation_enabled", out.animation_enabled);
    out.color = parseHexColor(stringFromObjectOrDefault(obj, "color", "#09AED0"));
    out.min_height_from_top_px = std::max(
        0,
        intFromObjectOrDefault(obj, "min_height_from_top_px", out.min_height_from_top_px));
    out.max_height_from_top_px = std::max(
        out.min_height_from_top_px,
        intFromObjectOrDefault(obj, "max_height_from_top_px", out.max_height_from_top_px));
    out.amplitude_px = std::max(0.0, doubleFromObjectOrDefault(obj, "amplitude_px", out.amplitude_px));
    out.wavelength_px = std::max(1.0, doubleFromObjectOrDefault(obj, "wavelength_px", out.wavelength_px));
    out.speed_px_per_second = doubleFromObjectOrDefault(obj, "speed_px_per_second", out.speed_px_per_second);
    out.segments = std::max(16, intFromObjectOrDefault(obj, "segments", out.segments));
}

void renderTransferTicketWaveBanner(
    SDL_Renderer* renderer,
    const TransferTicketWaveBannerConfig& config,
    double elapsed_seconds,
    int viewport_width) {
    if (!renderer || !config.enabled || viewport_width <= 0) {
        return;
    }

    const int min_y = std::max(0, std::min(config.min_height_from_top_px, config.max_height_from_top_px));
    const int max_y = std::max(min_y, std::max(config.min_height_from_top_px, config.max_height_from_top_px));
    const double baseline = (static_cast<double>(min_y) + static_cast<double>(max_y)) * 0.5;
    const double half_range = std::max(0.0, (static_cast<double>(max_y - min_y)) * 0.5);
    const double amplitude = half_range > 0.0 ? std::min(config.amplitude_px, half_range) : config.amplitude_px;
    const double wavelength = std::max(1.0, config.wavelength_px);
    const double phase_px = config.animation_enabled ? elapsed_seconds * config.speed_px_per_second : 0.0;
    const int segments = std::max(16, config.segments);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(
        renderer,
        static_cast<Uint8>(config.color.r),
        static_cast<Uint8>(config.color.g),
        static_cast<Uint8>(config.color.b),
        static_cast<Uint8>(config.color.a));

    for (int i = 0; i < segments; ++i) {
        const int x0 = static_cast<int>(std::floor(static_cast<double>(i) * viewport_width / segments));
        const int x1 = static_cast<int>(std::ceil(static_cast<double>(i + 1) * viewport_width / segments));
        const double mid_x = (static_cast<double>(x0) + static_cast<double>(x1)) * 0.5;
        const double angle = (mid_x + phase_px) * 2.0 * kPi / wavelength;
        const double surface_y = std::clamp(
            baseline + std::sin(angle) * amplitude,
            static_cast<double>(min_y),
            static_cast<double>(max_y));
        SDL_Rect strip{x0, 0, std::max(1, x1 - x0), std::max(1, static_cast<int>(std::round(surface_y)))};
        SDL_RenderFillRect(renderer, &strip);
    }
}

} // namespace pr
