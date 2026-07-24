#include "gameplay/attend/rendering/AttendEyeSocketMask.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <queue>
#include <utility>

namespace pr::gameplay::attend::rendering {
namespace {

constexpr int kMinimumOutlineLuminance = 90;
constexpr int kMaximumOutlineLuminance = 200;
constexpr int kOutlineLuminanceStep = 5;

bool isOutlineOrTransparent(const std::uint8_t* pixel, int luminance_threshold) {
    if (pixel[3] == 0) return true;
    const int luminance =
        (299 * static_cast<int>(pixel[0]) +
         587 * static_cast<int>(pixel[1]) +
         114 * static_cast<int>(pixel[2])) /
        1000;
    return luminance < luminance_threshold;
}

struct Component {
    std::vector<std::size_t> pixels;
    bool touches_tile_edge = false;
};

bool matchesLegacyBrightSclera(const std::uint8_t* pixel) {
    if (pixel[3] == 0) return false;
    const int high = std::max({pixel[0], pixel[1], pixel[2]});
    const int low = std::min({pixel[0], pixel[1], pixel[2]});
    const int luminance =
        (299 * static_cast<int>(pixel[0]) +
         587 * static_cast<int>(pixel[1]) +
         114 * static_cast<int>(pixel[2])) /
        1000;
    return high - low <= 46 && luminance >= 107;
}

std::vector<std::size_t> contrastingForeground(
    const std::uint8_t* rgba,
    int width,
    int left,
    int top,
    int right,
    int bottom) {
    constexpr int kQuantizationShift = 4;
    constexpr int kColorDistanceThreshold = 36;
    std::array<int, 4096> histogram{};
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            const std::size_t index = static_cast<std::size_t>(y * width + x);
            const std::uint8_t* pixel = rgba + index * 4;
            if (pixel[3] == 0) continue;
            const int bin =
                (static_cast<int>(pixel[0] >> kQuantizationShift) << 8) |
                (static_cast<int>(pixel[1] >> kQuantizationShift) << 4) |
                static_cast<int>(pixel[2] >> kQuantizationShift);
            ++histogram[static_cast<std::size_t>(bin)];
        }
    }
    const int background_bin = static_cast<int>(
        std::distance(histogram.begin(), std::max_element(histogram.begin(), histogram.end())));
    int background_sum[3] = {0, 0, 0};
    int background_count = 0;
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            const std::size_t index = static_cast<std::size_t>(y * width + x);
            const std::uint8_t* pixel = rgba + index * 4;
            const int bin =
                (static_cast<int>(pixel[0] >> kQuantizationShift) << 8) |
                (static_cast<int>(pixel[1] >> kQuantizationShift) << 4) |
                static_cast<int>(pixel[2] >> kQuantizationShift);
            if (pixel[3] == 0 || bin != background_bin) continue;
            background_sum[0] += pixel[0];
            background_sum[1] += pixel[1];
            background_sum[2] += pixel[2];
            ++background_count;
        }
    }
    if (background_count == 0) return {};
    const int background[3] = {
        background_sum[0] / background_count,
        background_sum[1] / background_count,
        background_sum[2] / background_count};

    std::vector<std::size_t> foreground;
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            const std::size_t index = static_cast<std::size_t>(y * width + x);
            const std::uint8_t* pixel = rgba + index * 4;
            if (pixel[3] == 0) continue;
            const int distance =
                std::abs(static_cast<int>(pixel[0]) - background[0]) +
                std::abs(static_cast<int>(pixel[1]) - background[1]) +
                std::abs(static_cast<int>(pixel[2]) - background[2]);
            if (distance >= kColorDistanceThreshold) foreground.push_back(index);
        }
    }
    return foreground;
}

Component collectComponent(
    const std::uint8_t* rgba,
    int width,
    int left,
    int top,
    int right,
    int bottom,
    int start_x,
    int start_y,
    int luminance_threshold,
    std::vector<bool>& visited) {
    Component component;
    std::queue<std::pair<int, int>> pending;
    pending.emplace(start_x, start_y);
    visited[static_cast<std::size_t>(start_y * width + start_x)] = true;

    constexpr int kDx[] = {-1, 1, 0, 0};
    constexpr int kDy[] = {0, 0, -1, 1};
    while (!pending.empty()) {
        const auto [x, y] = pending.front();
        pending.pop();
        const std::size_t pixel_index = static_cast<std::size_t>(y * width + x);
        component.pixels.push_back(pixel_index);
        component.touches_tile_edge = component.touches_tile_edge ||
            x == left || x == right - 1 || y == top || y == bottom - 1;

        for (int direction = 0; direction < 4; ++direction) {
            const int next_x = x + kDx[direction];
            const int next_y = y + kDy[direction];
            if (next_x < left || next_x >= right || next_y < top || next_y >= bottom) continue;
            const std::size_t next_index = static_cast<std::size_t>(next_y * width + next_x);
            if (visited[next_index] ||
                isOutlineOrTransparent(rgba + next_index * 4, luminance_threshold)) {
                continue;
            }
            visited[next_index] = true;
            pending.emplace(next_x, next_y);
        }
    }
    return component;
}

} // namespace

