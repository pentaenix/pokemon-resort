#include "ui/Overworld3DTestScreen.hpp"
#include "gameplay/world3d/rendering/PixelScale.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <map>
#include <set>

namespace pr {
namespace rooms=gameplay::world3d::aquarium::rooms;
namespace aqc=gameplay::world3d::aquarium::construction;
namespace {
int wallCoordinate(rooms::RoomBounds b,int wall) {
    switch(wall) {case 0:return b.row;case 1:return b.column+b.width;
    case 2:return b.row+b.depth;default:return b.column;}
}
rooms::RoomBounds movedBounds(rooms::RoomBounds b,int wall,int value) {
    switch(wall) {
    case 0:b.depth+=b.row-value;b.row=value;break;
    case 1:b.width=value-b.column;break;
    case 2:b.depth=value-b.row;break;
    case 3:b.width+=b.column-value;b.column=value;break;
    }
    return b;
}
std::optional<rooms::Wall> wallAt(rooms::Cell cell,int width,int depth) {
    if(cell.row==0) return rooms::Wall::North;
    if(cell.row==depth-1) return rooms::Wall::South;
    if(cell.column==0) return rooms::Wall::West;
    if(cell.column==width-1) return rooms::Wall::East;
    return std::nullopt;
}
int wallSegment(rooms::Wall wall,rooms::Cell cell) {
    return wall==rooms::Wall::North||wall==rooms::Wall::South?cell.column:cell.row;
}
}

void Overworld3DTestScreen::appendAquariumRoomPreview(aqc::AquariumConstructionVisual& visual) const {
    visual.state=aqc::ConstructionState::ResizeRoom;
    visual.room_edit_mode=aquarium_room_edit_mode_;
    visual.room_palette_index=aquarium_room_palette_index_;
    visual.room_top_down=aquarium_room_terrain_top_down_;
    visual.room_transition_kind=aquarium_room_transition_kind_==rooms::RoomTransitionKind::Stairs?1:0;
    visual.draft_valid=aquarium_room_error_.empty();
    visual.status_hint=aquarium_room_error_;
    if(visual.status_hint.empty()) {
        switch(aquarium_room_edit_mode_) {
        case aqc::ConstructionRoomEditMode::Layout:
            visual.status_hint="Resize the room or add and move doors"; break;
        case aqc::ConstructionRoomEditMode::Floor:
            visual.status_hint="Choose a swatch, then click or drag across floor cells"; break;
        case aqc::ConstructionRoomEditMode::Levels:
            visual.status_hint=aquarium_room_level_selection_ready_?
                "Move the depth knob; Cancel restores the selected floor":
                "Mark a floor section, then adjust its depth knob"; break;
        case aqc::ConstructionRoomEditMode::Transitions:
            visual.status_hint="Mark lower-floor blocks to build up; mark stair blocks to remove"; break;
        case aqc::ConstructionRoomEditMode::Walls:
            visual.status_hint="Choose a swatch, then click or drag along one wall"; break;
        case aqc::ConstructionRoomEditMode::Decorations:
            visual.status_hint="Room decorations use the room asset library"; break;
        }
    }
    visual.focused_action=aquarium_construction_focused_action_;
    const float tile=scene_.grid.tile_size;
    const float inset=scene_.interior.default_room.wall_face_offset_tiles*tile;
    const auto b=*aquarium_room_draft_;
    const float x=(b.column-scene_.interior.grid_origin_x)*tile;
    const float z=(b.row-scene_.interior.grid_origin_y)*tile;
    visual.room_outline=std::array<float,4>{x+inset,z+inset,x+b.width*tile-inset,z+b.depth*tile-inset};
    if(aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Floor ||
        aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Levels ||
        aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Transitions ||
        aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Walls ||
        aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Decorations) {
        visual.placement_offset_world_units=0;
        visual.cells.clear();
        const auto* candidate_room=rooms::findRoom(*aquarium_room_candidate_,scene_.id);
        std::vector<int> depths(std::size_t(b.width*b.depth),0);
        if(candidate_room)for(const auto& item:candidate_room->surfaces.floor_depth_overrides)
            if(item.cell.column>=0&&item.cell.row>=0&&item.cell.column<b.width&&item.cell.row<b.depth)
                depths[std::size_t(item.cell.row*b.width+item.cell.column)]=item.depth;
        for(int row=0;row<b.depth;++row) for(int column=0;column<b.width;++column) {
            const int depth=depths[std::size_t(row*b.width+column)];
            visual.cells.push_back({{column,row},scene_.interior.floor_datum-depth*tile,false,depth});
        }
        if(candidate_room&&aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Transitions) {
            for(const auto& transition:candidate_room->transitions) {
                visual.room_transition_cells.push_back({transition.lower_cell.column,
                    transition.lower_cell.row});
            }
        }
        if(aquarium_room_paint_gesture_) {
            const auto& gesture=*aquarium_room_paint_gesture_;
            if(gesture.mode==aqc::ConstructionRoomEditMode::Floor ||
                gesture.mode==aqc::ConstructionRoomEditMode::Levels) {
                for(int row=std::min(gesture.start.row,gesture.current.row);
                    row<=std::max(gesture.start.row,gesture.current.row);++row)
                    for(int column=std::min(gesture.start.column,gesture.current.column);
                        column<=std::max(gesture.start.column,gesture.current.column);++column)
                        visual.draft_cells.push_back({column,row});
            } else if(gesture.mode==aqc::ConstructionRoomEditMode::Transitions) {
                const int min_row=std::min(gesture.start.row,gesture.current.row);
                const int max_row=std::max(gesture.start.row,gesture.current.row);
                const int min_column=std::min(gesture.start.column,gesture.current.column);
                const int max_column=std::max(gesture.start.column,gesture.current.column);
                const auto preview=rooms::proposeRoomStairArea(*aquarium_room_candidate_,scene_.id,
                    gesture.start,gesture.current);
                const std::string preview_prefix="stairs_"+
                    std::to_string(aquarium_room_candidate_->revision+1)+"_";
                if(preview.document)if(const auto* preview_room=rooms::findRoom(*preview.document,scene_.id))
                    for(const auto& transition:preview_room->transitions)
                        if(transition.id.rfind(preview_prefix,0)==0&&
                            transition.lower_cell.column>=min_column&&
                            transition.lower_cell.column<=max_column&&
                            transition.lower_cell.row>=min_row&&transition.lower_cell.row<=max_row)
                            visual.draft_cells.push_back({transition.lower_cell.column,
                                transition.lower_cell.row});
                if(visual.draft_cells.empty())
                    for(int row=min_row;row<=max_row;++row)
                        for(int column=min_column;column<=max_column;++column)
                            visual.draft_cells.push_back({column,row});
            } else if(gesture.wall) {
                const auto current_wall=wallAt(gesture.current,b.width,b.depth);
                if(current_wall==gesture.wall) {
                    const int first=std::min(wallSegment(*gesture.wall,gesture.start),
                        wallSegment(*gesture.wall,gesture.current));
                    const int last=std::max(wallSegment(*gesture.wall,gesture.start),
                        wallSegment(*gesture.wall,gesture.current));
                    for(int segment=first;segment<=last;++segment)
                        visual.draft_cells.push_back((*gesture.wall==rooms::Wall::North)?
                            pr::aquarium::geometry::GridCell{segment,0}:
                            (*gesture.wall==rooms::Wall::South)?
                                pr::aquarium::geometry::GridCell{segment,b.depth-1}:
                            (*gesture.wall==rooms::Wall::West)?
                                pr::aquarium::geometry::GridCell{0,segment}:
                                pr::aquarium::geometry::GridCell{b.width-1,segment});
                }
            }
        }
    }
    if(aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Levels&&
        aquarium_room_level_selection_ready_&&aquarium_room_paint_gesture_) {
        const auto& selection=*aquarium_room_paint_gesture_;
        const float center_x=(std::min(selection.start.column,selection.current.column)+
            std::max(selection.start.column,selection.current.column)+1)*tile*.5f;
        const float center_z=(std::min(selection.start.row,selection.current.row)+
            std::max(selection.start.row,selection.current.row)+1)*tile*.5f;
        const int w=app_config_.window.virtual_width,h=app_config_.window.virtual_height;
        const auto viewport=visibleWorldViewportRect(w,h);
        const int pw=scene_.world_viewport.enabled?
            gameplay::world3d::rendering::worldViewportBaseWidth(scene_):w;
        const int ph=scene_.world_viewport.enabled?
            gameplay::world3d::rendering::worldViewportBaseHeight(scene_):h;
        float sx=0,sy=0,depth=0;
        if(camera_.worldToScreen({center_x,scene_.interior.floor_datum,center_z},
            pw,ph,sx,sy,depth)) {
            sx=viewport.x+sx*viewport.w/std::max(1,pw);
            sy=viewport.y+sy*viewport.h/std::max(1,ph);
            aqc::AquariumConstructionVisual::RoomLevelControl control;
            control.x=std::clamp(sx+64.0f,48.0f,float(std::max(48,w-112)));
            const float half=std::min(105.0f,float(std::max(70,h/5)));
            control.y0=std::clamp(sy-half,112.0f,float(std::max(112,h-245)));
            control.y1=std::min(float(h-72),control.y0+half*2.0f);
            control.depth=aquarium_room_palette_index_;
            control.knob_y=control.y0+(control.y1-control.y0)*
                float(control.depth)/float(rooms::kMaximumRoomFloorDepth);
            visual.room_level_control=control;
        }
    }
    for(const auto& door:rooms::findRoom(*aquarium_room_candidate_,scene_.id)->doors) {
        const bool horizontal=door.wall==rooms::Wall::North || door.wall==rooms::Wall::South;
        const float start=(door.offset-1)*tile, end=(door.offset+2)*tile;
        if(horizontal) {
            const float edge=door.wall==rooms::Wall::North?z+inset:z+b.depth*tile-inset;
            visual.room_portals.push_back({x+start,edge,x+end,edge});
        } else {
            const float edge=door.wall==rooms::Wall::West?x+inset:x+b.width*tile-inset;
            visual.room_portals.push_back({edge,z+start,edge,z+end});
        }
    }
    if(aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Walls && aquarium_room_paint_wall_) {
        const auto wall=*aquarium_room_paint_wall_;
        const bool horizontal=wall==rooms::Wall::North||wall==rooms::Wall::South;
        const int count=horizontal?b.width:b.depth;
        for(int segment=0;segment<count;++segment) {
            aqc::AquariumConstructionVisual::RoomWallStrip strip;
            if(horizontal) {
                strip.x0=x+segment*tile;strip.x1=x+(segment+1)*tile;
                strip.z0=strip.z1=wall==rooms::Wall::North?z+inset:z+b.depth*tile-inset;
            } else {
                strip.z0=z+segment*tile;strip.z1=z+(segment+1)*tile;
                strip.x0=strip.x1=wall==rooms::Wall::West?x+inset:x+b.width*tile-inset;
            }
            strip.y0=scene_.interior.floor_datum+.4f;
            strip.y1=scene_.interior.floor_datum+scene_.interior.default_room.wall_height_tiles*tile;
            if(aquarium_room_paint_gesture_&&aquarium_room_paint_gesture_->wall==wall) {
                const int first=std::min(wallSegment(wall,aquarium_room_paint_gesture_->start),
                    wallSegment(wall,aquarium_room_paint_gesture_->current));
                const int last=std::max(wallSegment(wall,aquarium_room_paint_gesture_->start),
                    wallSegment(wall,aquarium_room_paint_gesture_->current));
                strip.active=segment>=first&&segment<=last;
            }
            visual.room_wall_strips.push_back(strip);
        }
    }
    if(aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Decorations) {
        const auto* room=rooms::findRoom(*aquarium_room_candidate_,scene_.id);
        const rooms::RoomDecoration* object=room_decoration_draft_?&*room_decoration_draft_:nullptr;
        if(!object&&room&&room_decoration_selected_) {
            const auto found=std::find_if(room->decorations.begin(),room->decorations.end(),[&](const auto& item){
                return item.id==*room_decoration_selected_;});
            if(found!=room->decorations.end())object=&*found;
        }
        visual.room_decoration_collision_preview=true;
        std::set<std::pair<int,int>> blocked;
        // Tank construction is half a room cell offset on both axes. Each
        // tank cell therefore overlaps four room-placement cells.
        for(const auto& tank:aquarium_construction_.committedDesign().tanks)
            for(const auto cell:aqc::tankFootprintCells(tank))
                for(int row=0;row<2;++row)for(int column=0;column<2;++column)
                    blocked.emplace(cell.column+column,cell.row+row);
        if(room) {
            for(const auto& door:room->doors)for(auto cell:rooms::protectedDoorCells(*room,door))
                blocked.emplace(cell.column-room->bounds.column,cell.row-room->bounds.row);
            for(const auto& item:room->decorations) {
                if(object&&item.id==object->id)continue;
                const auto* asset=room_decoration_catalog_.resolve(item.asset_id);
                if(!asset){blocked.emplace(item.cell.column,item.cell.row);continue;}
                for(const auto cell:rooms::roomDecorationPlacementCells(
                    *asset,item.cell,item.yaw_quarter_turns,tile))blocked.emplace(cell.column,cell.row);
            }
        }
        for(auto& surface:visual.cells)
            surface.blocked=blocked.count({surface.cell.column,surface.cell.row})!=0;
        if(object) {
            const auto* asset=room_decoration_catalog_.resolve(object->asset_id);
            const auto footprint=asset?rooms::roomDecorationPlacementCells(
                *asset,object->cell,object->yaw_quarter_turns,tile):std::vector<rooms::Cell>{object->cell};
            for(const auto cell:footprint) {
                if(room_decoration_draft_)visual.draft_cells.push_back({cell.column,cell.row});
                else visual.selected_cells.push_back({cell.column,cell.row});
            }
            const int w=app_config_.window.virtual_width,h=app_config_.window.virtual_height;
            const auto viewport=visibleWorldViewportRect(w,h);
            const int pw=scene_.world_viewport.enabled?gameplay::world3d::rendering::worldViewportBaseWidth(scene_):w;
            const int ph=scene_.world_viewport.enabled?gameplay::world3d::rendering::worldViewportBaseHeight(scene_):h;
            float sx=0,sy=0,depth=0;
            if(camera_.worldToScreen({(object->cell.column+.5f)*tile,scene_.interior.floor_datum+tile*.6f,
                (object->cell.row+.5f)*tile},pw,ph,sx,sy,depth)) {
                sx=viewport.x+sx*viewport.w/std::max(1,pw);sy=viewport.y+sy*viewport.h/std::max(1,ph);
                visual.room_handles.push_back({sx,sy,0,false,room_decoration_dragging_,1});
                visual.room_handles.push_back({sx+58,sy,0,false,false,2});
                visual.room_handles.push_back({sx-58,sy,0,false,false,3});
            }
        }
        return;
    }
    if(aquarium_room_edit_mode_!=aqc::ConstructionRoomEditMode::Layout) return;
    const int w=app_config_.window.virtual_width,h=app_config_.window.virtual_height;
    const auto viewport=visibleWorldViewportRect(w,h);
    const int pw=scene_.world_viewport.enabled?gameplay::world3d::rendering::worldViewportBaseWidth(scene_):w;
    const int ph=scene_.world_viewport.enabled?gameplay::world3d::rendering::worldViewportBaseHeight(scene_):h;
    const auto* room=rooms::findRoom(aquarium_room_gesture_?aquarium_room_gesture_->baseline:
        *aquarium_room_candidate_,scene_.id);
    for(int wall=0;wall<4;++wall) {
        const float wx=wall==1?x+b.width*tile-inset:wall==3?x+inset:x+b.width*tile*.5f;
        const float wz=wall==0?z+inset:wall==2?z+b.depth*tile-inset:z+b.depth*tile*.5f;
        float sx=0,sy=0,depth=0;
        if(!camera_.worldToScreen({wx,scene_.interior.floor_datum+tile*.3f,wz},pw,ph,sx,sy,depth)) {
            sx=pw*.5f;sy=wall==0?0:float(ph);
        }
        sx=viewport.x+sx*viewport.w/std::max(1,pw);
        sy=viewport.y+sy*viewport.h/std::max(1,ph);
        // Handles remain reachable even when a long wall extends off-screen.
        sx=std::clamp(sx,42.0f,float(std::max(42,w-106)));
        sy=std::clamp(sy,105.0f,float(std::max(105,h-106)));
        visual.room_handles.push_back({sx,sy,wall,false,false});
        const bool has_door=std::any_of(room->doors.begin(),room->doors.end(),[&](const auto& d){return int(d.wall)==wall;});
        if(!has_door) visual.room_handles.push_back({
            sx+((wall==0||wall==2)?56.0f:0.0f),
            sy+((wall==1||wall==3)?56.0f:0.0f),wall,true,false});
    }
    // Keep clamped controls distinct: long rooms may project multiple walls onto one edge.
    for(std::size_t i=0;i<visual.room_handles.size();++i) {
        auto& handle=visual.room_handles[i];
        const auto clear=[&](float x,float y) {
            for(std::size_t j=0;j<i;++j) if(std::hypot(x-visual.room_handles[j].x,
                y-visual.room_handles[j].y)<52) return false;
            return true;
        };
        if(!clear(handle.x,handle.y)) {
            float best=1e30f,bx=handle.x,by=handle.y;
            for(float y=105;y<h-35;y+=56) for(float x=35;x<w-35;x+=56) {
                const float distance=(x-handle.x)*(x-handle.x)+(y-handle.y)*(y-handle.y);
                if(distance<best && clear(x,y)) {best=distance;bx=x;by=y;}
            }
            handle.x=bx;handle.y=by;
        }
        handle.focused=aquarium_room_gesture_ ?
            handle.wall==aquarium_room_gesture_->wall && handle.add_door==aquarium_room_gesture_->add_door :
            int(i)==aquarium_room_handle_focus_;
    }
}

bool Overworld3DTestScreen::handleAquariumRoomPointer(int x,int y,bool press,bool release) {
    if(!aquarium_room_draft_) return false;
    const auto level_cell_at=[&](int screen_x,int screen_y)
            -> std::optional<pr::aquarium::geometry::GridCell> {
        // Levels and stairs are edited on one stable logical plane. Picking the
        // stepped render surface makes the same pointer jump between cells as
        // the floor changes height and is especially unusable from above.
        const int sw=std::max(1,app_config_.window.virtual_width);
        const int sh=std::max(1,app_config_.window.virtual_height);
        const auto viewport=visibleWorldViewportRect(sw,sh);
        if(screen_x<viewport.x||screen_y<viewport.y||screen_x>=viewport.x+viewport.w||
            screen_y>=viewport.y+viewport.h)return std::nullopt;
        const int pw=scene_.world_viewport.enabled?
            gameplay::world3d::rendering::worldViewportBaseWidth(scene_):sw;
        const int ph=scene_.world_viewport.enabled?
            gameplay::world3d::rendering::worldViewportBaseHeight(scene_):sh;
        const float px=float(screen_x-viewport.x)*float(pw)/float(std::max(1,viewport.w));
        const float py=float(screen_y-viewport.y)*float(ph)/float(std::max(1,viewport.h));
        const auto pose=camera_.pose();
        constexpr float kPi=3.1415926535f;
        const float tangent=std::tan(std::max(.001f,pose.preset.fov_y_deg*kPi/360.0f));
        const float aspect=float(std::max(1,pw))/float(std::max(1,ph));
        const float ndc_x=px*2.0f/float(std::max(1,pw))-1.0f;
        const float ndc_y=1.0f-py*2.0f/float(std::max(1,ph));
        const gameplay::world3d::camera::Vec3 ray{
            pose.forward.x+pose.right.x*ndc_x*tangent*aspect+pose.up.x*ndc_y*tangent,
            pose.forward.y+pose.right.y*ndc_x*tangent*aspect+pose.up.y*ndc_y*tangent,
            pose.forward.z+pose.right.z*ndc_x*tangent*aspect+pose.up.z*ndc_y*tangent};
        if(std::abs(ray.y)<.00001f)return std::nullopt;
        const float distance=(scene_.interior.floor_datum-pose.position.y)/ray.y;
        if(distance<=0.0f)return std::nullopt;
        const float tile=std::max(1.0f,scene_.grid.tile_size);
        return pr::aquarium::geometry::GridCell{
            int(std::floor((pose.position.x+ray.x*distance)/tile)),
            int(std::floor((pose.position.z+ray.z*distance)/tile))};
    };
    const auto wall_hit=[&]() -> std::optional<std::pair<rooms::Wall,int>> {
        const auto b=*aquarium_room_draft_;
        const float tile=scene_.grid.tile_size;
        const float inset=scene_.interior.default_room.wall_face_offset_tiles*tile;
        const float ox=(b.column-scene_.interior.grid_origin_x)*tile;
        const float oz=(b.row-scene_.interior.grid_origin_y)*tile;
        const int sw=app_config_.window.virtual_width,sh=app_config_.window.virtual_height;
        const auto viewport=visibleWorldViewportRect(sw,sh);
        const int pw=scene_.world_viewport.enabled?gameplay::world3d::rendering::worldViewportBaseWidth(scene_):sw;
        const int ph=scene_.world_viewport.enabled?gameplay::world3d::rendering::worldViewportBaseHeight(scene_):sh;
        float best=1e30f;std::optional<std::pair<rooms::Wall,int>> result;
        const auto edge=[](float px,float py,float ax,float ay,float bx,float by) {
            return (px-ax)*(by-ay)-(py-ay)*(bx-ax);
        };
        const auto triangle_contains=[&](float px,float py,const std::array<float,2>& a,
            const std::array<float,2>& c,const std::array<float,2>& d) {
            const float e0=edge(px,py,a[0],a[1],c[0],c[1]);
            const float e1=edge(px,py,c[0],c[1],d[0],d[1]);
            const float e2=edge(px,py,d[0],d[1],a[0],a[1]);
            return (e0>=0&&e1>=0&&e2>=0)||(e0<=0&&e1<=0&&e2<=0);
        };
        for(auto wall:{rooms::Wall::North,rooms::Wall::East,rooms::Wall::South,rooms::Wall::West}) {
            if(aquarium_room_paint_wall_&&wall!=*aquarium_room_paint_wall_)continue;
            const int count=(wall==rooms::Wall::North||wall==rooms::Wall::South)?b.width:b.depth;
            for(int segment=0;segment<count;++segment) {
                const bool horizontal=wall==rooms::Wall::North||wall==rooms::Wall::South;
                const float fixed_x=wall==rooms::Wall::West?ox+inset:ox+b.width*tile-inset;
                const float fixed_z=wall==rooms::Wall::North?oz+inset:oz+b.depth*tile-inset;
                const float x0=horizontal?ox+segment*tile:fixed_x;
                const float x1=horizontal?ox+(segment+1)*tile:fixed_x;
                const float z0=horizontal?fixed_z:oz+segment*tile;
                const float z1=horizontal?fixed_z:oz+(segment+1)*tile;
                std::array<std::array<float,2>,4> screen{};bool projected=true;float d=0;
                const float low=scene_.interior.floor_datum;
                const float high=low+scene_.interior.default_room.wall_height_tiles*tile;
                const std::array<gameplay::world3d::camera::Vec3,4> corners{{
                    {x0,low,z0},{x1,low,z1},{x1,high,z1},{x0,high,z0}}};
                for(int i=0;i<4;++i) {
                    float sx=0,sy=0;projected&=camera_.worldToScreen(corners[i],pw,ph,sx,sy,d);
                    screen[i]={viewport.x+sx*viewport.w/std::max(1,pw),
                        viewport.y+sy*viewport.h/std::max(1,ph)};
                }
                if(!projected)continue;
                if(triangle_contains(x,y,screen[0],screen[1],screen[2])||
                    triangle_contains(x,y,screen[0],screen[2],screen[3]))return std::make_pair(wall,segment);
                const float cx=(screen[0][0]+screen[1][0]+screen[2][0]+screen[3][0])*.25f;
                const float cy=(screen[0][1]+screen[1][1]+screen[2][1]+screen[3][1])*.25f;
                const float distance=std::hypot(x-cx,y-cy);
                if(distance<best){best=distance;result=std::make_pair(wall,segment);}
            }
        }
        return best<=18.0f?result:std::nullopt;
    };
    const auto decoration_cells=[&](const rooms::RoomDecoration& item) {
        const auto* asset=room_decoration_catalog_.resolve(item.asset_id);
        if(!asset)return std::vector<rooms::Cell>{item.cell};
        return rooms::roomDecorationPlacementCells(
            *asset,item.cell,item.yaw_quarter_turns,scene_.grid.tile_size);
    };
    const auto decoration_error=[&](const rooms::RoomDecoration& item)->std::string {
        const auto* room=rooms::findRoom(*aquarium_room_candidate_,scene_.id);
        if(!room)return "The active room is unavailable";
        const auto footprint=decoration_cells(item);
        std::optional<int> footprint_depth;
        for(const auto point:footprint) {
            // The placement cell stays inside the room, but measured geometry
            // may overlap the wall face. This is intentional for vending
            // machines, signs and other wall-backed fixtures.
            if(point.column<0||point.row<0||point.column>=room->bounds.width||
                point.row>=room->bounds.depth)return "Object extends outside the room";
            for(const auto& door:room->doors)for(auto p:rooms::protectedDoorCells(*room,door))
                if(p.column-room->bounds.column==point.column&&p.row-room->bounds.row==point.row)
                    return "Object blocks a doorway";
            // Match the canonical half-cell transform used by the preview.
            for(const auto& tank:aquarium_construction_.committedDesign().tanks)
                for(const auto occupied:aqc::tankFootprintCells(tank))
                    for(int row=0;row<2;++row)for(int column=0;column<2;++column)
                        if(occupied.column+column==point.column&&occupied.row+row==point.row)
                            return "Object overlaps an aquarium tank";
            for(const auto& other:room->decorations)if(other.id!=item.id)
                for(const auto occupied:decoration_cells(other))
                    if(occupied.column==point.column&&occupied.row==point.row)
                        return "Object overlaps another decoration";
            const int depth=rooms::roomFloorDepth(*room,point);
            if(!footprint_depth)footprint_depth=depth;
            else if(depth!=*footprint_depth)return "Object spans different floor levels";
        }
        return {};
    };
    const auto decoration_valid=[&](const rooms::RoomDecoration& item) {
        return decoration_error(item).empty();
    };
    const auto publish_decoration_draft=[&]() {
        if(!room_decoration_draft_||!decoration_valid(*room_decoration_draft_)) {
            if(room_decoration_draft_)aquarium_room_error_=decoration_error(*room_decoration_draft_);
            requestAquariumConstructionErrorFeedback();return false;
        }
        auto* room=rooms::findRoom(*aquarium_room_candidate_,scene_.id);
        if(!room)return false;
        auto found=std::find_if(room->decorations.begin(),room->decorations.end(),[&](const auto& item){
            return item.id==room_decoration_draft_->id;});
        if(found==room->decorations.end()) {
            if(room->decorations.size()>=rooms::kRoomDecorationLimit) {
                requestAquariumConstructionErrorFeedback();return false;
            }
            room->decorations.push_back(*room_decoration_draft_);
        } else *found=*room_decoration_draft_;
        room_decoration_selected_=room_decoration_draft_->id;
        room_decoration_draft_.reset();room_decoration_dragging_=false;room_decoration_asset_.reset();
        aquarium_room_error_.clear();
        refreshAquariumRenderActors();aquarium_construction_move_sfx_requested_=true;return true;
    };
    const auto frame_wall=[&](rooms::Wall wall) {
        const auto b=*aquarium_room_draft_;const float tile=scene_.grid.tile_size;
        const float inset=scene_.interior.default_room.wall_face_offset_tiles*tile;
        const float ox=(b.column-scene_.interior.grid_origin_x)*tile;
        const float oz=(b.row-scene_.interior.grid_origin_y)*tile;
        const bool horizontal=wall==rooms::Wall::North||wall==rooms::Wall::South;
        const float span=(horizontal?b.width:b.depth)*tile;
        const float maximum_distance=std::max(115.0f,camera_.pose().preset.far_clip-80.0f);
        const float distance=std::clamp(span*1.5f,115.0f,maximum_distance);
        const float wall_height=scene_.interior.default_room.wall_height_tiles*tile;
        const float camera_y=scene_.interior.floor_datum+wall_height*.9f;
        const float look_y=scene_.interior.floor_datum+wall_height*.25f;
        const float pitch=std::atan2(look_y-camera_y,distance)*180.0f/3.14159265f;
        gameplay::world3d::camera::Vec3 position{};float yaw=0;
        if(wall==rooms::Wall::North){position={ox+b.width*tile*.5f+aquarium_room_wall_camera_pan_,camera_y,oz+inset+distance};yaw=180;}
        else if(wall==rooms::Wall::South){position={ox+b.width*tile*.5f-aquarium_room_wall_camera_pan_,camera_y,oz+b.depth*tile-inset-distance};yaw=0;}
        else if(wall==rooms::Wall::West){position={ox+inset+distance,camera_y,oz+b.depth*tile*.5f-aquarium_room_wall_camera_pan_};yaw=-90;}
        else {position={ox+b.width*tile-inset-distance,camera_y,oz+b.depth*tile*.5f+aquarium_room_wall_camera_pan_};yaw=90;}
        camera_.setManualPose(position,yaw,pitch);
    };
    const auto apply_paint=[&](const RoomPaintGesture& gesture) {
        const auto baseline=*aquarium_room_candidate_;
        auto* room=rooms::findRoom(*aquarium_room_candidate_,scene_.id);if(!room)return false;
        if(gesture.mode==aqc::ConstructionRoomEditMode::Floor) {
            for(int row=std::min(gesture.start.row,gesture.current.row);
                row<=std::max(gesture.start.row,gesture.current.row);++row)
                for(int column=std::min(gesture.start.column,gesture.current.column);
                    column<=std::max(gesture.start.column,gesture.current.column);++column) {
                    auto& values=room->surfaces.floor_overrides;
                    const auto existing=std::find_if(values.begin(),values.end(),[&](const auto& value){
                        return value.cell.column==column&&value.cell.row==row;});
                    if(existing==values.end())values.push_back({{column,row},aquarium_room_palette_index_});
                    else existing->palette=aquarium_room_palette_index_;
                }
        } else if(gesture.mode==aqc::ConstructionRoomEditMode::Levels) {
            std::map<std::pair<int,int>,int> depths;
            for(const auto& item:room->surfaces.floor_depth_overrides)
                depths[{item.cell.column,item.cell.row}]=item.depth;
            const auto original_depths=depths;
            const int first_row=std::min(gesture.start.row,gesture.current.row);
            const int last_row=std::max(gesture.start.row,gesture.current.row);
            const int first_column=std::min(gesture.start.column,gesture.current.column);
            const int last_column=std::max(gesture.start.column,gesture.current.column);
            const auto selected=[&](rooms::Cell cell) {
                return cell.column>=first_column&&cell.column<=last_column&&
                    cell.row>=first_row&&cell.row<=last_row;
            };
            const auto paint_cell=[&](rooms::Cell cell) {
                if(cell.column<0||cell.row<0||cell.column>=room->bounds.width||
                    cell.row>=room->bounds.depth)return;
                if(aquarium_room_palette_index_==0)depths.erase({cell.column,cell.row});
                else depths[{cell.column,cell.row}]=aquarium_room_palette_index_;
            };
            for(int row=first_row;row<=last_row;++row)
                for(int column=first_column;column<=last_column;++column)
                    paint_cell({column,row});
            // A tank is installed on the half-cell convention and therefore
            // overlaps four room cells per footprint cell. Require the player
            // to select that complete support area; partial selections reject
            // the entire edit instead of silently expanding it.
            const auto& document=aquarium_construction_.committedDesign();
            const auto frame=document.room_frame.value_or(
                pr::aquarium::geometry::GridCell{});
            for(const auto& tank:document.tanks) {
                std::vector<rooms::Cell> support;
                for(const auto source:aqc::tankFootprintCells(tank))
                    for(int row=0;row<2;++row)for(int column=0;column<2;++column) {
                        const rooms::Cell cell{
                            source.column+frame.column+column-room->bounds.column,
                            source.row+frame.row+row-room->bounds.row};
                        support.push_back(cell);
                    }
                const bool intersects=std::any_of(support.begin(),support.end(),selected);
                const bool contains_all=std::all_of(support.begin(),support.end(),selected);
                std::optional<int> existing_level;
                bool existing_uniform=true;
                for(const auto cell:support) {
                    const auto found=original_depths.find({cell.column,cell.row});
                    const int level=found==original_depths.end()?0:found->second;
                    if(!existing_level)existing_level=level;
                    else if(level!=*existing_level)existing_uniform=false;
                }
                const bool same_level=existing_uniform&&existing_level&&
                    *existing_level==aquarium_room_palette_index_;
                if(intersects&&!contains_all&&!same_level) {
                    aquarium_room_error_="Select the entire tank platform";
                    return false;
                }
            }
            // Height painting is intentionally permissive. Stairs are derived
            // conveniences, so touching either endpoint (or invalidating the
            // one-level relationship) removes them instead of rejecting the
            // whole floor edit.
            room->transitions.erase(std::remove_if(room->transitions.begin(),room->transitions.end(),
                [&](const rooms::RoomTransition& transition) {
                    return selected(transition.lower_cell)||selected(transition.upper_cell)||
                        ([&] {
                            const auto depth_at=[&](rooms::Cell cell) {
                                const auto found=depths.find({cell.column,cell.row});
                                return found==depths.end()?0:found->second;
                            };
                            return depth_at(transition.lower_cell)!=depth_at(transition.upper_cell)+1;
                        })();
                }),room->transitions.end());
            room->surfaces.floor_depth_overrides.clear();
            room->surfaces.floor_depth_overrides.reserve(depths.size());
            for(const auto& [cell,depth]:depths)
                room->surfaces.floor_depth_overrides.push_back({{cell.first,cell.second},depth});
        } else if(gesture.wall) {
            const int first=std::min(wallSegment(*gesture.wall,gesture.start),
                wallSegment(*gesture.wall,gesture.current));
            const int last=std::max(wallSegment(*gesture.wall,gesture.start),
                wallSegment(*gesture.wall,gesture.current));
            for(int segment=first;segment<=last;++segment) {
                auto& values=room->surfaces.wall_overrides;
                const auto existing=std::find_if(values.begin(),values.end(),[&](const auto& value){
                    return value.wall==*gesture.wall&&value.segment==segment;});
                if(existing==values.end())values.push_back({*gesture.wall,segment,aquarium_room_palette_index_});
                else existing->palette=aquarium_room_palette_index_;
            }
        } else return false;
        if(gesture.mode==aqc::ConstructionRoomEditMode::Levels) {
            try {
                const auto errors=rooms::validateBuildingLayout(
                    *aquarium_room_candidate_,aquariumRoomOccupancy());
                if(!errors.empty()) {
                    *aquarium_room_candidate_=baseline;
                    aquarium_room_error_=errors.front();
                    return false;
                }
                const auto* candidate_room=rooms::findRoom(*aquarium_room_candidate_,scene_.id);
                const auto& document=aquarium_construction_.committedDesign();
                const auto frame=document.room_frame.value_or(
                    pr::aquarium::geometry::GridCell{});
                if(candidate_room)for(const auto& tank:document.tanks) {
                    std::optional<int> tank_depth;
                    bool mixed=false;
                    for(const auto source:aqc::tankFootprintCells(tank))
                        for(int row=0;row<2;++row)for(int column=0;column<2;++column) {
                            const rooms::Cell local{
                                source.column+frame.column+column-candidate_room->bounds.column,
                                source.row+frame.row+row-candidate_room->bounds.row};
                            const int depth=rooms::roomFloorDepth(*candidate_room,local);
                            if(!tank_depth)tank_depth=depth;
                            else if(depth!=*tank_depth)mixed=true;
                        }
                    if(mixed) {
                        *aquarium_room_candidate_=baseline;
                        aquarium_room_error_="Tank footprint spans floor levels";
                        return false;
                    }
                }
            } catch(const std::exception& e) {
                *aquarium_room_candidate_=baseline;
                aquarium_room_error_=e.what();
                return false;
            }
        }
        aquarium_room_error_.clear();
        previewAquariumRoomSurfaceStyle();
        return true;
    };
    const auto finish_level_selection=[&]() {
        if(!aquarium_room_paint_gesture_)return false;
        aquarium_room_level_selection_baseline_=*aquarium_room_candidate_;
        aquarium_room_level_selection_ready_=true;
        const auto* room=rooms::findRoom(*aquarium_room_candidate_,scene_.id);
        aquarium_room_palette_index_=room?rooms::roomFloorDepth(
            *room,aquarium_room_paint_gesture_->start):0;
        aquarium_room_error_.clear();
        return true;
    };
    const auto apply_level_depth=[&](int depth) {
        if(!aquarium_room_level_selection_ready_||!aquarium_room_paint_gesture_||
            !aquarium_room_level_selection_baseline_)return false;
        const auto previous=*aquarium_room_candidate_;
        aquarium_room_candidate_=*aquarium_room_level_selection_baseline_;
        aquarium_room_palette_index_=std::clamp(depth,0,rooms::kMaximumRoomFloorDepth);
        if(apply_paint(*aquarium_room_paint_gesture_))return true;
        aquarium_room_candidate_=previous;
        previewAquariumRoomSurfaceStyle();
        return false;
    };
    const auto apply_stair_flight=[&](rooms::Cell start,rooms::Cell finish) {
        const int min_column=std::min(start.column,finish.column);
        const int max_column=std::max(start.column,finish.column);
        const int min_row=std::min(start.row,finish.row);
        const int max_row=std::max(start.row,finish.row);
        const auto* current_room=rooms::findRoom(*aquarium_room_candidate_,scene_.id);
        const bool removes_existing=current_room&&std::any_of(current_room->transitions.begin(),
            current_room->transitions.end(),[&](const rooms::RoomTransition& transition) {
                return transition.lower_cell.column>=min_column&&
                    transition.lower_cell.column<=max_column&&
                    transition.lower_cell.row>=min_row&&transition.lower_cell.row<=max_row;
            });
        const auto proposal=removes_existing?
            rooms::proposeRoomStairRemovalArea(*aquarium_room_candidate_,scene_.id,
                start,finish,aquariumRoomOccupancy()):
            rooms::proposeRoomStairArea(*aquarium_room_candidate_,scene_.id,
                start,finish,aquariumRoomOccupancy());
        if(!proposal.document) {
            aquarium_room_error_=proposal.diagnostics.empty()?"Stair run cannot be placed":
                proposal.diagnostics.front();return false;
        }
        const auto* room=rooms::findRoom(*proposal.document,scene_.id);if(!room)return false;
        const auto& document=aquarium_construction_.committedDesign();
        const auto frame=document.room_frame.value_or(pr::aquarium::geometry::GridCell{});
        for(const auto& tank:document.tanks) {
            std::optional<int> support_depth;
            for(const auto source:aqc::tankFootprintCells(tank))
                for(int row=0;row<2;++row)for(int column=0;column<2;++column) {
                    const rooms::Cell cell{source.column+frame.column+column-room->bounds.column,
                        source.row+frame.row+row-room->bounds.row};
                    const int depth=rooms::roomFloorDepth(*room,cell);
                    if(!support_depth)support_depth=depth;
                    else if(*support_depth!=depth) {
                        aquarium_room_error_="Stairs cannot split a tank platform";return false;
                    }
                }
        }
        aquarium_room_candidate_=*proposal.document;
        aquarium_room_error_.clear();previewAquariumRoomSurfaceStyle();
        aquarium_construction_move_sfx_requested_=true;return true;
    };
    const auto level_control=aquariumConstructionVisual().room_level_control;
    const auto update_level_knob=[&](int pointer_y) {
        if(!level_control)return false;
        const float span=std::max(1.0f,level_control->y1-level_control->y0);
        const float t=std::clamp((pointer_y-level_control->y0)/span,0.0f,1.0f);
        const int depth=int(std::lround(t*rooms::kMaximumRoomFloorDepth));
        if(depth==aquarium_room_palette_index_)return true;
        if(!apply_level_depth(depth))requestAquariumConstructionErrorFeedback();
        else aquarium_construction_move_sfx_requested_=true;
        return true;
    };
    if(aquarium_room_level_knob_dragging_) {
        if(release) {
            update_level_knob(y);aquarium_room_level_knob_dragging_=false;return true;
        }
        if(!press) {update_level_knob(y);return true;}
    }
    if(press&&level_control&&std::abs(x-level_control->x)<=28&&
        y>=level_control->y0-22&&y<=level_control->y1+22) {
        aquarium_room_level_knob_dragging_=true;update_level_knob(y);return true;
    }
    if(release) {
        if(aquarium_room_terrain_camera_panning_) {
            aquarium_room_terrain_camera_panning_=false;return true;
        }
        if(room_decoration_dragging_) {
            const int h=app_config_.window.virtual_height;
            const auto tray=gameplay::world3d::aquarium::decorations::roomAssetTrayLayout(
                app_config_.window.virtual_width,h);
            if(y>=tray.panel.y) {
                room_decoration_draft_.reset();room_decoration_dragging_=false;
                refreshAquariumRenderActors();return true;
            }
            if(const auto cell=aquariumConstructionCellAt(x,y);cell&&room_decoration_draft_)
                room_decoration_draft_->cell={cell->column,cell->row};
            publish_decoration_draft();return true;
        }
        if(aquarium_room_paint_gesture_) {
            if(aquarium_room_paint_gesture_->mode==aqc::ConstructionRoomEditMode::Levels) {
                if(!aquarium_room_level_selection_ready_&&
                    (aquarium_room_paint_gesture_->start.column!=aquarium_room_paint_gesture_->current.column||
                     aquarium_room_paint_gesture_->start.row!=aquarium_room_paint_gesture_->current.row))
                    finish_level_selection();
                return true;
            }
            if(aquarium_room_paint_gesture_->mode==aqc::ConstructionRoomEditMode::Transitions&&
                aquarium_room_paint_gesture_->start.column==aquarium_room_paint_gesture_->current.column&&
                aquarium_room_paint_gesture_->start.row==aquarium_room_paint_gesture_->current.row)
                return true;
            const auto gesture=*aquarium_room_paint_gesture_;
            aquarium_room_paint_gesture_.reset();
            const bool applied=gesture.mode==aqc::ConstructionRoomEditMode::Transitions?
                apply_stair_flight(gesture.start,gesture.current):apply_paint(gesture);
            if(!applied)requestAquariumConstructionErrorFeedback();
            return true;
        }
        if(aquarium_room_gesture_ && aquarium_room_gesture_->moved) finishAquariumRoomGesture();
        return true;
    }
    if(!press&&aquarium_room_terrain_camera_panning_) {
        const auto pose=camera_.pose();
        constexpr float kPi=3.1415926535f;
        const float world_per_pixel=2.0f*(pose.position.y-scene_.interior.floor_datum)*
            std::tan(pose.preset.fov_y_deg*kPi/360.0f)/
            float(std::max(1,app_config_.window.virtual_height));
        const float next_x=aquarium_room_terrain_pan_origin_x_+
            (x-aquarium_room_terrain_pan_start_.x)*world_per_pixel;
        const float next_z=aquarium_room_terrain_pan_origin_z_+
            (y-aquarium_room_terrain_pan_start_.y)*world_per_pixel;
        if(aquarium_room_terrain_top_down_) {
            aquarium_room_terrain_camera_x_=next_x;
            aquarium_room_terrain_camera_z_=next_z;
            frameAquariumRoomTerrainEditor();
        } else {
            aquarium_construction_camera_tracking_.center_x=next_x;
            aquarium_construction_camera_tracking_.center_z=next_z;
            adjustAquariumRoomSize(0,0);
        }
        return true;
    }
    if(!press&&room_decoration_dragging_&&room_decoration_draft_) {
        if(const auto cell=aquariumConstructionCellAt(x,y)) {
            room_decoration_draft_->cell={cell->column,cell->row};
            aquarium_room_error_=decoration_error(*room_decoration_draft_);
            refreshAquariumRenderActors();
        }
        return true;
    }
    if(!press&&aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Walls&&
        aquarium_room_paint_wall_&&!aquarium_room_paint_gesture_) {
        const int width=app_config_.window.virtual_width;
        const float tile=scene_.grid.tile_size;
        const bool horizontal=*aquarium_room_paint_wall_==rooms::Wall::North||
            *aquarium_room_paint_wall_==rooms::Wall::South;
        const float span=(horizontal?aquarium_room_draft_->width:aquarium_room_draft_->depth)*tile;
        const float limit=std::max(0.0f,span*.5f-145.0f);
        if(x<width*.10f) aquarium_room_wall_camera_pan_=std::max(-limit,aquarium_room_wall_camera_pan_-tile*.35f);
        else if(x>width*.90f) aquarium_room_wall_camera_pan_=std::min(limit,aquarium_room_wall_camera_pan_+tile*.35f);
        frame_wall(*aquarium_room_paint_wall_);
    }
    if(!press && aquarium_room_paint_gesture_&&!aquarium_room_level_selection_ready_) {
        if(aquarium_room_paint_gesture_->mode==aqc::ConstructionRoomEditMode::Walls) {
            if(const auto hit=wall_hit();hit&&hit->first==aquarium_room_paint_gesture_->wall) {
                const auto wall=hit->first; const int segment=hit->second;
                aquarium_room_paint_gesture_->current=(wall==rooms::Wall::North)?rooms::Cell{segment,0}:
                    (wall==rooms::Wall::South)?rooms::Cell{segment,aquarium_room_draft_->depth-1}:
                    (wall==rooms::Wall::West)?rooms::Cell{0,segment}:
                    rooms::Cell{aquarium_room_draft_->width-1,segment};
            }
        } else if(const auto cell=(aquarium_room_paint_gesture_->mode==aqc::ConstructionRoomEditMode::Levels||
            aquarium_room_paint_gesture_->mode==aqc::ConstructionRoomEditMode::Transitions)?
            level_cell_at(x,y):aquariumConstructionCellAt(x,y))
            aquarium_room_paint_gesture_->current={cell->column,cell->row};
        return true;
    }
    if(press) {
        if(aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Decorations) {
            const int w=app_config_.window.virtual_width,h=app_config_.window.virtual_height;
            const auto tray=gameplay::world3d::aquarium::decorations::roomAssetTrayLayout(w,h);
            const SDL_Point pointer{x,y};
            if(SDL_PointInRect(&pointer,&tray.panel)) {
                for(int category=0;category<4;++category) {
                    if(SDL_PointInRect(&pointer,&tray.categories[category])) {
                        room_decoration_category_index_=category;room_decoration_asset_index_=0;
                        room_decoration_asset_.reset();room_decoration_draft_.reset();
                        room_decoration_dragging_=false;refreshAquariumRenderActors();return true;
                    }
                }
                const auto category=rooms::kRoomDecorationCategories[room_decoration_category_index_];
                const auto entries=room_decoration_catalog_.indices(category);
                const int pages=std::max(1,(int(entries.size())+5)/6);
                if(SDL_PointInRect(&pointer,&tray.previous)||SDL_PointInRect(&pointer,&tray.next)) {
                    const int page=room_decoration_asset_index_/6;
                    room_decoration_asset_index_=((page+
                        (SDL_PointInRect(&pointer,&tray.previous)?-1:1)+pages)%pages)*6;
                    room_decoration_asset_.reset();return true;
                }
                for(int i=0;i<6;++i) {
                    if(!SDL_PointInRect(&pointer,&tray.assets[i]))continue;
                    const int index=(room_decoration_asset_index_/6)*6+i;
                    if(index<int(entries.size())) {
                        room_decoration_asset_index_=index;
                        room_decoration_asset_=room_decoration_catalog_.entries()[entries[index]].id;
                        room_decoration_selected_.reset();
                        rooms::RoomDecoration draft;
                        draft.id="room_prop_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
                        draft.asset_id=*room_decoration_asset_;
                        draft.cell={aquarium_room_draft_->width/2,aquarium_room_draft_->depth/2};
                        room_decoration_draft_=std::move(draft);room_decoration_dragging_=true;
                        refreshAquariumRenderActors();
                        aquarium_construction_move_sfx_requested_=true;
                    }
                    return true;
                }
                return true;
            }
        }
        const auto hit=aquariumConstructionHudHitAt(x,y);
        if(hit.action==aqc::ConstructionHudAction::RoomCameraTopDown) {
            aquarium_room_terrain_top_down_=!aquarium_room_terrain_top_down_;
            aquarium_room_terrain_camera_initialized_=false;
            if(aquarium_room_terrain_top_down_)frameAquariumRoomTerrainEditor();
            else adjustAquariumRoomSize(0,0);
            return true;
        }
        if(hit.action==aqc::ConstructionHudAction::RoomCameraPan) {
            aquarium_room_terrain_camera_panning_=true;
            aquarium_room_terrain_pan_start_={x,y};
            aquarium_room_terrain_pan_origin_x_=aquarium_room_terrain_top_down_?
                aquarium_room_terrain_camera_x_:aquarium_construction_camera_tracking_.center_x;
            aquarium_room_terrain_pan_origin_z_=aquarium_room_terrain_top_down_?
                aquarium_room_terrain_camera_z_:aquarium_construction_camera_tracking_.center_z;
            return true;
        }
        if(hit.action==aqc::ConstructionHudAction::RoomZoomOut||
            hit.action==aqc::ConstructionHudAction::RoomZoomIn) {
            aqc::adjustAquariumConstructionCameraZoom(aquarium_construction_camera_tracking_,
                hit.action==aqc::ConstructionHudAction::RoomZoomOut?-1:1);
            adjustAquariumRoomSize(0,0);return true;
        }
        if(hit.action==aqc::ConstructionHudAction::Build) {
            if(aquarium_room_paint_gesture_&&!aquarium_room_level_selection_ready_) {
                requestAquariumConstructionErrorFeedback();return true;
            }
            aquarium_room_paint_gesture_.reset();
            aquarium_room_level_selection_baseline_.reset();
            aquarium_room_level_selection_ready_=false;
            if(room_decoration_draft_&&!publish_decoration_draft())return true;
            commitAquariumRoomResize();return true;
        }
        if(hit.action==aqc::ConstructionHudAction::Cancel) {
            if(aquarium_room_paint_gesture_) {cancelAquariumRoomResize();return true;}
            cancelAquariumRoomResize();return true;
        }
        const auto select_mode=[&](aqc::ConstructionRoomEditMode mode) {
            if(aquarium_room_gesture_) finishAquariumRoomGesture();
            aquarium_room_paint_gesture_.reset();
            aquarium_room_level_selection_baseline_.reset();
            aquarium_room_level_selection_ready_=false;
            aquarium_room_level_knob_dragging_=false;
            aquarium_room_paint_wall_.reset();
            room_decoration_draft_.reset();room_decoration_dragging_=false;
            room_decoration_selected_.reset();room_decoration_asset_.reset();
            aquarium_room_edit_mode_=mode;
            if(const auto* room=rooms::findRoom(*aquarium_room_candidate_,scene_.id)) {
                if(mode==aqc::ConstructionRoomEditMode::Floor)
                    aquarium_room_palette_index_=room->surfaces.floor_palette;
                else if(mode==aqc::ConstructionRoomEditMode::Levels)
                    aquarium_room_palette_index_=0;
                else if(mode==aqc::ConstructionRoomEditMode::Walls)
                    aquarium_room_palette_index_=room->surfaces.wall_palette;
            }
            aquarium_room_handle_focus_=0;
            aquarium_room_terrain_top_down_=false;
            aquarium_room_terrain_camera_initialized_=false;
            adjustAquariumRoomSize(0,0);
            return true;
        };
        if(hit.action==aqc::ConstructionHudAction::RoomLayout)
            return select_mode(aqc::ConstructionRoomEditMode::Layout);
        if(hit.action==aqc::ConstructionHudAction::RoomFloor)
            return select_mode(aqc::ConstructionRoomEditMode::Floor);
        if(hit.action==aqc::ConstructionHudAction::RoomLevels)
            return select_mode(aqc::ConstructionRoomEditMode::Levels);
        if(hit.action==aqc::ConstructionHudAction::RoomTransitions)
        {
            aquarium_room_transition_kind_=rooms::RoomTransitionKind::Stairs;
            return select_mode(aqc::ConstructionRoomEditMode::Transitions);
        }
        if(hit.action==aqc::ConstructionHudAction::RoomWalls)
            return select_mode(aqc::ConstructionRoomEditMode::Walls);
        if(hit.action==aqc::ConstructionHudAction::RoomDecorations)
            return select_mode(aqc::ConstructionRoomEditMode::Decorations);
        if(hit.action==aqc::ConstructionHudAction::RoomStairs) {
            aquarium_room_transition_kind_=rooms::RoomTransitionKind::Stairs;
            aquarium_construction_focused_action_=hit.action;return true;
        }
        constexpr aqc::ConstructionHudAction palette_actions[]{
            aqc::ConstructionHudAction::RoomPalette0,aqc::ConstructionHudAction::RoomPalette1,
            aqc::ConstructionHudAction::RoomPalette2,aqc::ConstructionHudAction::RoomPalette3,
            aqc::ConstructionHudAction::RoomPalette4,aqc::ConstructionHudAction::RoomPalette5};
        for(int index=0;index<rooms::kRoomSurfacePaletteCount;++index)
            if(hit.action==palette_actions[index]) {
                if(aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Floor ||
                    aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Walls)
                    return setAquariumRoomSurfacePalette(index);
                return true;
            }
        if(aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Transitions) {
            const auto picked=level_cell_at(x,y);if(!picked)return true;
            const rooms::Cell point{picked->column,picked->row};
            if(!aquarium_room_paint_gesture_) {
                aquarium_room_paint_gesture_=RoomPaintGesture{
                    aqc::ConstructionRoomEditMode::Transitions,point,point,{}};
                aquarium_room_error_.clear();return true;
            }
            const auto start=aquarium_room_paint_gesture_->start;
            aquarium_room_paint_gesture_.reset();
            if(!apply_stair_flight(start,point))requestAquariumConstructionErrorFeedback();
            return true;
        }
        if(aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Decorations) {
            const auto visual=aquariumConstructionVisual();
            for(const auto& handle:visual.room_handles) if(handle.decoration_action&&
                std::hypot(handle.x-x,handle.y-y)<=28) {
                auto* room=rooms::findRoom(*aquarium_room_candidate_,scene_.id);
                if(!room||!room_decoration_selected_)return true;
                auto item=std::find_if(room->decorations.begin(),room->decorations.end(),[&](const auto& value){
                    return value.id==*room_decoration_selected_;});
                if(item==room->decorations.end())return true;
                if(handle.decoration_action==1) {
                    room_decoration_draft_=*item;room_decoration_dragging_=true;
                } else if(handle.decoration_action==2) {
                    if(!room_decoration_draft_||room_decoration_draft_->id!=item->id)
                        room_decoration_draft_=*item;
                    room_decoration_draft_->yaw_quarter_turns=
                        (room_decoration_draft_->yaw_quarter_turns+1)%4;
                    aquarium_room_error_=decoration_error(*room_decoration_draft_);
                    if(!aquarium_room_error_.empty())requestAquariumConstructionErrorFeedback();
                } else {
                    room->decorations.erase(item);room_decoration_selected_.reset();
                }
                refreshAquariumRenderActors();return true;
            }
            const auto cell=aquariumConstructionCellAt(x,y);if(!cell)return true;
            auto found=std::find_if(aquarium_room_candidate_->rooms.begin(),aquarium_room_candidate_->rooms.end(),
                [&](const auto& room){return room.id==scene_.id;});
            if(found==aquarium_room_candidate_->rooms.end())return true;
            const rooms::Cell point{cell->column,cell->row};
            if(room_decoration_asset_) {
                rooms::RoomDecoration item;
                item.id="room_prop_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
                item.asset_id=*room_decoration_asset_;item.cell=point;room_decoration_draft_=item;
                publish_decoration_draft();return true;
            }
            const auto existing=std::find_if(found->decorations.rbegin(),found->decorations.rend(),[&](const auto& item){
                const auto footprint=decoration_cells(item);
                return std::any_of(footprint.begin(),footprint.end(),[&](rooms::Cell occupied){
                    return occupied.column==point.column&&occupied.row==point.row;});
            });
            room_decoration_selected_=existing==found->decorations.rend()?std::optional<std::string>{}:
                std::optional<std::string>{existing->id};
            return true;
        }
        if(aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Floor ||
            aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Levels ||
            aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Walls) {
            rooms::Cell point{};
            std::optional<rooms::Wall> hit_wall;
            if(aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Walls) {
                const auto hit=wall_hit(); if(!hit) return true;
                hit_wall=hit->first;
                if(!aquarium_room_paint_wall_||*aquarium_room_paint_wall_!=*hit_wall) {
                    aquarium_room_paint_wall_=*hit_wall;aquarium_room_wall_camera_pan_=0.0f;
                    const rooms::Cell clicked=(*hit_wall==rooms::Wall::North)?rooms::Cell{hit->second,0}:
                        (*hit_wall==rooms::Wall::South)?rooms::Cell{hit->second,aquarium_room_draft_->depth-1}:
                        (*hit_wall==rooms::Wall::West)?rooms::Cell{0,hit->second}:
                        rooms::Cell{aquarium_room_draft_->width-1,hit->second};
                    apply_paint({aqc::ConstructionRoomEditMode::Walls,clicked,clicked,*hit_wall});
                    frame_wall(*hit_wall);return true;
                }
                point=(*hit_wall==rooms::Wall::North)?rooms::Cell{hit->second,0}:
                    (*hit_wall==rooms::Wall::South)?rooms::Cell{hit->second,aquarium_room_draft_->depth-1}:
                    (*hit_wall==rooms::Wall::West)?rooms::Cell{0,hit->second}:
                    rooms::Cell{aquarium_room_draft_->width-1,hit->second};
            } else {
                const auto cell=aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Levels?
                    level_cell_at(x,y):aquariumConstructionCellAt(x,y);
                if(!cell) return true;
                point={cell->column,cell->row};
            }
            if(aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Levels&&
                aquarium_room_level_selection_ready_) {
                aquarium_room_paint_gesture_.reset();
                aquarium_room_level_selection_baseline_.reset();
                aquarium_room_level_selection_ready_=false;
            }
            if(!aquarium_room_paint_gesture_) {
                RoomPaintGesture gesture;
                gesture.mode=aquarium_room_edit_mode_;gesture.start=gesture.current=point;
                if(gesture.mode==aqc::ConstructionRoomEditMode::Walls)
                    gesture.wall=hit_wall;
                if(gesture.mode==aqc::ConstructionRoomEditMode::Walls&&!gesture.wall) {
                    requestAquariumConstructionErrorFeedback();return true;
                }
                aquarium_room_paint_gesture_=gesture;return true;
            }
            auto gesture=*aquarium_room_paint_gesture_;gesture.current=point;
            if(gesture.mode==aqc::ConstructionRoomEditMode::Levels) {
                aquarium_room_paint_gesture_=gesture;
                finish_level_selection();return true;
            }
            aquarium_room_paint_gesture_.reset();
            if(!apply_paint(gesture))requestAquariumConstructionErrorFeedback();
            return true;
        }
        if(aquarium_room_gesture_) {finishAquariumRoomGesture();return true;}
        const auto visual=aquariumConstructionVisual();
        for(std::size_t i=0;i<visual.room_handles.size();++i) {
            const auto& handle=visual.room_handles[i];
            if(std::hypot(handle.x-x,handle.y-y)>28) continue;
            RoomGesture gesture;
            gesture.wall=handle.wall;gesture.add_door=handle.add_door;
            gesture.start_x=x;gesture.start_y=y;gesture.baseline=*aquarium_room_candidate_;
            try { gesture.occupancy=aquariumRoomOccupancy(); }
            catch(const std::exception& e) {aquarium_room_error_=e.what();return true;}
            const auto b=rooms::findRoom(gesture.baseline,scene_.id)->bounds;
            gesture.coordinate=gesture.add_door?
                ((gesture.wall==0||gesture.wall==2)?b.width:b.depth)/2:wallCoordinate(b,gesture.wall);
            const bool horizontal=gesture.add_door?(gesture.wall==0||gesture.wall==2):(gesture.wall==1||gesture.wall==3);
            const int w=app_config_.window.virtual_width,h=app_config_.window.virtual_height;
            const auto viewport=visibleWorldViewportRect(w,h);
            const int pw=scene_.world_viewport.enabled?gameplay::world3d::rendering::worldViewportBaseWidth(scene_):w;
            const int ph=scene_.world_viewport.enabled?gameplay::world3d::rendering::worldViewportBaseHeight(scene_):h;
            const float tile=scene_.grid.tile_size;
            const float wx=(b.column-scene_.interior.grid_origin_x+
                (gesture.wall==1?float(b.width):gesture.wall==3?0.0f:b.width*.5f))*tile;
            const float wz=(b.row-scene_.interior.grid_origin_y+
                (gesture.wall==0?0.0f:gesture.wall==2?float(b.depth):b.depth*.5f))*tile;
            float ax=0,ay=0,bx=0,by=0,depth=0;
            camera_.worldToScreen({wx,0,wz},pw,ph,ax,ay,depth);
            camera_.worldToScreen({wx+(horizontal?tile:0),0,wz+(horizontal?0:tile)},pw,ph,bx,by,depth);
            gesture.axis_x=(bx-ax)*viewport.w/std::max(1,pw);
            gesture.axis_y=(by-ay)*viewport.h/std::max(1,ph);
            aquarium_room_handle_focus_=int(i);aquarium_room_gesture_=std::move(gesture);
            if(handle.add_door) updateAquariumRoomGesture(0);
            return true;
        }
        return true;
    }
    if(aquarium_room_gesture_) {
        auto& g=*aquarium_room_gesture_;
        const int dx=x-g.start_x,dy=y-g.start_y;
        if(std::hypot(float(dx),float(dy))>=6) g.moved=true;
        const float divisor=std::max(1.0f,g.axis_x*g.axis_x+g.axis_y*g.axis_y);
        const int steps=std::clamp(int(std::lround((dx*g.axis_x+dy*g.axis_y)/divisor)),-128,128);
        if(steps!=g.steps) updateAquariumRoomGesture(steps);
    }
    return true;
}

void Overworld3DTestScreen::updateAquariumRoomGesture(int steps) {
    if(!aquarium_room_gesture_) return;
    auto& g=*aquarium_room_gesture_;g.steps=steps;
    try {
        const auto* room=rooms::findRoom(g.baseline,scene_.id);
        rooms::LayoutProposal result;
        if(g.add_door) {
            int id=1;
            std::string next;
            do {next="aquarium_room_"+std::to_string(id++);} while(rooms::findRoom(g.baseline,next));
            result=rooms::proposeConnectedRoom(g.baseline,scene_.id,rooms::Wall(g.wall),g.coordinate+steps,
                "to_"+next,next,"from_"+scene_.id,"link_"+next,g.occupancy);
        } else {
            const auto bounds=movedBounds(room->bounds,g.wall,g.coordinate+steps);
            if(bounds.width<8||bounds.depth<8||bounds.width>128||bounds.depth>128)
                throw std::runtime_error("Wall limit reached");
            aquarium_room_draft_=bounds;
            result=rooms::proposeWallMove(g.baseline,scene_.id,rooms::Wall(g.wall),g.coordinate+steps,g.occupancy);
            if(result.document) {
                auto projected=rooms::projectBuildingRoom(aquarium_room_style_,*result.document,
                    *rooms::findRoom(*result.document,scene_.id));
                auto tanks=aquarium_construction_.committedDesign();
                aqc::rebaseAquariumDesign(tanks,bounds.column,bounds.row);
                for(const auto& tank:tanks.tanks) for(auto p:aqc::tankFootprintCells(tank))
                    if(!rooms::roomDrawingCellFits(projected,p.column,p.row))
                        throw std::runtime_error("Wall would overlap a tank");
            }
        }
        if(!result.document) throw std::runtime_error(result.diagnostics.front());
        aquarium_room_candidate_=std::move(*result.document);
        aquarium_room_draft_=rooms::findRoom(*aquarium_room_candidate_,scene_.id)->bounds;
        aquarium_room_error_.clear();
    } catch(const std::exception& e) {aquarium_room_error_=e.what();}
}

bool Overworld3DTestScreen::finishAquariumRoomGesture() {
    if(!aquarium_room_gesture_) return true;
    const bool valid=aquarium_room_error_.empty();
    if(!valid) {
        aquarium_room_candidate_=aquarium_room_gesture_->baseline;
        aquarium_room_draft_=rooms::findRoom(*aquarium_room_candidate_,scene_.id)->bounds;
        requestAquariumConstructionErrorFeedback();
    }
    aquarium_room_gesture_.reset();aquarium_room_error_.clear();
    return valid;
}

void Overworld3DTestScreen::navigateAquariumRoom(int dx,int dy) {
    if(!dx && !dy) return;
    if(aquarium_room_gesture_) {
        const auto& g=*aquarium_room_gesture_;
        const bool horizontal=g.add_door?(g.wall==0||g.wall==2):(g.wall==1||g.wall==3);
        updateAquariumRoomGesture(g.steps+(horizontal?dx:dy));return;
    }
    if(dx) {
        constexpr int count=6;
        int mode=int(aquarium_room_edit_mode_);
        mode=(mode+(dx>0?1:-1)+count)%count;
        aquarium_room_edit_mode_=aqc::ConstructionRoomEditMode(mode);
        aquarium_room_paint_gesture_.reset();
        aquarium_room_level_selection_baseline_.reset();
        aquarium_room_level_selection_ready_=false;
        aquarium_room_level_knob_dragging_=false;
        aquarium_room_terrain_top_down_=false;
        if(const auto* room=rooms::findRoom(*aquarium_room_candidate_,scene_.id)) {
            if(aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Floor)
                aquarium_room_palette_index_=room->surfaces.floor_palette;
            else if(aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Levels)
                aquarium_room_palette_index_=0;
            else if(aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Walls)
                aquarium_room_palette_index_=room->surfaces.wall_palette;
        }
        aquarium_room_handle_focus_=0;
        adjustAquariumRoomSize(0,0);
        return;
    }
    if(dy && aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Levels) {
        const auto control=aquariumConstructionVisual().room_level_control;
        if(!control)return;
        const int next=std::clamp(aquarium_room_palette_index_+(dy>0?1:-1),0,
            rooms::kMaximumRoomFloorDepth);
        const float y=control->y0+(control->y1-control->y0)*float(next)/
            float(rooms::kMaximumRoomFloorDepth);
        handleAquariumRoomPointer(int(control->x),int(y),true);
        handleAquariumRoomPointer(int(control->x),int(y),false,true);
        return;
    }
    if(dy && (aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Floor ||
        aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Walls)) {
        const int next=(aquarium_room_palette_index_+(dy>0?1:-1)+
            rooms::kRoomSurfacePaletteCount)%rooms::kRoomSurfacePaletteCount;
        if(!setAquariumRoomSurfacePalette(next)) requestAquariumConstructionErrorFeedback();
        else aquarium_construction_move_sfx_requested_=true;
        return;
    }
    const auto visual=aquariumConstructionVisual();
    const int count=int(visual.room_handles.size());
    if(count) aquarium_room_handle_focus_=(aquarium_room_handle_focus_+(dx+dy>0?1:-1)+count)%count;
}

void Overworld3DTestScreen::advanceAquariumRoom() {
    if(aquarium_room_gesture_) {finishAquariumRoomGesture();return;}
    const auto visual=aquariumConstructionVisual();
    if(visual.room_handles.empty()) return;
    const auto handle=visual.room_handles[std::clamp(aquarium_room_handle_focus_,0,int(visual.room_handles.size())-1)];
    handleAquariumRoomPointer(int(handle.x),int(handle.y),true);
}
}
