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
inline constexpr int kRoomSurfacePaletteCount = 6;
inline constexpr int kMaximumRoomFloorDepth = 5;
struct RoomSurfaceStyle {
    struct FloorOverride { Cell cell; int palette=0; };
    struct FloorDepthOverride { Cell cell; int depth=0; };
    struct WallOverride { Wall wall=Wall::North; int segment=0; int palette=0; };
    int floor_palette = 0;
    int wall_palette = 0;
    std::vector<FloorOverride> floor_overrides;
    // Integer room-cell depressions. Zero is implicit; persisted values are 1..5.
    std::vector<FloorDepthOverride> floor_depth_overrides;
    std::vector<WallOverride> wall_overrides;
};
inline constexpr std::size_t kRoomDecorationLimit = 50;
struct RoomDecoration {
    std::string id;
    std::string asset_id;
    Cell cell;
    int yaw_quarter_turns=0;
};
enum class RoomTransitionKind { Ramp, Stairs };
struct RoomTransition {
    std::string id;
    RoomTransitionKind kind=RoomTransitionKind::Ramp;
    // Both cells are room-local. The lower cell owns the runtime ramp special.
    Cell lower_cell;
    Cell upper_cell;
};
struct RoomLayout {
    std::string id;
    RoomBounds bounds;
    std::vector<RoomDoor> doors;
    RoomSurfaceStyle surfaces;
    std::vector<RoomDecoration> decorations;
    std::vector<RoomTransition> transitions;
};
int roomFloorDepth(const RoomLayout& room,Cell cell);
const char* roomTransitionKindName(RoomTransitionKind kind);
std::vector<Cell> roomTransitionUpperCandidates(const RoomLayout& room,Cell lower_cell);
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
RoomLayout* findRoom(BuildingLayout&, const std::string& id);
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
LayoutProposal proposeRoomTransition(const BuildingLayout&,const std::string& room_id,
    Cell lower_cell,RoomTransitionKind kind,const std::vector<RoomOccupancy>& occupancy={});
// Creates a straight, cardinal stair flight between existing endpoint levels.
// Intermediate cells are assigned the required one-level depth bands.
LayoutProposal proposeRoomStairFlight(const BuildingLayout&,const std::string& room_id,
    Cell start,Cell finish,const std::vector<RoomOccupancy>& occupancy={});
// Fills a rectangular stair footprint beside a different floor level. The
// attachment side and rise direction are inferred from the surrounding floor.
LayoutProposal proposeRoomStairArea(const BuildingLayout&,const std::string& room_id,
    Cell first,Cell last,const std::vector<RoomOccupancy>& occupancy={});
LayoutProposal proposeRoomStairRemovalArea(const BuildingLayout&,const std::string& room_id,
    Cell first,Cell last,const std::vector<RoomOccupancy>& occupancy={});

enum class LayoutLoadStatus { Loaded, Invalid, NewerVersion };
struct LayoutLoadResult {
    LayoutLoadStatus status=LayoutLoadStatus::Invalid;
    std::optional<BuildingLayout> document;
    std::string diagnostic;
};
LayoutLoadResult parseBuildingLayout(const std::string& json);
std::string serializeBuildingLayout(const BuildingLayout&);

} // namespace pr::gameplay::world3d::aquarium::rooms
