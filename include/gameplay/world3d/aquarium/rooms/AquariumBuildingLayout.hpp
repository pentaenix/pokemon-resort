#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::aquarium::rooms {

enum class Wall { North, East, South, West };
Wall oppositeWall(Wall wall);
const char* wallName(Wall wall);

struct Cell { int column=0, row=0; };
// Room-local cells. Signed origin allows later four-wall editing without
// changing the authoritative coordinates of existing tanks.
struct RoomBounds { int column=0, row=0, width=17, depth=13; };
struct RoomDoor {
    std::string id;
    Wall wall=Wall::South;
    int offset=0; // Centre cell measured along its own wall from the NW end.
};
struct RoomLayout {
    std::string id;
    RoomBounds bounds;
    std::vector<RoomDoor> doors;
};
struct DoorEndpoint { std::string room_id, door_id; };
struct DoorConnection { std::string id; DoorEndpoint first, second; };
// Existing overworld links are preserved rather than invented as editable rooms.
struct ExternalDoorConnection {
    std::string id;
    DoorEndpoint interior;
    std::string destination_map_id, destination_anchor_id;
};
struct BuildingLayout {
    std::string id;
    std::uint64_t revision=0;
    std::vector<RoomLayout> rooms;
    std::vector<DoorConnection> connections;
    std::vector<ExternalDoorConnection> external_connections;
};
struct RoomOccupancy {
    std::string room_id;
    // Complete tank collision/footprint cells supplied by the runtime adapter,
    // not only tank centres. Never persisted as part of the building document.
    std::vector<Cell> cells;
};
struct DoorLanding {
    DoorEndpoint endpoint;
    Cell cell;
    Wall facing; // Direction into the destination room.
};

const RoomLayout* findRoom(const BuildingLayout&, const std::string& id);
const RoomDoor* findDoor(const RoomLayout&, const std::string& id);
Cell doorLandingCell(const RoomLayout&, const RoomDoor&);
std::vector<Cell> protectedDoorCells(const RoomLayout&, const RoomDoor&);
std::optional<DoorLanding> resolveDoorLanding(const BuildingLayout&, const DoorEndpoint& source);
std::vector<std::string> validateBuildingLayout(
    const BuildingLayout&, const std::vector<RoomOccupancy>& occupancy={});

struct LayoutProposal {
    std::optional<BuildingLayout> document;
    std::vector<std::string> diagnostics;
};
// Pure candidate operations. Nothing mutates current or writes a save. A live
// commit owner must stage scene/resources and persistence before publication.
LayoutProposal proposeDoorMove(const BuildingLayout&, const DoorEndpoint&, int offset,
    const std::vector<RoomOccupancy>& occupancy={});
LayoutProposal proposeRoomResize(const BuildingLayout&, const std::string& room_id,
    RoomBounds bounds, const std::vector<RoomOccupancy>& occupancy={});
LayoutProposal proposeConnectedRoom(const BuildingLayout&, const std::string& source_room,
    Wall source_wall, int source_offset, const std::string& source_door_id,
    const std::string& new_room_id, const std::string& receiving_door_id,
    const std::string& connection_id, const std::vector<RoomOccupancy>& occupancy={});

enum class LayoutLoadStatus { Loaded, Invalid, NewerVersion };
struct LayoutLoadResult {
    LayoutLoadStatus status=LayoutLoadStatus::Invalid;
    std::optional<BuildingLayout> document;
    std::string diagnostic;
};
LayoutLoadResult parseBuildingLayout(const std::string& json);
std::string serializeBuildingLayout(const BuildingLayout&);

} // namespace pr::gameplay::world3d::aquarium::rooms
