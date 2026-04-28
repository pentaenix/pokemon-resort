#include "ui/transfer_system/detail/SdlScanlineDraw.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace pr::transfer_system::detail {

void setDrawColor(SDL_Renderer* renderer, const Color& c) {
    SDL_SetRenderDrawColor(
        renderer,
        static_cast<Uint8>(c.r),
        static_cast<Uint8>(c.g),
        static_cast<Uint8>(c.b),
        static_cast<Uint8>(c.a));
}

void fillRoundedRectScanlines(SDL_Renderer* renderer, int x, int y, int w, int h, int radius, const Color& c) {
    if (w <= 0 || h <= 0) {
        return;
    }
    radius = std::max(0, std::min(radius, std::min(w, h) / 2));
    setDrawColor(renderer, c);
    for (int yy = y; yy < y + h; ++yy) {
        int x0 = x;
        int x1 = x + w;
        if (yy < y + radius) {
            const int dy = yy - (y + radius);
            const int disc = radius * radius - dy * dy;
            if (disc >= 0) {
                const int s = static_cast<int>(std::lround(std::sqrt(static_cast<double>(disc))));
                x0 = (x + radius) - s;
                x1 = (x + w - radius) + s;
            }
        } else if (yy >= y + h - radius) {
            const int dy = yy - (y + h - radius);
            const int disc = radius * radius - dy * dy;
            if (disc >= 0) {
                const int s = static_cast<int>(std::lround(std::sqrt(static_cast<double>(disc))));
                x0 = (x + radius) - s;
                x1 = (x + w - radius) + s;
            }
        }
        if (x1 > x0) {
            SDL_Rect row{x0, yy, x1 - x0, 1};
            SDL_RenderFillRect(renderer, &row);
        }
    }
}

void fillRoundedRingScanlines(
    SDL_Renderer* renderer,
    int x,
    int y,
    int w,
    int h,
    int outer_radius,
    int stroke,
    const Color& border,
    const Color& inner_fill) {
    if (w <= 0 || h <= 0 || stroke <= 0 || stroke * 2 >= w || stroke * 2 >= h) {
        return;
    }
    fillRoundedRectScanlines(renderer, x, y, w, h, outer_radius, border);
    const int inner_r = std::max(0, outer_radius - stroke);
    fillRoundedRectScanlines(renderer, x + stroke, y + stroke, w - 2 * stroke, h - 2 * stroke, inner_r, inner_fill);
}

void fillTriangleScanlines(SDL_Renderer* renderer, int x0, int y0, int x1, int y1, int x2, int y2, const Color& c) {
    struct P {
        int x;
        int y;
    };
    P p[3] = {{x0, y0}, {x1, y1}, {x2, y2}};
    std::sort(p, p + 3, [](const P& a, const P& b) { return a.y < b.y; });

    if (p[0].y == p[2].y) {
        return;
    }

    setDrawColor(renderer, c);
    auto edge_x = [](int x_a, int y_a, int x_b, int y_b, int y) -> double {
        if (y_b == y_a) {
            return static_cast<double>(x_a);
        }
        return static_cast<double>(x_a) +
            static_cast<double>(x_b - x_a) * static_cast<double>(y - y_a) / static_cast<double>(y_b - y_a);
    };

    auto fill_span = [&](int y, double xa, double xb) {
        int x_lo = static_cast<int>(std::floor(std::min(xa, xb)));
        int x_hi = static_cast<int>(std::ceil(std::max(xa, xb)));
        if (x_hi <= x_lo) {
            x_hi = x_lo + 1;
        }
        SDL_Rect row{x_lo, y, x_hi - x_lo, 1};
        SDL_RenderFillRect(renderer, &row);
    };

    if (p[0].y == p[1].y) {
        for (int y = p[0].y; y <= p[2].y; ++y) {
            const double xl = edge_x(p[0].x, p[0].y, p[2].x, p[2].y, y);
            const double xr = edge_x(p[1].x, p[1].y, p[2].x, p[2].y, y);
            fill_span(y, xl, xr);
        }
        return;
    }
    if (p[1].y == p[2].y) {
        for (int y = p[0].y; y <= p[1].y; ++y) {
            const double xl = edge_x(p[0].x, p[0].y, p[1].x, p[1].y, y);
            const double xr = edge_x(p[0].x, p[0].y, p[2].x, p[2].y, y);
            fill_span(y, xl, xr);
        }
        return;
    }

    for (int y = p[0].y; y < p[1].y; ++y) {
        const double xl = edge_x(p[0].x, p[0].y, p[1].x, p[1].y, y);
        const double xr = edge_x(p[0].x, p[0].y, p[2].x, p[2].y, y);
        fill_span(y, xl, xr);
    }
    for (int y = p[1].y; y <= p[2].y; ++y) {
        const double xl = edge_x(p[1].x, p[1].y, p[2].x, p[2].y, y);
        const double xr = edge_x(p[0].x, p[0].y, p[2].x, p[2].y, y);
        fill_span(y, xl, xr);
    }
}

