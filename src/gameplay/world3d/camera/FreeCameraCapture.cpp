#include "gameplay/world3d/camera/FreeCameraCapture.hpp"

#include <iomanip>
#include <limits>
#include <sstream>

namespace pr::gameplay::world3d::camera {
namespace {

const char* facingName(FacingDirection facing) {
    switch (facing) {
        case FacingDirection::North: return "north";
        case FacingDirection::East: return "east";
        case FacingDirection::South: return "south";
        case FacingDirection::West: return "west";
    }
    return "south";
}

std::string escaped(std::string value) {
    std::string out;
    out.reserve(value.size());
    for (const char character : value) {
        if (character == '\\' || character == '"') out.push_back('\\');
        out.push_back(character);
    }
    return out;
}

void appendVec3(std::ostringstream& out, Vec3 value) {
    out << '[' << value.x << ',' << value.y << ',' << value.z << ']';
}

} // namespace

std::string formatFreeCameraCapture(const FreeCameraCapture& capture) {
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<float>::max_digits10)
        << "{\"mapId\":\"" << escaped(capture.map_id)
        << "\",\"nearestPlacementId\":\"" << escaped(capture.nearest_placement_id)
        << "\",\"cameraPosition\":";
    appendVec3(out, capture.camera_position);
    out << ",\"yawDegrees\":" << capture.yaw_degrees
        << ",\"pitchDegrees\":" << capture.pitch_degrees
        << ",\"playerPosition\":";
    appendVec3(out, capture.player_position);
    out << ",\"playerFacing\":\"" << facingName(capture.player_facing) << "\"}";
    return out.str();
}

} // namespace pr::gameplay::world3d::camera
