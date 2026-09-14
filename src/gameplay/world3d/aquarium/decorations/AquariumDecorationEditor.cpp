#include "gameplay/world3d/aquarium/decorations/AquariumDecorationEditor.hpp"
#include "gameplay/world3d/aquarium/AquariumBodyNavigation.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>

namespace pr::gameplay::world3d::aquarium::decorations {
float substrateWorldY(const construction::PlayerTankRuntime& tank) {
    return tank.world_floor_y-tank.design.depth_steps*8.0f+pr::aquarium::geometry::kFlatSandSurfaceWorldUnits;
}
AquariumPokemonActor decorationActor(const Decoration& o,const Asset& a,
    const construction::PlayerTankRuntime& tank,bool editor_view) {
    AquariumPokemonActor actor;
    actor.id="decoration:"+tank.design.id+":"+o.id;
    actor.model_path=a.path.string(); actor.model_scale=a.base_scale*o.scale_steps*.25f;
    actor.world_yaw_degrees=o.yaw_steps*15.0f;
    const float angle=actor.world_yaw_degrees*3.14159265f/180;
    const float x=(a.bounds.min_x+a.bounds.max_x)*.5f*actor.model_scale;
    const float z=(a.bounds.min_z+a.bounds.max_z)*.5f*actor.model_scale;
    actor.world_position={tank.world_center_x+o.x_steps*2.0f-x*std::cos(angle)-z*std::sin(angle),
        substrateWorldY(tank)+o.height_steps*2.0f-a.bounds.min_y*actor.model_scale,
        tank.world_center_z+o.z_steps*2.0f+x*std::sin(angle)-z*std::cos(angle)};
    (void)editor_view;
    return actor;
}
bool Editor::open(const construction::PlayerTankRuntime& tank,const AquariumNavigation& nav,
    std::vector<Decoration> objects,Catalog& catalog) {
    close();
    if(!nav.valid || objects.size()>kTankDecorationLimit) return false;
    tank_.design=tank.design;tank_.world_center_x=tank.world_center_x;
    tank_.world_center_z=tank.world_center_z;tank_.world_floor_y=tank.world_floor_y;
    navigation_=nav;objects_=initial_=std::move(objects);catalog_=&catalog;active_=true;
    return true;
}
void Editor::close() {
    active_=false;moving_=false;draft_.reset();selected_.reset();
    undo_.clear();redo_.clear();objects_.clear();initial_.clear();message_.clear();
}
bool Editor::chooseAsset(const std::string& id) {
    if(!active_ || !catalog_->resolve(id)) {message_="This model could not be loaded";return false;}
    if(objects_.size()>=kTankDecorationLimit) {message_="This tank has all its decoration slots filled";return false;}
    Decoration object;
    object.scale_steps=6; // New placements start two quarter-scale ticks larger.
    object.asset_id=id;
    object.id="decor_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"_"+std::to_string(++sequence_);
    draft_=object;selected_.reset();moving_=true;message_.clear();return true;
}
bool Editor::selectAt(float x,float z) {
    if(draft_) return false;
    std::vector<std::string> hits;
    for(const auto& o:objects_) {
        const auto* a=catalog_->resolve(o.asset_id);if(!a) continue;
        const float scale=a->base_scale*o.scale_steps*.25f;
        const float radius=std::max(a->bounds.max_x-a->bounds.min_x,a->bounds.max_z-a->bounds.min_z)*scale*.5f;
        if(std::hypot(x-o.x_steps*2,z-o.z_steps*2)<=std::max(4.0f,radius)) hits.push_back(o.id);
    }
    if(hits.empty()) {selected_.reset();return false;}
    const auto at=selected_ ? std::find(hits.begin(),hits.end(),*selected_) : hits.end();
    selected_=at==hits.end() || at+1==hits.end() ? hits.front() : *(at+1);
    return true;
}
void Editor::selectNext() {
    if(draft_ || objects_.empty()) return;
    auto at=selected_ ? std::find_if(objects_.begin(),objects_.end(),[&](const auto& o){return o.id==*selected_;}) : objects_.end();
    selected_=at==objects_.end() || at+1==objects_.end() ? objects_.front().id : (at+1)->id;
}
bool Editor::beginEdit() {
    if(draft_) return true;
    if(!selected_) return false;
    const auto at=std::find_if(objects_.begin(),objects_.end(),[&](const auto& o){return o.id==*selected_;});
    if(at==objects_.end()) return false;
    draft_=*at;return true;
}
bool Editor::beginMove() {if(!beginEdit()) return false;moving_=true;return true;}
void Editor::moveTo(float x,float z) {
    if(!draft_ || !moving_) return;
    draft_->x_steps=std::clamp(int(std::lround(x*.5f)),-32768,32768);
    draft_->z_steps=std::clamp(int(std::lround(z*.5f)),-32768,32768);
}
void Editor::nudge(int dx,int dz) {if(beginMove()){draft_->x_steps+=dx;draft_->z_steps+=dz;}}
void Editor::adjustHeight(int steps) {if(beginEdit()) draft_->height_steps=std::clamp(draft_->height_steps+steps,-1024,1024);}
void Editor::resetHeight() {if(beginEdit()) draft_->height_steps=0;}
void Editor::adjustSize(int steps) {if(beginEdit()) draft_->scale_steps=std::clamp(draft_->scale_steps+steps,1,32);}
void Editor::rotate(int steps) {if(beginEdit()) draft_->yaw_steps=(draft_->yaw_steps+steps%24+24)%24;}
bool Editor::valid(const Decoration& o) const {
    const auto* asset=catalog_->resolve(o.asset_id); if(!asset) return false;
    const float scale=asset->base_scale*o.scale_steps*.25f/16;
    const auto& b=asset->bounds;
    const float radius=std::hypot(b.max_x-b.min_x,b.max_z-b.min_z)*scale*.5f;
    const float floor=(substrateWorldY(tank_)-tank_.world_floor_y)/16;
    const float base=floor+o.height_steps*.125f;
    const float top=std::max(floor+.02f,base+(b.max_y-b.min_y)*scale);
    // Buried parts are decorative and do not need water; all exposed parts do.
    const float clipped_base=std::max(floor+.01f,base);
    return containsAquariumBody(navigation_,{radius,0,std::max(.01f,top-clipped_base)},
        {o.x_steps*.125f,clipped_base,o.z_steps*.125f});
}
bool Editor::valid() const {return !draft_ || valid(*draft_);}
bool Editor::arrangementValid() const {
    return std::all_of(objects_.begin(),objects_.end(),[&](const auto& object){return valid(object);});
}
void Editor::record(std::vector<Decoration> next) {
    undo_.push_back(objects_);if(undo_.size()>100) undo_.erase(undo_.begin());
    redo_.clear();objects_=std::move(next);
}
bool Editor::confirm() {
    if(!draft_) return false;
    if(!valid()) {message_="Keep the decoration inside the glass and clear of tunnels";return false;}
    auto next=objects_;
    auto at=std::find_if(next.begin(),next.end(),[&](const auto& o){return o.id==draft_->id;});
    if(at==next.end()) {if(next.size()>=kTankDecorationLimit)return false;next.push_back(*draft_);} else *at=*draft_;
    selected_=draft_->id;record(std::move(next));draft_.reset();moving_=false;message_.clear();return true;
}
bool Editor::cancel() {
    if(!draft_) return false;
    draft_.reset();moving_=false;message_.clear();return true;
}
bool Editor::erase() {
    if(draft_ || !selected_) return false;
    auto next=objects_;
    next.erase(std::remove_if(next.begin(),next.end(),[&](const auto& o){return o.id==*selected_;}),next.end());
    record(std::move(next));selected_.reset();return true;
}
bool Editor::undo() {
    if(draft_ || undo_.empty())return false;
    redo_.push_back(objects_);objects_=std::move(undo_.back());undo_.pop_back();selected_.reset();return true;
}
bool Editor::redo() {
    if(draft_ || redo_.empty())return false;
    undo_.push_back(objects_);objects_=std::move(redo_.back());redo_.pop_back();selected_.reset();return true;
}
std::vector<AquariumPokemonActor> Editor::actors(bool editor_view) {
    std::vector<AquariumPokemonActor> result;
    for(const auto& o:objects_) {
        if(draft_ && draft_->id==o.id) continue;
        if(const auto* a=catalog_->resolve(o.asset_id))result.push_back(decorationActor(o,*a,tank_,editor_view));
    }
    if(draft_) if(const auto* a=catalog_->resolve(draft_->asset_id))result.push_back(decorationActor(*draft_,*a,tank_,editor_view));
    return result;
}
std::vector<std::string> validateRuntimeDecorations(const construction::AquariumDesignDocument& document,
    const construction::PlayerAquariumRuntimeSet& runtime,const std::filesystem::path& root) {
    std::vector<std::string> errors;
    if(document.tank_decorations.empty())return errors;
    Catalog catalog;catalog.scan(root);
    for(const auto& arrangement:document.tank_decorations){
        const auto tank=std::find_if(runtime.tanks.begin(),runtime.tanks.end(),
            [&](const auto& t){return t.design.id==arrangement.tank_id;});
        if(tank==runtime.tanks.end())continue; // undo-recoverable arrangement of a deleted tank
        const auto nav=std::find_if(runtime.simulation_tanks.begin(),runtime.simulation_tanks.end(),
            [&](const auto& t){return t.tank_id==arrangement.tank_id;});
        Editor validator;
        if(nav==runtime.simulation_tanks.end() || !validator.open(*tank,nav->navigation,arrangement.objects,catalog) ||
            !validator.arrangementValid())
            errors.push_back("Move or remove decorations that cross the glass or tunnel: "+arrangement.tank_id);
    }
    return errors;
}
} // namespace pr::gameplay::world3d::aquarium::decorations
