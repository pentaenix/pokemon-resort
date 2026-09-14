#include "gameplay/world3d/aquarium/rooms/AquariumBuildingLayout.hpp"

#include <algorithm>
#include <deque>
#include <set>
#include <utility>

namespace pr::gameplay::world3d::aquarium::rooms {
namespace {
bool validId(const std::string& id) {
    return !id.empty() && id.size()<=128 && std::all_of(id.begin(),id.end(),[](unsigned char c) {
        return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-';
    });
}
bool validWall(Wall wall) {
    return wall==Wall::North || wall==Wall::East || wall==Wall::South || wall==Wall::West;
}
bool horizontal(Wall wall) { return wall==Wall::North || wall==Wall::South; }
bool boundsValid(RoomBounds b) {
    return b.width>=8 && b.depth>=8 && b.width<=128 && b.depth<=128 &&
        b.column>=-4096 && b.column<=4096 && b.row>=-4096 && b.row<=4096;
}
bool inside(RoomBounds b,Cell p) {
    return p.column>=b.column && p.column<b.column+b.width && p.row>=b.row && p.row<b.row+b.depth;
}
auto endpointKey(const DoorEndpoint& endpoint) { return std::make_pair(endpoint.room_id,endpoint.door_id); }
LayoutProposal finish(BuildingLayout candidate,const std::vector<RoomOccupancy>& occupancy) {
    auto errors=validateBuildingLayout(candidate,occupancy);
    if (!errors.empty()) return {std::nullopt,std::move(errors)};
    if (candidate.revision>=9007199254740991ULL) return {std::nullopt,{"Building revision exhausted"}};
    ++candidate.revision;
    return {std::move(candidate),{}};
}
}

Wall oppositeWall(Wall wall) {
    switch(wall) {
    case Wall::North:return Wall::South;
    case Wall::South:return Wall::North;
    case Wall::East:return Wall::West;
    case Wall::West:return Wall::East;
    }
    return wall;
}
const char* wallName(Wall wall) {
    switch(wall) {
    case Wall::North:return "north";
    case Wall::South:return "south";
    case Wall::East:return "east";
    case Wall::West:return "west";
    }
    return "invalid";
}
const RoomLayout* findRoom(const BuildingLayout& layout,const std::string& id) {
    auto it=std::find_if(layout.rooms.begin(),layout.rooms.end(),[&](const auto& room){return room.id==id;});
    return it==layout.rooms.end() ? nullptr : &*it;
}
const RoomDoor* findDoor(const RoomLayout& room,const std::string& id) {
    auto it=std::find_if(room.doors.begin(),room.doors.end(),[&](const auto& door){return door.id==id;});
    return it==room.doors.end() ? nullptr : &*it;
}
Cell doorLandingCell(const RoomLayout& room,const RoomDoor& door) {
    const auto b=room.bounds;
    switch(door.wall) {
    case Wall::North:return {b.column+door.offset,b.row};
    case Wall::South:return {b.column+door.offset,b.row+b.depth-1};
    case Wall::West:return {b.column,b.row+door.offset};
    case Wall::East:return {b.column+b.width-1,b.row+door.offset};
    }
    return {};
}
std::vector<Cell> protectedDoorCells(const RoomLayout& room,const RoomDoor& door) {
    std::vector<Cell> cells;
    const auto center=doorLandingCell(room,door);
    for (int inward=0;inward<2;++inward) for (int across=-1;across<=1;++across) {
        const int dx=horizontal(door.wall) ? across : door.wall==Wall::West ? inward : -inward;
        const int dy=!horizontal(door.wall) ? across : door.wall==Wall::North ? inward : -inward;
        cells.push_back({center.column+dx,center.row+dy});
    }
    return cells;
}
std::optional<DoorLanding> resolveDoorLanding(const BuildingLayout& layout,const DoorEndpoint& source) {
    for (const auto& connection:layout.connections) {
        const DoorEndpoint* target=nullptr;
        if (endpointKey(connection.first)==endpointKey(source)) target=&connection.second;
        if (endpointKey(connection.second)==endpointKey(source)) target=&connection.first;
        if (!target) continue;
        const auto* room=findRoom(layout,target->room_id);
        const auto* door=room ? findDoor(*room,target->door_id) : nullptr;
        if (!door) return std::nullopt;
        return DoorLanding{*target,doorLandingCell(*room,*door),oppositeWall(door->wall)};
    }
    return std::nullopt;
}

std::vector<std::string> validateBuildingLayout(const BuildingLayout& layout,const std::vector<RoomOccupancy>& occupancy) {
    std::vector<std::string> errors;
    if (!validId(layout.id)) errors.push_back("Invalid building ID");
    if (layout.revision>9007199254740991ULL) errors.push_back("Invalid building revision");
    if (layout.rooms.empty() || layout.rooms.size()>128) errors.push_back("Invalid room count");
    if (layout.connections.size()>1024 || layout.external_connections.size()>1024) errors.push_back("Too many connections");
    std::set<std::string> room_ids,link_ids;
    std::set<std::pair<std::string,std::string>> doors,connected;
    for (const auto& room:layout.rooms) {
        if (!validId(room.id) || !room_ids.insert(room.id).second) errors.push_back("Invalid/duplicate room ID: "+room.id);
        if (!boundsValid(room.bounds)) { errors.push_back("Invalid room bounds: "+room.id); continue; }
        if (room.doors.size()>128) errors.push_back("Too many room doors: "+room.id);
        bool positions_valid=true;
        std::set<std::pair<int,int>> lanes;
        for (const auto& door:room.doors) {
            if (!validId(door.id) || !doors.insert({room.id,door.id}).second) errors.push_back("Invalid/duplicate door ID: "+door.id);
            const int length=horizontal(door.wall) ? room.bounds.width : room.bounds.depth;
            if (!validWall(door.wall) || door.offset<2 || door.offset>length-3) {
                positions_valid=false;
                errors.push_back("Door intersects room corner: "+door.id); continue;
            }
            for (auto cell:protectedDoorCells(room,door))
                if (!lanes.insert({cell.column,cell.row}).second) errors.push_back("Door lanes overlap: "+door.id);
        }
        for (const auto& occupied:occupancy) if (occupied.room_id==room.id) for (auto cell:occupied.cells) {
            if (!inside(room.bounds,cell)) errors.push_back("Room resize intersects a tank: "+room.id);
            if (lanes.count({cell.column,cell.row})) errors.push_back("Door lane intersects a tank: "+room.id);
        }
        if (positions_valid && room.doors.size()>1) {
            std::set<std::pair<int,int>> blocked, visited;
            for (const auto& occupied:occupancy) if (occupied.room_id==room.id)
                for (auto cell:occupied.cells) blocked.insert({cell.column,cell.row});
            std::deque<Cell> pending;
            const auto add=[&](Cell cell) {
                const auto key=std::make_pair(cell.column,cell.row);
                if (inside(room.bounds,cell) && !blocked.count(key) && visited.insert(key).second) pending.push_back(cell);
            };
            add(doorLandingCell(room,room.doors.front()));
            while (!pending.empty()) {
                const auto p=pending.front(); pending.pop_front();
                for (auto delta:{Cell{1,0},Cell{-1,0},Cell{0,1},Cell{0,-1}})
                    add({p.column+delta.column,p.row+delta.row});
            }
            for (const auto& door:room.doors) {
                const auto p=doorLandingCell(room,door);
                if (!visited.count({p.column,p.row})) errors.push_back("Doors are disconnected by tanks: "+room.id);
            }
        }
    }
    const auto endpoint=[&](const DoorEndpoint& e) {
        if (!doors.count(endpointKey(e))) errors.push_back("Missing door endpoint: "+e.room_id+"/"+e.door_id);
        if (!connected.insert(endpointKey(e)).second) errors.push_back("Door has multiple connections: "+e.door_id);
    };
    for (const auto& connection:layout.connections) {
        if (!validId(connection.id) || !link_ids.insert(connection.id).second) errors.push_back("Invalid/duplicate connection ID");
        endpoint(connection.first); endpoint(connection.second);
        const auto* a=findRoom(layout,connection.first.room_id);
        const auto* b=findRoom(layout,connection.second.room_id);
        const auto* ad=a ? findDoor(*a,connection.first.door_id) : nullptr;
        const auto* bd=b ? findDoor(*b,connection.second.door_id) : nullptr;
        if (a==b || (ad && bd && oppositeWall(ad->wall)!=bd->wall)) errors.push_back("Connection requires different rooms and opposite walls");
    }
    for (const auto& connection:layout.external_connections) {
        if (!validId(connection.id) || !link_ids.insert(connection.id).second ||
            !validId(connection.destination_map_id) ||
            (!connection.destination_anchor_id.empty() && !validId(connection.destination_anchor_id)))
            errors.push_back("Invalid external connection");
        endpoint(connection.interior);
    }
    if (doors!=connected) errors.push_back("Every door must have exactly one connection");
    for (const auto& occupied:occupancy) if (!room_ids.count(occupied.room_id)) errors.push_back("Occupancy references a missing room");
    return errors;
}

LayoutProposal proposeDoorMove(const BuildingLayout& current,const DoorEndpoint& endpoint,int offset,
    const std::vector<RoomOccupancy>& occupancy) {
    auto candidate=current;
    for (auto& room:candidate.rooms) if (room.id==endpoint.room_id)
        for (auto& door:room.doors) if (door.id==endpoint.door_id) {
            door.offset=offset; // Wall and both connection endpoints are immutable.
            return finish(std::move(candidate),occupancy);
        }
    return {std::nullopt,{"Door not found"}};
}
LayoutProposal proposeRoomResize(const BuildingLayout& current,const std::string& id,RoomBounds bounds,
    const std::vector<RoomOccupancy>& occupancy) {
    auto candidate=current;
    for (auto& room:candidate.rooms) if (room.id==id) {
        room.bounds=bounds;
        return finish(std::move(candidate),occupancy);
    }
    return {std::nullopt,{"Room not found"}};
}
LayoutProposal proposeConnectedRoom(const BuildingLayout& current,const std::string& source_id,
    Wall wall,int offset,const std::string& source_door_id,const std::string& new_room_id,
    const std::string& receiving_door_id,const std::string& connection_id,const std::vector<RoomOccupancy>& occupancy) {
    auto candidate=current;
    auto source=std::find_if(candidate.rooms.begin(),candidate.rooms.end(),[&](auto& room){return room.id==source_id;});
    if (source==candidate.rooms.end()) return {std::nullopt,{"Source room not found"}};
    source->doors.push_back({source_door_id,wall,offset});
    // Odd dimensions let a three-cell doorway be exactly centred on whole cells.
    RoomLayout receiving{new_room_id, horizontal(wall) ? RoomBounds{0,0,17,13} : RoomBounds{0,0,13,17},{}};
    const int length=horizontal(wall) ? receiving.bounds.width : receiving.bounds.depth;
    receiving.doors.push_back({receiving_door_id,oppositeWall(wall),(length-1)/2});
    candidate.rooms.push_back(std::move(receiving));
    candidate.connections.push_back({connection_id,{source_id,source_door_id},{new_room_id,receiving_door_id}});
    return finish(std::move(candidate),occupancy);
}
} // namespace pr::gameplay::world3d::aquarium::rooms
