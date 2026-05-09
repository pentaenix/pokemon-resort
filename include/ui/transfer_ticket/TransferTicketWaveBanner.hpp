#pragma once

#include "core/Types.hpp"

#include <SDL.h>

namespace pr {

class JsonValue;

struct TransferTicketWaveBannerConfig {
    bool enabled = true;
    bool animation_enabled = true;
    Color color{0x09, 0xae, 0xd0, 255};
    int min_height_from_top_px = 100;
    int max_height_from_top_px = 120;
    double amplitude_px = 10.0;
    double wavelength_px = 256.0;
    double speed_px_per_second = 28.0;
    int segments = 160;
};

void applyTransferTicketWaveBannerConfig(TransferTicketWaveBannerConfig& out, const JsonValue& obj);

void renderTransferTicketWaveBanner(
    SDL_Renderer* renderer,
    const TransferTicketWaveBannerConfig& config,
    double elapsed_seconds,
    int viewport_width);

} // namespace pr
