#include "ui/Overworld3DTestScreen.hpp"
#include "gameplay/world3d/rendering/PixelScale.hpp"
#include <algorithm>
#include <cmath>

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
}

void Overworld3DTestScreen::appendAquariumRoomPreview(aqc::AquariumConstructionVisual& visual) const {
    visual.state=aqc::ConstructionState::ResizeRoom;
    visual.draft_valid=aquarium_room_error_.empty();
    visual.status_hint=aquarium_room_error_;
    visual.focused_action=aquarium_construction_focused_action_;
    const float tile=scene_.grid.tile_size;
    const float inset=scene_.interior.default_room.wall_face_offset_tiles*tile;
    const auto b=*aquarium_room_draft_;
    const float x=(b.column-scene_.interior.grid_origin_x)*tile;
    const float z=(b.row-scene_.interior.grid_origin_y)*tile;
    visual.room_outline=std::array<float,4>{x+inset,z+inset,x+b.width*tile-inset,z+b.depth*tile-inset};
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
    if(release) {
        if(aquarium_room_gesture_ && aquarium_room_gesture_->moved) finishAquariumRoomGesture();
        return true;
    }
    if(press) {
        const auto hit=aquariumConstructionHudHitAt(x,y);
        if(hit.action==aqc::ConstructionHudAction::Build) {commitAquariumRoomResize();return true;}
        if(hit.action==aqc::ConstructionHudAction::Cancel) {cancelAquariumRoomResize();return true;}
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
