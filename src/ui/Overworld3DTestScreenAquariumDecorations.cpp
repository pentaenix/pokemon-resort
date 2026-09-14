#include "ui/Overworld3DTestScreen.hpp"
#include "gameplay/world3d/aquarium/decorations/AquariumDecorationInteraction.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace pr {
namespace decor=gameplay::world3d::aquarium::decorations;
namespace aqc=gameplay::world3d::aquarium::construction;
bool Overworld3DTestScreen::beginAquariumDecorations() {
    if(!bgfx_renderer_)return false;
    if(aquarium_commit_future_.valid() || aquarium_construction_.state()!=aqc::ConstructionState::Selected)return false;
    const auto* selected=aquarium_construction_.selectedTank();if(!selected)return false;
    const auto tank=std::find_if(player_aquarium_runtime_.tanks.begin(),player_aquarium_runtime_.tanks.end(),
        [&](const auto& t){return t.design.id==selected->id;});
    const auto nav=std::find_if(player_aquarium_runtime_.simulation_tanks.begin(),player_aquarium_runtime_.simulation_tanks.end(),
        [&](const auto& t){return t.tank_id==selected->id;});
    if(tank==player_aquarium_runtime_.tanks.end()||nav==player_aquarium_runtime_.simulation_tanks.end())return false;
    if(!decoration_catalog_loaded_){decoration_catalog_.scan(project_root_);decoration_catalog_loaded_=true;}
    if(decoration_catalog_.entries().empty())return false;
    std::vector<decor::Decoration> objects;
    for(const auto& t:aquarium_construction_.committedDesign().tank_decorations)if(t.tank_id==selected->id)objects=t.objects;
    if(!decoration_editor_.open(*tank,nav->navigation,std::move(objects),decoration_catalog_))return false;
    closeAquariumStocking();resetAquariumConstructionPointerOperation();
    decoration_saved_camera_=camera_;decoration_tool_=decor::Tool::None;
    decoration_pointer_tool_active_=false;
    decoration_zoom_=1;decoration_commit_pending_=false;
    decoration_zoom_axis_=0;
    updateAquariumDecorationCamera(0);
    std::cerr<<"[AquariumDecorations] event=open tank="<<selected->id<<" assets="<<decoration_catalog_.entries().size()<<'\n';
    return true;
}
void Overworld3DTestScreen::closeAquariumDecorations() {
    if(!decoration_editor_.active())return;
    decoration_asset_press_.reset();decoration_asset_dragged_=false;
    decoration_editor_.close();decoration_tool_=decor::Tool::None;decoration_commit_pending_=false;
    if(decoration_saved_camera_)camera_=*decoration_saved_camera_;
    decoration_saved_camera_.reset();resetAquariumConstructionPointerOperation();
    std::cerr<<"[AquariumDecorations] event=close\n";
}
void Overworld3DTestScreen::finishAquariumDecorations() {
    if(decoration_commit_pending_)return;
    if(decoration_editor_.draft()&&!decoration_editor_.confirm()){requestAquariumConstructionErrorFeedback();return;}
    if(!decoration_editor_.dirty()){closeAquariumDecorations();return;}
    auto candidate=aquarium_construction_.prepareDecorationChange(decoration_editor_.tankId(),decoration_editor_.objects());
    if(candidate && beginAquariumConstructionCommit(std::move(candidate)))decoration_commit_pending_=true;
    else requestAquariumConstructionErrorFeedback();
}
void Overworld3DTestScreen::updateAquariumDecorationCamera(double dt) {
    if(decoration_commit_pending_&&!aquarium_commit_future_.valid()&&aquarium_construction_.state()!=aqc::ConstructionState::Building){
        decoration_commit_pending_=false;
        for(const auto& t:aquarium_construction_.committedDesign().tank_decorations)
            if(t.tank_id==decoration_editor_.tankId()&&t.objects==decoration_editor_.objects()){closeAquariumDecorations();return;}
    }
    const auto& tank=decoration_editor_.tank();
    decoration_zoom_=std::clamp(decoration_zoom_*float(std::exp(decoration_zoom_axis_*dt)),.35f,2.0f);
    const float bottom=decor::substrateWorldY(tank),top=tank.world_floor_y+tank.design.height_steps*8;
    camera_=decor::framedDecorationCamera(*decoration_saved_camera_,
        {tank.world_center_x,(bottom+top)*.5f,tank.world_center_z},
        {pr::aquarium::geometry::occupiedWidthCells(tank.design.footprint)*8.0f,(top-bottom)*.5f,
         pr::aquarium::geometry::occupiedDepthCells(tank.design.footprint)*8.0f},
        app_config_.window.virtual_width,app_config_.window.virtual_height,decoration_zoom_);
}
SDL_FPoint Overworld3DTestScreen::aquariumDecorationHandlePosition(bool floor) const {
    const auto& editor=decoration_editor_;const decor::Decoration* object=editor.draft()?&*editor.draft():nullptr;
    if(!object&&editor.selected())for(const auto& o:editor.objects())if(o.id==*editor.selected())object=&o;
    const int w=app_config_.window.virtual_width,h=app_config_.window.virtual_height;
    if(!object)return {w*.5f,h*.45f};
    const auto viewport=visibleWorldViewportRect(w,h);float x=0,y=0,depth=0;
    const auto& t=editor.tank();
    camera_.worldToScreen({t.world_center_x+object->x_steps*2,decor::substrateWorldY(t)+(floor?0:object->height_steps*2),
        t.world_center_z+object->z_steps*2},viewport.w,viewport.h,x,y,depth);
    return {x+viewport.x,y+viewport.y};
}
void Overworld3DTestScreen::chooseAquariumDecoration(int direction) {
    decoration_asset_press_.reset();decoration_asset_dragged_=false;
    const auto entries=decoration_catalog_.indices(decoration_category_);
    const int size=int(entries.size());if(size==0)return;
    decoration_asset_index_=(decoration_asset_index_+direction+size)%size;
    decoration_editor_.chooseAsset(decoration_catalog_.entries()[entries[decoration_asset_index_]].id);
    decoration_tool_=decor::Tool::Move;
    decoration_pointer_tool_active_=false;
}
void Overworld3DTestScreen::navigateAquariumDecorations(int dx,int dy) {
    if(decoration_commit_pending_)return;
    if(decoration_tool_==decor::Tool::Height)decoration_editor_.adjustHeight(-dy+dx);
    else if(decoration_tool_==decor::Tool::Size)decoration_editor_.adjustSize(-dy+dx);
    else if(decoration_tool_==decor::Tool::Rotate)decoration_editor_.rotate(dx-dy);
    else decoration_editor_.nudge(dx,dy);
}
bool Overworld3DTestScreen::handleAquariumDecorationPointer(int x,int y,bool pressed) {
    if(decoration_commit_pending_)return true;
    if(pressed){decoration_asset_press_.reset();decoration_asset_dragged_=false;}
    else if(decoration_asset_press_ && decor::decorationDragStarted(*decoration_asset_press_,{x,y}))
        decoration_asset_dragged_=true;
    const int w=app_config_.window.virtual_width,h=app_config_.window.virtual_height;
    const auto handle=aquariumDecorationHandlePosition();
    if(pressed)for(const auto& button:decor::buttons(w,h,decoration_asset_index_/6,
        decoration_editor_.selected().has_value()||decoration_editor_.draft().has_value(),handle.x,handle.y)){
        SDL_Point point{x,y};if(!SDL_PointInRect(&point,&button.rect))continue;
        const int action=button.action;
        if(action==1)finishAquariumDecorations();
        else if(action==2){if(!decoration_editor_.cancel())closeAquariumDecorations();decoration_tool_=decor::Tool::None;decoration_pointer_tool_active_=false;}
        else if(action==3)decoration_editor_.undo();
        else if(action==4)decoration_editor_.redo();
        else if(action==5||action==6){
            const int pages=(int(decoration_catalog_.indices(decoration_category_).size())+5)/6;
            if(pages)decoration_asset_index_=((decoration_asset_index_/6+(action==5?-1:1)+pages)%pages)*6;
        } else if(action>=20&&action<=23){
            decoration_editor_.cancel();decoration_pointer_tool_active_=false;decoration_tool_=decor::Tool::None;
            decoration_category_=static_cast<decor::Category>(action-20);decoration_asset_index_=0;
        } else if(action>=100){
            const int index=action-100;if(index<int(decoration_catalog_.indices(decoration_category_).size())){
                decoration_asset_index_=index;chooseAquariumDecoration(0);
                decoration_pointer_tool_active_=decoration_editor_.draft().has_value();
                if(decoration_pointer_tool_active_)decoration_asset_press_=SDL_Point{x,y};
            }
        } else if(action==14)decoration_editor_.erase();
        else if(action==15){decoration_editor_.resetHeight();decoration_tool_=decor::Tool::Height;decoration_pointer_tool_active_=false;}
        else {
            const auto tool=action==10?decor::Tool::Move:action==11?decor::Tool::Height:action==12?decor::Tool::Size:decor::Tool::Rotate;
            decoration_tool_=tool;decoration_pointer_tool_active_=true;
            decoration_pointer_step_=decoration_tool_==decor::Tool::Rotate ? x/12 : -y/12;
            if(action==10)decoration_editor_.beginMove();
        }
        return true;
    }
    if(y>=h-180||y<80)return true;
    if(decoration_tool_!=decor::Tool::None&&decoration_tool_!=decor::Tool::Move){
        if(!decoration_pointer_tool_active_)return true;
        const int step=decoration_tool_==decor::Tool::Rotate?x/12:-y/12;
        const int delta=step-decoration_pointer_step_;decoration_pointer_step_=step;
        if(decoration_tool_==decor::Tool::Height)decoration_editor_.adjustHeight(delta);
        else if(decoration_tool_==decor::Tool::Size)decoration_editor_.adjustSize(delta);
        else decoration_editor_.rotate(delta);
        return true;
    }
    const auto viewport=visibleWorldViewportRect(w,h);const auto pose=camera_.pose();
    const float tangent=std::tan(pose.preset.fov_y_deg*3.14159265f/360);
    const float nx=(2.0f*(x-viewport.x)/std::max(1,viewport.w)-1)*tangent*viewport.w/std::max(1,viewport.h);
    const float ny=(1-2.0f*(y-viewport.y)/std::max(1,viewport.h))*tangent;
    const auto& t=decoration_editor_.tank();
    const float dx=pose.forward.x+pose.right.x*nx+pose.up.x*ny;
    const float dy=pose.forward.y+pose.right.y*nx+pose.up.y*ny;
    const float dz=pose.forward.z+pose.right.z*nx+pose.up.z*ny;
    if(std::abs(dy)<.0001f)return true;
    const float elevation=decoration_editor_.draft()?decoration_editor_.draft()->height_steps*2.0f:0;
    const float distance=(decor::substrateWorldY(t)+elevation-pose.position.y)/dy;
    const float local_x=pose.position.x+dx*distance-t.world_center_x;
    const float local_z=pose.position.z+dz*distance-t.world_center_z;
    if(decoration_editor_.moving()){
        if(decoration_pointer_tool_active_ && (!decoration_asset_press_||decoration_asset_dragged_))
            decoration_editor_.moveTo(local_x,local_z);
    } else if(pressed) {
        const decor::Decoration* hit=nullptr;float nearest=1e30f;
        for(const auto& object:decoration_editor_.objects())if(const auto* a=decoration_catalog_.resolve(object.asset_id)){
            const float scale=a->base_scale*object.scale_steps*.25f;
            float px=0,py=0,depth=0;
            if(!camera_.worldToScreen({t.world_center_x+object.x_steps*2,
                decor::substrateWorldY(t)+object.height_steps*2+(a->bounds.max_y-a->bounds.min_y)*scale*.5f,
                t.world_center_z+object.z_steps*2},viewport.w,viewport.h,px,py,depth))continue;
            const float radius=std::max(14.0f,std::max(a->bounds.max_x-a->bounds.min_x,a->bounds.max_z-a->bounds.min_z)*scale*
                viewport.h/(4*std::max(.001f,depth)*tangent));
            const float score=std::hypot(px+viewport.x-x,py+viewport.y-y)/radius;
            if(score<1 && score<nearest){nearest=score;hit=&object;}
        }
        if(hit)decoration_editor_.selectAt(hit->x_steps*2,hit->z_steps*2);
        else decoration_editor_.selectAt(local_x,local_z);
    }
    return true;
}
bool Overworld3DTestScreen::handleAquariumDecorationEvent(const SDL_Event& event) {
    if(decoration_commit_pending_)return true;
    if(decor::usesDecorationPointerCallback(event))return false;
    if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){
        decoration_editor_.cancel();decoration_pointer_tool_active_=false;decoration_tool_=decor::Tool::None;
        decoration_zoom_axis_=0;return true;
    }
    if(event.type==SDL_MOUSEWHEEL){
        int x=0,y=0;SDL_GetMouseState(&x,&y);const auto logical=mapPointerToLogical(x,y);
        if(logical.y>app_config_.window.virtual_height-180){
            const int pages=(int(decoration_catalog_.indices(decoration_category_).size())+5)/6;
            if(pages)decoration_asset_index_=((decoration_asset_index_/6+(event.wheel.y>0?-1:1)+pages)%pages)*6;
        }
        else decoration_zoom_=std::clamp(decoration_zoom_*(event.wheel.y>0?.9f:1.1f),.35f,2.0f);
    } else if(event.type==SDL_MOUSEBUTTONDOWN&&event.button.button==SDL_BUTTON_RIGHT){
        if(!decoration_editor_.cancel())closeAquariumDecorations();decoration_tool_=decor::Tool::None;decoration_pointer_tool_active_=false;
    } else if(event.type==SDL_KEYDOWN){
        decoration_pointer_tool_active_=false;
        const auto key=event.key.keysym.sym;const auto mod=event.key.keysym.mod;
        if(key==SDLK_z&&(mod&(KMOD_CTRL|KMOD_GUI))){if(mod&KMOD_SHIFT)decoration_editor_.redo();else decoration_editor_.undo();}
        else if(key==SDLK_q||key==SDLK_e){decoration_tool_=decor::Tool::Height;decoration_editor_.adjustHeight(key==SDLK_e?1:-1);}
        else if(key==SDLK_LEFTBRACKET||key==SDLK_RIGHTBRACKET)decoration_editor_.adjustSize(key==SDLK_RIGHTBRACKET?1:-1);
        else if(key==SDLK_r||key==SDLK_t)decoration_editor_.rotate(key==SDLK_r?1:-1);
        else if(key==SDLK_DELETE)decoration_editor_.erase();
        else if(key==SDLK_TAB)decoration_editor_.selectNext();
        else if(key==SDLK_PAGEUP||key==SDLK_PAGEDOWN){
            decoration_editor_.cancel();decoration_tool_=decor::Tool::None;
            decoration_category_=static_cast<decor::Category>((int(decoration_category_)+(key==SDLK_PAGEUP?3:1))%4);
            decoration_asset_index_=0;
        }
        else return false; // Navigation, accept and cancel use the normal mappings.
    } else if(event.type==SDL_CONTROLLERBUTTONDOWN){
        decoration_pointer_tool_active_=false;
        switch(event.cbutton.button){
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:chooseAquariumDecoration(-1);break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:chooseAquariumDecoration(1);break;
        case SDL_CONTROLLER_BUTTON_BACK:decoration_editor_.undo();break;
        case SDL_CONTROLLER_BUTTON_START:decoration_editor_.redo();break;
        case SDL_CONTROLLER_BUTTON_X:if(decoration_editor_.selected())decoration_editor_.erase();else decoration_editor_.selectNext();break;
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK:decoration_editor_.selectNext();break;
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:
            decoration_tool_=static_cast<decor::Tool>(int(decoration_tool_)%4+1);
            if(decoration_tool_==decor::Tool::Move)decoration_editor_.beginMove();
            else if(decoration_tool_==decor::Tool::Height)decoration_editor_.adjustHeight(0);
            else if(decoration_tool_==decor::Tool::Size)decoration_editor_.adjustSize(0);
            else decoration_editor_.rotate(0);
            break;
        default:return false; // A/B/Y and directional buttons belong to InputRouter.
        }
    } else if(event.type==SDL_CONTROLLERAXISMOTION){
        if(event.caxis.axis!=SDL_CONTROLLER_AXIS_RIGHTY && event.caxis.axis!=SDL_CONTROLLER_AXIS_TRIGGERLEFT &&
            event.caxis.axis!=SDL_CONTROLLER_AXIS_TRIGGERRIGHT)return false;
        decoration_pointer_tool_active_=false;
        if(event.caxis.axis==SDL_CONTROLLER_AXIS_RIGHTY)
            decoration_zoom_axis_=std::abs(event.caxis.value)>6000 ? event.caxis.value/32768.0f : 0;
        if(event.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERLEFT||event.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERRIGHT){
            bool& held=event.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERLEFT?decoration_left_trigger_:decoration_right_trigger_;
            const bool down=event.caxis.value>16000;
            if(down&&!held){decoration_tool_=decor::Tool::Height;decoration_editor_.adjustHeight(event.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERLEFT?-1:1);}
            held=down;
        }
    }
    return true;
}
} // namespace pr