std::vector<std::uint8_t> buildAttendEyeSocketMaskRgba(
    const std::uint8_t* rgba,
    int width,
    int height,
    int columns,
    int rows) {
    if (!rgba || width <= 0 || height <= 0 || columns <= 0 || rows <= 0) return {};

    std::vector<std::uint8_t> mask(static_cast<std::size_t>(width * height * 4), 0);
    for (int row = 0; row < rows; ++row) {
        const int top = row * height / rows;
        const int bottom = (row + 1) * height / rows;
        for (int column = 0; column < columns; ++column) {
            const int left = column * width / columns;
            const int right = (column + 1) * width / columns;
            const std::size_t minimum_socket_area = static_cast<std::size_t>(
                std::max(1, (right - left) * (bottom - top) / 100));
            Component best;

            // Eye atlases use anything from near-black outlines (Dewpider) to
            // medium gray anti-aliased outlines (Wartortle/Steenee). Search the
            // useful range and keep the largest region that is actually closed
            // inside the expression tile.
            for (int luminance_threshold = kMinimumOutlineLuminance;
                 luminance_threshold <= kMaximumOutlineLuminance;
                 luminance_threshold += kOutlineLuminanceStep) {
                std::vector<bool> visited(static_cast<std::size_t>(width * height), false);
                for (int y = top; y < bottom; ++y) {
                    for (int x = left; x < right; ++x) {
                        const std::size_t pixel_index = static_cast<std::size_t>(y * width + x);
                        if (visited[pixel_index] ||
                            isOutlineOrTransparent(rgba + pixel_index * 4, luminance_threshold)) {
                            continue;
                        }
                        Component candidate = collectComponent(
                            rgba,
                            width,
                            left,
                            top,
                            right,
                            bottom,
                            x,
                            y,
                            luminance_threshold,
                            visited);
                        if (!candidate.touches_tile_edge &&
                            candidate.pixels.size() > best.pixels.size()) {
                            best = std::move(candidate);
                        }
                    }
                }
            }

            if (best.pixels.size() >= minimum_socket_area) {
                for (std::size_t pixel_index : best.pixels) {
                    const std::size_t output = pixel_index * 4;
                    mask[output + 0] = 255;
                    mask[output + 1] = 255;
                    mask[output + 2] = 255;
                    mask[output + 3] = 255;
                }
                continue;
            }

            // Some legacy sheets draw an intentionally open eyelid rather than
            // a closed socket. Preserve the renderer's pre-stencil behavior for
            // those sheets without applying its white-only rule to colored,
            // closed sockets such as Dewpider's.
            int legacy_aperture_pixels = 0;
            for (int y = top; y < bottom; ++y) {
                for (int x = left; x < right; ++x) {
                    const std::size_t pixel_index = static_cast<std::size_t>(y * width + x);
                    if (!matchesLegacyBrightSclera(rgba + pixel_index * 4)) continue;
                    const std::size_t output = pixel_index * 4;
                    mask[output + 0] = 255;
                    mask[output + 1] = 255;
                    mask[output + 2] = 255;
                    mask[output + 3] = 255;
                    ++legacy_aperture_pixels;
                }
            }
            if (legacy_aperture_pixels > 0) continue;

            const std::vector<std::size_t> foreground = contrastingForeground(
                rgba,
                width,
                left,
                top,
                right,
                bottom);
            if (foreground.size() < minimum_socket_area) continue;
            for (std::size_t pixel_index : foreground) {
                const std::size_t output = pixel_index * 4;
                mask[output + 0] = 255;
                mask[output + 1] = 255;
                mask[output + 2] = 255;
                mask[output + 3] = 255;
            }
        }
    }
    return mask;
}

} // namespace pr::gameplay::attend::rendering
