#include "gameplay/world3d/aquarium/rooms/AquariumRoomRuntime.hpp"
#include "gameplay/world3d/interiors/DefaultRoom.hpp"
#include <algorithm>
#include <stdexcept>

namespace pr::gameplay::world3d::aquarium::rooms {
namespace {
Wall wallFor(const std::string& edge) {
    if (edge=="north") return Wall::North;
    if (edge=="east") return Wall::East;
    if (edge=="west") return Wall::West;
    return Wall::South;
}

template<class T> void resizePlane(std::vector<std::vector<T>>& plane,int w,int h,T empty={}) {
    plane.resize(h);
    for (auto& row:plane) row.resize(w,empty);
}
std::uint8_t rampSpecial(Cell lower,Cell upper) {
    const int dx=upper.column-lower.column,dy=upper.row-lower.row;
    if(dy==-1)return 2; // north
    if(dx==1)return 3;  // east
    if(dy==1)return 4;  // south
    return 5;           // west
}
}

void applyRoomSurfaceStyle(SceneConfig& scene,const RoomSurfaceStyle& style) {
    const auto& floor=roomSurfacePalette(style.floor_palette);
    const auto& wall=roomSurfacePalette(style.wall_palette);
    auto& room=scene.interior.default_room;
    room.floor_color_a=floor.floor_a; room.floor_color_b=floor.floor_b;
    room.wall_color_ns=wall.wall_ns; room.wall_color_ew=wall.wall_ew;
    room.trim_color=wall.trim; room.baseboard_color=wall.baseboard;
    room.floor_color_overrides.clear();
    for(const auto& override:style.floor_overrides) {
        const auto& palette=roomSurfacePalette(override.palette);
        room.floor_color_overrides.push_back({override.cell.column,override.cell.row,
            palette.floor_a,palette.floor_b});
    }
    room.wall_color_overrides.clear();
    for(const auto& override:style.wall_overrides) {
        const auto& palette=roomSurfacePalette(override.palette);
        room.wall_color_overrides.push_back({wallName(override.wall),override.segment,
            override.wall==Wall::North||override.wall==Wall::South?palette.wall_ns:palette.wall_ew,
            palette.trim,palette.baseboard});
    }
}

BuildingLayout roomLayoutFromScene(const SceneConfig& scene) {
    BuildingLayout result;
    result.id="aquarium-building";
    RoomLayout room{scene.id,{0,0,scene.grid.width,scene.grid.height},{}};
    for (const auto& opening:scene.interior.openings) {
        const Wall wall=wallFor(opening.edge);
        const int offset=(opening.from+opening.to)/2;
        const auto trigger=std::find_if(scene.door_triggers.begin(),scene.door_triggers.end(),[&](const auto& t) {
            return (wall==Wall::North || wall==Wall::South) ? t.tile_x==offset : t.tile_y==offset;
        });
        if (trigger==scene.door_triggers.end()) continue;
        const auto link=std::find_if(scene.links.begin(),scene.links.end(),[&](const auto& l){return l.id==trigger->link_id;});
        if (link==scene.links.end()) continue;
        room.doors.push_back({link->id,wall,offset});
        result.external_connections.push_back({link->id,{scene.id,link->id},
            link->destination_map_id,link->destination_anchor_id});
    }
    result.rooms.push_back(std::move(room));
    return result;
}

bool roomDrawingCellFits(const SceneConfig& scene,int column,int row) {
    // Tanks occupy [cell+.5, cell+1.5], not the walking cell's [cell,cell+1].
    // Keep at least half a cell between the entire footprint and the inner wall
    // face (also leaves room for the bezel). Never clip a drawing cell in half.
    const float wall=std::max(0.0f,scene.interior.default_room.wall_face_offset_tiles);
    constexpr float gap=0.5f;
    return column+0.5f>=wall+gap && row+0.5f>=wall+gap &&
        column+1.5f<=scene.grid.width-wall-gap &&
        row+1.5f<=scene.grid.height-wall-gap;
}

SceneConfig projectRoomLayout(const SceneConfig& source,const RoomLayout& room) {
    if (room.id!=source.id ||
        room.bounds.width<8 || room.bounds.depth<8 || room.bounds.width>128 || room.bounds.depth>128 ||
        !source.models.empty() || !source.characters.empty() || !source.interior.shell_model_id.empty())
        throw std::runtime_error("Room cannot be resized by the procedural adapter");
    auto scene=source;
    applyRoomSurfaceStyle(scene,room.surfaces);
    const auto old=roomLayoutFromScene(source).rooms.front();
    scene.grid.width=room.bounds.width;
    scene.grid.height=room.bounds.depth;
    scene.interior.grid_origin_x=room.bounds.column;
    scene.interior.grid_origin_y=room.bounds.row;
    constexpr int kRoomFloorLevels=kMaximumRoomFloorDepth;
    const float floor_step=scene.terrain.height_per_floor>0.0f?
        scene.terrain.height_per_floor:scene.grid.tile_size;
    scene.terrain.base_height_world=-float(kRoomFloorLevels)*floor_step;
    scene.terrain.heights.assign(scene.grid.height,
        std::vector<std::uint8_t>(scene.grid.width,std::uint8_t(kRoomFloorLevels)));
    for(const auto& item:room.surfaces.floor_depth_overrides)
        if(item.cell.column>=0&&item.cell.row>=0&&item.cell.column<scene.grid.width&&item.cell.row<scene.grid.height)
            scene.terrain.heights[std::size_t(item.cell.row)][std::size_t(item.cell.column)]=
                std::uint8_t(kRoomFloorLevels-std::clamp(item.depth,0,kRoomFloorLevels));
    resizePlane(scene.terrain.specials,scene.grid.width,scene.grid.height,std::uint8_t{0});
    resizePlane(scene.terrain.collision,scene.grid.width,scene.grid.height,std::uint8_t{0});
    scene.terrain.transition_edges.clear();
    resizePlane(scene.tile_surfaces.cells,scene.grid.width,scene.grid.height,TileSurfaceInfo{});
    constexpr const char* kTransitionLayer="aquarium_room_transitions";
    auto transition_layer=std::find_if(scene.tile_layers.layers.begin(),scene.tile_layers.layers.end(),
        [](const auto& layer){return layer.id==kTransitionLayer;});
    if(transition_layer==scene.tile_layers.layers.end()) {
        scene.tile_layers.layers.push_back({kTransitionLayer,true,{}});
        transition_layer=std::prev(scene.tile_layers.layers.end());
    }
    resizePlane(transition_layer->cells,scene.grid.width,scene.grid.height,-1);
    for(const auto& transition:room.transitions) {
        const auto lower=transition.lower_cell;
        if(lower.column<0||lower.row<0||lower.column>=scene.grid.width||lower.row>=scene.grid.height)continue;
        scene.terrain.specials[std::size_t(lower.row)][std::size_t(lower.column)]=
            rampSpecial(lower,transition.upper_cell);
        scene.terrain.transition_edges.push_back({
            lower.column,lower.row,transition.upper_cell.column,transition.upper_cell.row,
            true}); // Legacy saved ramps are presented as stairs.
    }
    scene.interior.openings.clear();
    for (const auto& door:room.doors) {
        const auto* previous=findDoor(old,door.id);
        if (!previous || previous->wall!=door.wall) throw std::runtime_error("Room door identity changed");
        const auto before=doorLandingCell(old,*previous), after=doorLandingCell(room,door);
        const int dx=after.column-room.bounds.column-before.column,
            dy=after.row-room.bounds.row-before.row;
        for (auto& trigger:scene.door_triggers) if (trigger.link_id==door.id) {
            trigger.tile_x+=dx; trigger.tile_y+=dy;
            if (trigger.visual.enabled) {trigger.visual.tile_x+=dx; trigger.visual.tile_y+=dy;}
        }
        for (auto& anchor:scene.anchors) if (anchor.tile_x==before.column && anchor.tile_y==before.row) {
            anchor.tile_x=after.column; anchor.tile_y=after.row;
        }
        scene.interior.openings.push_back({wallName(door.wall),door.offset-1,door.offset+1});
    }
    return scene;
}

AquariumConstructionConfig roomConstructionConfig(
    const SceneConfig& scene,const AquariumConstructionConfig& style,bool regenerate) {
    auto config=style;
    if (regenerate) {
        config.allowed_cells.clear();
        for (int y=0;y<scene.grid.height;++y) for(int x=0;x<scene.grid.width;++x)
            config.allowed_cells.push_back({x,y});
    }
    const auto room=roomLayoutFromScene(scene).rooms.front();
    std::vector<Cell> lanes;
    for(const auto& door:room.doors) {
        auto cells=protectedDoorCells(room,door);
        lanes.insert(lanes.end(),cells.begin(),cells.end());
    }
    auto& cells=config.allowed_cells;
    config.legacy_wall_cells.clear();
    for (auto p:cells) if(p.column>=0 && p.row>=0 &&
        p.column<scene.grid.width && p.row<scene.grid.height &&
        !roomDrawingCellFits(scene,p.column,p.row)) config.legacy_wall_cells.push_back(p);
    cells.erase(std::remove_if(cells.begin(),cells.end(),[&](auto p) {
        if (!roomDrawingCellFits(scene,p.column,p.row)) return true;
        // Shifted footprint touches four walking cells, all must clear the lane.
        return std::any_of(lanes.begin(),lanes.end(),[&](auto lane) {
            return lane.column>=p.column && lane.column<=p.column+1 &&
                lane.row>=p.row && lane.row<=p.row+1;
        });
    }),cells.end());
    if (!room.doors.empty()) {
        const auto landing=doorLandingCell(room,room.doors.front());
        config.has_return_cell=true;
        config.return_cell={landing.column,landing.row};
        config.return_facing=wallName(oppositeWall(room.doors.front().wall));
    }
    return config;
}
} // namespace pr::gameplay::world3d::aquarium::rooms
