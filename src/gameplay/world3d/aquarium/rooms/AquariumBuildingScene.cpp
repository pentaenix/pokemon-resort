#include "gameplay/world3d/aquarium/rooms/AquariumRoomRuntime.hpp"
#include <algorithm>
#include <stdexcept>

namespace pr::gameplay::world3d::aquarium::rooms {
namespace {
FacingDirection facing(Wall w) {
    switch(w) {
    case Wall::North:return FacingDirection::North;
    case Wall::East:return FacingDirection::East;
    case Wall::South:return FacingDirection::South;
    case Wall::West:return FacingDirection::West;
    }
    return FacingDirection::South;
}
}
SceneConfig projectBuildingRoom(const SceneConfig& style,const BuildingLayout& building,const RoomLayout& room) {
    auto base=style;
    base.id=room.id;
    base.interior.openings.clear(); base.door_triggers.clear(); base.anchors.clear(); base.links.clear();
    base.interior.floor_cutouts.clear(); base.models.clear(); base.characters.clear();
    auto empty=room; empty.doors.clear();
    auto scene=projectRoomLayout(base,empty);
    scene.environment.space="interior:"+room.id;
    scene.terrain.heights.assign(room.bounds.depth,std::vector<std::uint8_t>(room.bounds.width,0));
    scene.terrain.specials=scene.terrain.heights;
    scene.terrain.collision=scene.terrain.heights;
    for(const auto& door:room.doors) {
        const auto p=doorLandingCell(room,door);
        const int x=p.column-room.bounds.column,y=p.row-room.bounds.row;
        scene.anchors.push_back({door.id+"_arrival",x,y,facing(oppositeWall(door.wall))});
        scene.interior.openings.push_back({wallName(door.wall),door.offset-1,door.offset+1});
        std::string link_id;
        bool internal=false;
        for(const auto& connection:building.external_connections) {
            if(connection.interior.room_id!=room.id || connection.interior.door_id!=door.id) continue;
            link_id=connection.id;
            scene.links.push_back({link_id,connection.destination_map_id,connection.destination_anchor_id});
            // Keep the resort's existing arrival anchor valid across upgrades.
            if(room.id=="aquarium_builder_lab")
                scene.anchors.push_back({"from_resort",x,y,facing(oppositeWall(door.wall))});
        }
        for(const auto& connection:building.connections) {
            const DoorEndpoint* other=nullptr;
            if(connection.first.room_id==room.id && connection.first.door_id==door.id) other=&connection.second;
            if(connection.second.room_id==room.id && connection.second.door_id==door.id) other=&connection.first;
            if(!other) continue;
            internal=true;
            link_id=connection.id;
            scene.links.push_back({link_id,other->room_id,other->door_id+"_arrival"});
        }
        if(link_id.empty()) throw std::runtime_error("Unconnected room doorway");
        for(int across=-1;across<=1;++across) {
            DoorTriggerConfig trigger;
            trigger.id=door.id+"_"+std::to_string(across+1);
            trigger.link_id=link_id;
            trigger.script_id=internal?"aquarium_room_transfer":"door_exit_default";
            trigger.allowed_directions={facing(door.wall)};
            trigger.tile_x=x; trigger.tile_y=y;
            switch(door.wall) {
            case Wall::North:trigger.tile_y=-1; trigger.tile_x+=across;break;
            case Wall::South:trigger.tile_y=room.bounds.depth; trigger.tile_x+=across;break;
            case Wall::West:trigger.tile_x=-1; trigger.tile_y+=across;break;
            case Wall::East:trigger.tile_x=room.bounds.width; trigger.tile_y+=across;break;
            }
            scene.door_triggers.push_back(std::move(trigger));
        }
    }
    return scene;
}

LayoutProposal proposeWallMove(const BuildingLayout& current,const std::string& id,Wall wall,
    int coordinate,const std::vector<RoomOccupancy>& occupancy) {
    if(current.revision>=9007199254740991ULL || coordinate < -4096 || coordinate > 4096)
        return {std::nullopt,{"Room coordinate or revision limit reached"}};
    auto candidate=current;
    for(auto& room:candidate.rooms) if(room.id==id) {
        const auto old=room.bounds;
        auto& b=room.bounds;
        switch(wall) {
        case Wall::North:b.row=coordinate;b.depth=old.row+old.depth-coordinate;break;
        case Wall::South:b.depth=coordinate-old.row;break;
        case Wall::West:b.column=coordinate;b.width=old.column+old.width-coordinate;break;
        case Wall::East:b.width=coordinate-old.column;break;
        }
        // Orthogonal doors keep their stable coordinate; never move the other room's endpoint.
        for(auto& door:room.doors) {
            if(door.wall==Wall::North || door.wall==Wall::South) door.offset+=old.column-b.column;
            else door.offset+=old.row-b.row;
        }
        auto errors=validateBuildingLayout(candidate,occupancy);
        if(!errors.empty()) return {std::nullopt,std::move(errors)};
        ++candidate.revision;
        return {std::move(candidate),{}};
    }
    return {std::nullopt,{"Room not found"}};
}
}
