#pragma once

#include "core/Types.hpp"

#include <SDL.h>

namespace pr::transfer_system::detail {

void setDrawColor(SDL_Renderer* renderer, const Color& c);

void fillRoundedRectScanlines(SDL_Renderer* renderer, int x, int y, int w, int h, int radius, const Color& c);

void fillRoundedRingScanlines(
    SDL_Renderer* renderer,
    int x,
    int y,
    int w,
    int h,
    int outer_radius,
    int stroke,
    const Color& border,
    const Color& inner_fill);

void fillTriangleScanlines(SDL_Renderer* renderer, int x0, int y0, int x1, int y1, int x2, int y2, const Color& c);

void fillThickSegmentScanlines(SDL_Renderer* renderer, int x0, int y0, int x1, int y1, int thickness, const Color& c);

void drawRoundedOutlineScanlines(
    SDL_Renderer* renderer,
    int x,
    int y,
    int w,
    int h,
    int radius,
    const Color& c,
    int thickness);

} // namespace pr::transfer_system::detail

