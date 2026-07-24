#pragma once

#include <cstdint>
#include <vector>

namespace pr::gameplay::attend::rendering {

// Builds an alpha-only aperture atlas from the dark, enclosed outlines in an
// authored eye-expression sheet. Socket colors vary by Pokemon, so geometry of
// the outline is a more reliable boundary than a white/color heuristic.
std::vector<std::uint8_t> buildAttendEyeSocketMaskRgba(
    const std::uint8_t* rgba,
    int width,
    int height,
    int columns,
    int rows);

} // namespace pr::gameplay::attend::rendering
