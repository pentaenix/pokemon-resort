#pragma once

#include <cstdint>
#include <vector>

namespace pr::gameplay::world3d::rendering {

struct RgbaImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
    bool valid() const { return width > 0 && height > 0 && !pixels.empty(); }
};

// Decode PNG bytes and build RGBA with white RGB and original alpha (Gen4 entry flash).
RgbaImage buildWhiteSilhouetteFromPngBytes(const std::vector<std::uint8_t>& png_bytes);

RgbaImage decodePngToRgba(const std::vector<std::uint8_t>& png_bytes);

} // namespace pr::gameplay::world3d::rendering