void fillThickSegmentScanlines(SDL_Renderer* renderer, int x0, int y0, int x1, int y1, int thickness, const Color& c) {
    if (thickness <= 0) {
        return;
    }
    const double dx = static_cast<double>(x1 - x0);
    const double dy = static_cast<double>(y1 - y0);
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4) {
        return;
    }
    const double hx = -dy / len * (static_cast<double>(thickness) * 0.5);
    const double hy = dx / len * (static_cast<double>(thickness) * 0.5);
    const int ax0 = x0 + static_cast<int>(std::lround(hx));
    const int ay0 = y0 + static_cast<int>(std::lround(hy));
    const int ax1 = x0 - static_cast<int>(std::lround(hx));
    const int ay1 = y0 - static_cast<int>(std::lround(hy));
    const int bx0 = x1 + static_cast<int>(std::lround(hx));
    const int by0 = y1 + static_cast<int>(std::lround(hy));
    const int bx1 = x1 - static_cast<int>(std::lround(hx));
    const int by1 = y1 - static_cast<int>(std::lround(hy));
    fillTriangleScanlines(renderer, ax0, ay0, ax1, ay1, bx1, by1, c);
    fillTriangleScanlines(renderer, ax0, ay0, bx1, by1, bx0, by0, c);
}

void drawRoundedOutlineScanlines(
    SDL_Renderer* renderer,
    int x,
    int y,
    int w,
    int h,
    int radius,
    const Color& c,
    int thickness) {
    if (w <= 0 || h <= 0 || thickness <= 0) {
        return;
    }
    radius = std::clamp(radius, 0, std::min(w, h) / 2);
    thickness = std::clamp(thickness, 1, std::min(w, h) / 2);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, static_cast<Uint8>(c.r), static_cast<Uint8>(c.g), static_cast<Uint8>(c.b), static_cast<Uint8>(c.a));

    const int inner_x = x + thickness;
    const int inner_y = y + thickness;
    const int inner_w = w - thickness * 2;
    const int inner_h = h - thickness * 2;
    const int inner_r = std::max(0, radius - thickness);

    auto span_for = [&](int yy, int ox, int oy, int ow, int oh, int r) -> std::pair<int, int> {
        int x0 = ox;
        int x1 = ox + ow;
        if (r > 0) {
            if (yy < oy + r) {
                const int dy = yy - (oy + r);
                const int disc = r * r - dy * dy;
                if (disc >= 0) {
                    const int s = static_cast<int>(std::lround(std::sqrt(static_cast<double>(disc))));
                    x0 = (ox + r) - s;
                    x1 = (ox + ow - r) + s;
                }
            } else if (yy >= oy + oh - r) {
                const int dy = yy - (oy + oh - r);
                const int disc = r * r - dy * dy;
                if (disc >= 0) {
                    const int s = static_cast<int>(std::lround(std::sqrt(static_cast<double>(disc))));
                    x0 = (ox + r) - s;
                    x1 = (ox + ow - r) + s;
                }
            }
        }
        return {x0, x1};
    };

    for (int yy = y; yy < y + h; ++yy) {
        const auto [ox0, ox1] = span_for(yy, x, y, w, h, radius);
        int ix0 = 0;
        int ix1 = 0;
        const bool has_inner = inner_w > 0 && inner_h > 0 && yy >= inner_y && yy < inner_y + inner_h;
        if (has_inner) {
            const auto inner = span_for(yy, inner_x, inner_y, inner_w, inner_h, inner_r);
            ix0 = inner.first;
            ix1 = inner.second;
        }

        const int left_w = has_inner ? std::max(0, ix0 - ox0) : std::max(0, ox1 - ox0);
        if (left_w > 0) {
            SDL_Rect rct{ox0, yy, left_w, 1};
            SDL_RenderFillRect(renderer, &rct);
        }
        if (has_inner) {
            const int right_w = std::max(0, ox1 - ix1);
            if (right_w > 0) {
                SDL_Rect rct{ix1, yy, right_w, 1};
                SDL_RenderFillRect(renderer, &rct);
            }
        }
    }
}

} // namespace pr::transfer_system::detail

