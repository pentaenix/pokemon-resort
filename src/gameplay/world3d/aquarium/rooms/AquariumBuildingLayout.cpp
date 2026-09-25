#include "gameplay/world3d/aquarium/rooms/AquariumBuildingLayout.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <deque>
#include <map>
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
void pruneSurfaceOverrides(RoomLayout& room) {
    auto& floor=room.surfaces.floor_overrides;
    floor.erase(std::remove_if(floor.begin(),floor.end(),[&](const auto& paint) {
        return paint.cell.column<0||paint.cell.row<0||paint.cell.column>=room.bounds.width||
            paint.cell.row>=room.bounds.depth;
    }),floor.end());
    auto& depths=room.surfaces.floor_depth_overrides;
    depths.erase(std::remove_if(depths.begin(),depths.end(),[&](const auto& item) {
        return item.cell.column<0||item.cell.row<0||item.cell.column>=room.bounds.width||
            item.cell.row>=room.bounds.depth;
    }),depths.end());
    auto& walls=room.surfaces.wall_overrides;
    walls.erase(std::remove_if(walls.begin(),walls.end(),[&](const auto& paint) {
        const int length=horizontal(paint.wall)?room.bounds.width:room.bounds.depth;
        return paint.segment<0||paint.segment>=length;
    }),walls.end());
    room.decorations.erase(std::remove_if(room.decorations.begin(),room.decorations.end(),[&](const auto& item) {
        return item.cell.column<0||item.cell.row<0||item.cell.column>=room.bounds.width||
            item.cell.row>=room.bounds.depth;
    }),room.decorations.end());
    room.transitions.erase(std::remove_if(room.transitions.begin(),room.transitions.end(),[&](const auto& item) {
        return item.lower_cell.column<0||item.lower_cell.row<0||
            item.lower_cell.column>=room.bounds.width||item.lower_cell.row>=room.bounds.depth||
            item.upper_cell.column<0||item.upper_cell.row<0||
            item.upper_cell.column>=room.bounds.width||item.upper_cell.row>=room.bounds.depth;
    }),room.transitions.end());
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
const char* roomTransitionKindName(RoomTransitionKind kind) {
    return kind==RoomTransitionKind::Stairs?"stairs":"ramp";
}
int roomFloorDepth(const RoomLayout& room,Cell cell) {
    const auto found=std::find_if(room.surfaces.floor_depth_overrides.begin(),
        room.surfaces.floor_depth_overrides.end(),[&](const auto& item) {
            return item.cell.column==cell.column&&item.cell.row==cell.row;
        });
    return found==room.surfaces.floor_depth_overrides.end()?0:found->depth;
}
std::vector<Cell> roomTransitionUpperCandidates(const RoomLayout& room,Cell lower) {
    std::vector<Cell> result;
    const int lower_depth=roomFloorDepth(room,lower);
    for(const Cell delta:std::array<Cell,4>{{{0,-1},{1,0},{0,1},{-1,0}}}) {
        const Cell upper{lower.column+delta.column,lower.row+delta.row};
        if(upper.column<0||upper.row<0||upper.column>=room.bounds.width||upper.row>=room.bounds.depth)
            continue;
        if(lower_depth==roomFloorDepth(room,upper)+1) result.push_back(upper);
    }
    return result;
}
const RoomLayout* findRoom(const BuildingLayout& layout,const std::string& id) {
    auto it=std::find_if(layout.rooms.begin(),layout.rooms.end(),[&](const auto& room){return room.id==id;});
    return it==layout.rooms.end() ? nullptr : &*it;
}
RoomLayout* findRoom(BuildingLayout& layout,const std::string& id) {
    auto it=std::find_if(layout.rooms.begin(),layout.rooms.end(),[&](const auto& room){return room.id==id;});
    return it==layout.rooms.end()?nullptr:&*it;
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
        if(room.surfaces.floor_palette<0 || room.surfaces.floor_palette>=kRoomSurfacePaletteCount ||
            room.surfaces.wall_palette<0 || room.surfaces.wall_palette>=kRoomSurfacePaletteCount)
            errors.push_back("Invalid room surface palette: "+room.id);
        std::set<std::pair<int,int>> painted_cells;
        for(const auto& paint:room.surfaces.floor_overrides) {
            if(paint.cell.column<0 || paint.cell.row<0 || paint.cell.column>=room.bounds.width ||
                paint.cell.row>=room.bounds.depth || paint.palette<0 ||
                paint.palette>=kRoomSurfacePaletteCount ||
                !painted_cells.insert({paint.cell.column,paint.cell.row}).second)
                errors.push_back("Invalid/duplicate floor paint: "+room.id);
        }
        std::set<std::pair<int,int>> depth_cells;
        for(const auto& item:room.surfaces.floor_depth_overrides) {
            if(item.cell.column<0||item.cell.row<0||item.cell.column>=room.bounds.width||
                item.cell.row>=room.bounds.depth||item.depth<1||
                item.depth>kMaximumRoomFloorDepth||
                !depth_cells.insert({item.cell.column,item.cell.row}).second)
                errors.push_back("Invalid/duplicate floor depth: "+room.id);
        }
        std::set<std::pair<int,int>> painted_walls;
        for(const auto& paint:room.surfaces.wall_overrides) {
            const int length=horizontal(paint.wall)?room.bounds.width:room.bounds.depth;
            if(!validWall(paint.wall) || paint.segment<0 || paint.segment>=length ||
                paint.palette<0 || paint.palette>=kRoomSurfacePaletteCount ||
                !painted_walls.insert({int(paint.wall),paint.segment}).second)
                errors.push_back("Invalid/duplicate wall paint: "+room.id);
        }
        if(room.decorations.size()>kRoomDecorationLimit) errors.push_back("Too many room decorations: "+room.id);
        std::set<std::string> decoration_ids;
        std::set<std::pair<int,int>> decoration_cells;
        for(const auto& item:room.decorations) {
            if(!validId(item.id)||item.asset_id.empty()||item.asset_id.size()>256||
                item.cell.column<0||item.cell.row<0||item.cell.column>=room.bounds.width||
                item.cell.row>=room.bounds.depth||item.yaw_quarter_turns<0||item.yaw_quarter_turns>3||
                !decoration_ids.insert(item.id).second||
                !decoration_cells.insert({item.cell.column,item.cell.row}).second)
                errors.push_back("Invalid/overlapping room decoration: "+room.id);
        }
        std::set<std::string> transition_ids;
        std::set<std::pair<int,int>> transition_cells;
        for(const auto& item:room.transitions) {
            const int dx=item.upper_cell.column-item.lower_cell.column;
            const int dy=item.upper_cell.row-item.lower_cell.row;
            const bool valid_kind=item.kind==RoomTransitionKind::Ramp||item.kind==RoomTransitionKind::Stairs;
            if(!validId(item.id)||!valid_kind||
                item.lower_cell.column<0||item.lower_cell.row<0||
                item.lower_cell.column>=room.bounds.width||item.lower_cell.row>=room.bounds.depth||
                item.upper_cell.column<0||item.upper_cell.row<0||
                item.upper_cell.column>=room.bounds.width||item.upper_cell.row>=room.bounds.depth||
                std::abs(dx)+std::abs(dy)!=1||
                roomFloorDepth(room,item.lower_cell)!=roomFloorDepth(room,item.upper_cell)+1||
                !transition_ids.insert(item.id).second||
                !transition_cells.insert({item.lower_cell.column,item.lower_cell.row}).second)
                errors.push_back("Invalid/overlapping room transition: "+room.id);
        }
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
            const Cell local{cell.column-room.bounds.column,cell.row-room.bounds.row};
            if(transition_cells.count({local.column,local.row})||
                std::any_of(room.transitions.begin(),room.transitions.end(),[&](const auto& item) {
                    return item.upper_cell.column==local.column&&item.upper_cell.row==local.row;
                }))
                errors.push_back("Room transition intersects a tank: "+room.id);
        }
        for(const auto& item:room.transitions) {
            const Cell lower_global{item.lower_cell.column+room.bounds.column,
                item.lower_cell.row+room.bounds.row};
            const Cell upper_global{item.upper_cell.column+room.bounds.column,
                item.upper_cell.row+room.bounds.row};
            if(lanes.count({lower_global.column,lower_global.row})||
                lanes.count({upper_global.column,upper_global.row}))
                errors.push_back("Room transition intersects a door lane: "+room.id);
            if(decoration_cells.count({item.lower_cell.column,item.lower_cell.row})||
                decoration_cells.count({item.upper_cell.column,item.upper_cell.row}))
                errors.push_back("Room transition intersects a decoration: "+room.id);
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
        pruneSurfaceOverrides(room);
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

LayoutProposal proposeRoomTransition(const BuildingLayout& current,const std::string& room_id,
    Cell lower_cell,RoomTransitionKind kind,const std::vector<RoomOccupancy>& occupancy) {
    auto candidate=current;
    auto* room=findRoom(candidate,room_id);
    if(!room)return {std::nullopt,{"Room not found"}};
    const auto uppers=roomTransitionUpperCandidates(*room,lower_cell);
    if(uppers.empty())return {std::nullopt,{"Choose the lower cell beside a one-level edge"}};
    auto existing=std::find_if(room->transitions.begin(),room->transitions.end(),[&](const auto& item) {
        return item.lower_cell.column==lower_cell.column&&item.lower_cell.row==lower_cell.row;
    });
    Cell upper=uppers.front();
    if(existing!=room->transitions.end()) {
        if(existing->kind!=kind) {
            existing->kind=kind;
            return finish(std::move(candidate),occupancy);
        }
        const auto current_upper=std::find_if(uppers.begin(),uppers.end(),[&](Cell cell) {
            return cell.column==existing->upper_cell.column&&cell.row==existing->upper_cell.row;
        });
        const std::size_t next=current_upper==uppers.end()?0U:
            std::size_t(current_upper-uppers.begin())+1U;
        // Cycling past the final valid edge removes the transition, giving the
        // icon-only tool a reversible no-modal edit path.
        if(next>=uppers.size()) room->transitions.erase(existing);
        else existing->upper_cell=uppers[next];
    } else {
        room->transitions.push_back({"transition_"+std::to_string(current.revision+1)+"_"+
            std::to_string(lower_cell.column)+"_"+std::to_string(lower_cell.row),kind,lower_cell,upper});
    }
    return finish(std::move(candidate),occupancy);
}

LayoutProposal proposeRoomStairFlight(const BuildingLayout& current,const std::string& room_id,
    Cell start,Cell destination,const std::vector<RoomOccupancy>& occupancy) {
    auto candidate=current;
    auto* room=findRoom(candidate,room_id);
    if(!room)return {std::nullopt,{"Room not found"}};
    const int dc=destination.column-start.column,dr=destination.row-start.row;
    const int distance=std::abs(dc)+std::abs(dr);
    if(distance==0) {
        const auto old_size=room->transitions.size();
        room->transitions.erase(std::remove_if(room->transitions.begin(),room->transitions.end(),
            [&](const RoomTransition& item) {
                return (item.lower_cell.column==start.column&&item.lower_cell.row==start.row)||
                    (item.upper_cell.column==start.column&&item.upper_cell.row==start.row);
            }),room->transitions.end());
        if(room->transitions.size()==old_size)
            return {std::nullopt,{"Choose a different aligned floor level"}};
        return finish(std::move(candidate),occupancy);
    }
    if(dc!=0&&dr!=0)return {std::nullopt,{"Stair runs must be straight"}};
    const auto inside_room=[&](Cell cell) {
        return cell.column>=0&&cell.row>=0&&cell.column<room->bounds.width&&
            cell.row<room->bounds.depth;
    };
    if(!inside_room(start)||!inside_room(destination))
        return {std::nullopt,{"Stairs must stay inside the room"}};
    const int start_depth=roomFloorDepth(*room,start);
    const int destination_depth=roomFloorDepth(*room,destination);
    const int depth_change=std::abs(destination_depth-start_depth);
    if(depth_change==0)return {std::nullopt,{"Choose two different floor levels"}};
    if(distance<depth_change)return {std::nullopt,{"The stair run needs one cell per level"}};
    const int step_c=(dc>0)-(dc<0),step_r=(dr>0)-(dr<0);
    std::vector<Cell> path;
    path.reserve(std::size_t(distance+1));
    for(int i=0;i<=distance;++i)path.push_back({start.column+step_c*i,start.row+step_r*i});
    std::map<std::pair<int,int>,int> depths;
    for(const auto& item:room->surfaces.floor_depth_overrides)
        depths[{item.cell.column,item.cell.row}]=item.depth;
    const int direction=destination_depth>start_depth?1:-1;
    for(int i=0;i<=distance;++i) {
        const int offset=(depth_change*i+distance/2)/distance;
        const int depth=start_depth+direction*offset;
        const auto key=std::make_pair(path[std::size_t(i)].column,path[std::size_t(i)].row);
        if(depth==0)depths.erase(key);else depths[key]=depth;
    }
    room->surfaces.floor_depth_overrides.clear();
    for(const auto& [cell,depth]:depths)
        room->surfaces.floor_depth_overrides.push_back({{cell.first,cell.second},depth});
    const auto on_path=[&](Cell cell) {
        return std::find_if(path.begin(),path.end(),[&](Cell point) {
            return point.column==cell.column&&point.row==cell.row;
        })!=path.end();
    };
    room->transitions.erase(std::remove_if(room->transitions.begin(),room->transitions.end(),
        [&](const RoomTransition& item) {return on_path(item.lower_cell)||on_path(item.upper_cell);}),
        room->transitions.end());
    const auto depth_at=[&](Cell cell) {
        const auto found=depths.find({cell.column,cell.row});
        return found==depths.end()?0:found->second;
    };
    for(int i=0;i<distance;++i) {
        const auto a=path[std::size_t(i)],b=path[std::size_t(i+1)];
        const int ad=depth_at(a),bd=depth_at(b);
        if(ad==bd)continue;
        room->transitions.push_back({"stairs_"+std::to_string(current.revision+1)+"_"+
            std::to_string(i),RoomTransitionKind::Stairs,ad>bd?a:b,ad>bd?b:a});
    }
    return finish(std::move(candidate),occupancy);
}

LayoutProposal proposeRoomStairArea(const BuildingLayout& current,const std::string& room_id,
    Cell first,Cell last,const std::vector<RoomOccupancy>& occupancy) {
    auto candidate=current;
    auto* room=findRoom(candidate,room_id);
    if(!room)return {std::nullopt,{"Room not found"}};
    const int min_c=std::min(first.column,last.column),max_c=std::max(first.column,last.column);
    const int min_r=std::min(first.row,last.row),max_r=std::max(first.row,last.row);
    if(min_c<0||min_r<0||max_c>=room->bounds.width||max_r>=room->bounds.depth)
        return {std::nullopt,{"Stairs must stay inside the room"}};
    struct Attachment {int dc=0,dr=0,run=0,width=0,outside_depth=0,far_depth=0,score=0;};
    std::vector<Attachment> choices;
    const auto consider=[&](int dc,int dr,int run,int width) {
        std::optional<int> outside_depth,far_depth;
        for(int across=0;across<width;++across) {
            const Cell near=dc?Cell{dc>0?max_c:min_c,min_r+across}:
                Cell{min_c+across,dr>0?max_r:min_r};
            const Cell outside{near.column+dc,near.row+dr};
            const Cell far=dc?Cell{dc>0?min_c:max_c,min_r+across}:
                Cell{min_c+across,dr>0?min_r:max_r};
            if(outside.column<0||outside.row<0||outside.column>=room->bounds.width||
                outside.row>=room->bounds.depth)return;
            const int od=roomFloorDepth(*room,outside),fd=roomFloorDepth(*room,far);
            if((outside_depth&&*outside_depth!=od)||(far_depth&&*far_depth!=fd))return;
            outside_depth=od;far_depth=fd;
        }
        // Stairs are additive: the marked footprint must be one untouched
        // lower platform and the neighboring attachment must be higher.
        if(*outside_depth>=*far_depth)return;
        for(int row=min_r;row<=max_r;++row)for(int column=min_c;column<=max_c;++column)
            if(roomFloorDepth(*room,{column,row})!=*far_depth)return;
        const int change=*far_depth-*outside_depth;
        if(change<=run)
            choices.push_back({dc,dr,run,width,*outside_depth,*far_depth,change});
    };
    consider(0,-1,max_r-min_r+1,max_c-min_c+1);
    consider(1,0,max_c-min_c+1,max_r-min_r+1);
    consider(0,1,max_r-min_r+1,max_c-min_c+1);
    consider(-1,0,max_c-min_c+1,max_r-min_r+1);
    if(choices.empty())return {std::nullopt,{"Draw the stair area beside a different floor level"}};
    const auto attachment=*std::max_element(choices.begin(),choices.end(),[](const auto& a,const auto& b) {
        return a.score<b.score;
    });
    std::map<std::pair<int,int>,int> depths;
    for(const auto& item:room->surfaces.floor_depth_overrides)
        depths[{item.cell.column,item.cell.row}]=item.depth;
    const int direction=attachment.far_depth>attachment.outside_depth?1:-1;
    std::vector<Cell> area;
    // Extra selected length is only an aiming convenience. A rise of N levels
    // always consumes exactly N cells; width remains fully player-authored.
    for(int along=0;along<attachment.score;++along)for(int across=0;across<attachment.width;++across) {
        const Cell near=attachment.dc?Cell{attachment.dc>0?max_c:min_c,min_r+across}:
            Cell{min_c+across,attachment.dr>0?max_r:min_r};
        const Cell cell{near.column-attachment.dc*along,near.row-attachment.dr*along};
        const int depth=attachment.outside_depth+direction*std::min(along+1,attachment.score);
        area.push_back(cell);
        const auto key=std::make_pair(cell.column,cell.row);
        if(depth==0)depths.erase(key);else depths[key]=depth;
    }
    room->surfaces.floor_depth_overrides.clear();
    for(const auto& [cell,depth]:depths)
        room->surfaces.floor_depth_overrides.push_back({{cell.first,cell.second},depth});
    const auto in_area=[&](Cell cell) {return std::any_of(area.begin(),area.end(),[&](Cell point) {
        return point.column==cell.column&&point.row==cell.row;
    });};
    room->transitions.erase(std::remove_if(room->transitions.begin(),room->transitions.end(),
        [&](const RoomTransition& item) {return in_area(item.lower_cell)||in_area(item.upper_cell);}),
        room->transitions.end());
    const auto depth_at=[&](Cell cell) {const auto found=depths.find({cell.column,cell.row});
        return found==depths.end()?0:found->second;};
    int transition_index=0;
    for(int along=0;along<attachment.score;++along)for(int across=0;across<attachment.width;++across) {
        const Cell near=attachment.dc?Cell{attachment.dc>0?max_c:min_c,min_r+across}:
            Cell{min_c+across,attachment.dr>0?max_r:min_r};
        const Cell a{near.column-attachment.dc*along,near.row-attachment.dr*along};
        const Cell b{a.column+attachment.dc,a.row+attachment.dr};
        const int ad=depth_at(a),bd=depth_at(b);
        room->transitions.push_back({"stairs_"+std::to_string(current.revision+1)+"_"+
            std::to_string(transition_index++),RoomTransitionKind::Stairs,ad>bd?a:b,ad>bd?b:a});
    }
    return finish(std::move(candidate),occupancy);
}

LayoutProposal proposeRoomStairRemovalArea(const BuildingLayout& current,
    const std::string& room_id,Cell first,Cell last,
    const std::vector<RoomOccupancy>& occupancy) {
    auto candidate=current;
    auto* room=findRoom(candidate,room_id);
    if(!room)return {std::nullopt,{"Room not found"}};
    const int min_c=std::min(first.column,last.column),max_c=std::max(first.column,last.column);
    const int min_r=std::min(first.row,last.row),max_r=std::max(first.row,last.row);
    const auto selected=[&](Cell cell) {return cell.column>=min_c&&cell.column<=max_c&&
        cell.row>=min_r&&cell.row<=max_r;};
    const auto group_key=[](const RoomTransition& item) {
        if(item.id.rfind("stairs_",0)!=0)return item.id;
        const auto suffix=item.id.rfind('_');
        return suffix==std::string::npos?item.id:item.id.substr(0,suffix);
    };
    std::set<std::string> groups;
    for(const auto& item:room->transitions)
        if(selected(item.lower_cell)||selected(item.upper_cell))groups.insert(group_key(item));
    if(groups.empty())return {std::nullopt,{"Mark an existing stair block to remove it"}};
    std::map<std::pair<int,int>,int> depths;
    for(const auto& item:room->surfaces.floor_depth_overrides)
        depths[{item.cell.column,item.cell.row}]=item.depth;
    for(const auto& group:groups) {
        int restore_depth=0;
        std::vector<Cell> generated_cells;
        for(const auto& item:room->transitions)if(group_key(item)==group) {
            restore_depth=std::max(restore_depth,roomFloorDepth(*room,item.lower_cell));
            if(item.id.rfind("stairs_",0)==0)generated_cells.push_back(item.lower_cell);
        }
        for(const auto cell:generated_cells) {
            const auto key=std::make_pair(cell.column,cell.row);
            if(restore_depth==0)depths.erase(key);else depths[key]=restore_depth;
        }
    }
    room->transitions.erase(std::remove_if(room->transitions.begin(),room->transitions.end(),
        [&](const RoomTransition& item) {return groups.count(group_key(item))!=0;}),
        room->transitions.end());
    room->surfaces.floor_depth_overrides.clear();
    for(const auto& [cell,depth]:depths)
        room->surfaces.floor_depth_overrides.push_back({{cell.first,cell.second},depth});
    return finish(std::move(candidate),occupancy);
}
} // namespace pr::gameplay::world3d::aquarium::rooms
