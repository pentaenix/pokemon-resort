#include "gameplay/world3d/aquarium/rooms/AquariumRoomRuntime.hpp"

#include <algorithm>
#include <array>

namespace pr::gameplay::world3d::aquarium::rooms {
namespace {

const std::array<RoomSurfacePalette, kRoomSurfacePaletteCount> kPalettes{{
    RoomSurfacePalette{{42, 48, 58, 255}, {48, 56, 68, 255}, {28, 38, 52, 255}, {33, 44, 60, 255}, {58, 76, 98, 255}, {18, 24, 34, 255}},
    RoomSurfacePalette{{38, 56, 72, 255}, {44, 65, 82, 255}, {32, 67, 87, 255}, {27, 58, 78, 255}, {105, 202, 218, 255}, {19, 35, 48, 255}},
    RoomSurfacePalette{{170, 174, 166, 255}, {188, 191, 180, 255}, {104, 116, 116, 255}, {92, 105, 108, 255}, {224, 230, 211, 255}, {66, 75, 78, 255}},
    RoomSurfacePalette{{52, 74, 69, 255}, {60, 86, 78, 255}, {46, 78, 68, 255}, {39, 68, 61, 255}, {174, 202, 149, 255}, {27, 46, 42, 255}},
    RoomSurfacePalette{{172, 145, 103, 255}, {190, 163, 119, 255}, {109, 82, 66, 255}, {96, 72, 61, 255}, {239, 211, 151, 255}, {69, 51, 45, 255}},
    RoomSurfacePalette{{26, 28, 38, 255}, {34, 37, 49, 255}, {23, 27, 42, 255}, {19, 23, 37, 255}, {112, 124, 163, 255}, {11, 14, 24, 255}},
}};

} // namespace

const RoomSurfacePalette& roomSurfacePalette(int index) {
    return kPalettes[static_cast<std::size_t>(
        std::clamp(index, 0, kRoomSurfacePaletteCount - 1))];
}

} // namespace pr::gameplay::world3d::aquarium::rooms
