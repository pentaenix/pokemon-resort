#include "ui/Overworld3DTestScreen.hpp"
#include "gameplay/world3d/aquarium/AquariumPokemonMetrics.hpp"
#include <algorithm>
#include <cmath>

namespace pr {
namespace aq=gameplay::world3d::aquarium;
namespace {
struct Subject {gameplay::world3d::camera::Vec3 center;float radius;};
Subject subject(const aq::AquariumPokemonActor& actor) {
    const auto bounds=aq::measureAquariumPokemon(actor.model_path,actor.form);
    const float scale=actor.model_scale;
    return {{actor.world_position[0],actor.world_position[1]+(bounds.min_y+bounds.max_y)*.5f*scale,
        actor.world_position[2]},std::max(3.0f,std::max({bounds.max_x-bounds.min_x,bounds.max_y-bounds.min_y,
            bounds.max_z-bounds.min_z})*scale*.5f)};
}
}
void Overworld3DTestScreen::updateAquariumInspectionSubject() {
    if(!aquarium_inspection_camera_||!aquarium_simulation_||aquarium_inspection_camera_->focusedPokemonId().empty())return;
    for(const auto& actor:aquarium_simulation_->actors())if(actor.id==aquarium_inspection_camera_->focusedPokemonId()){
        aquarium_inspection_camera_->trackPokemon(subject(actor).center);return;
    }
    aquarium_inspection_camera_->leavePokemon();
}
bool Overworld3DTestScreen::pickAquariumInspectionPokemon(int x,int y) {
    if(!aquarium_inspection_camera_||!aquarium_inspection_camera_->focused()||!aquarium_simulation_)return false;
    const auto viewport=visibleWorldViewportRect(app_config_.window.virtual_width,app_config_.window.virtual_height);
    const auto prefix=aquarium_inspection_camera_->activePlacementId()+":";
    const aq::AquariumPokemonActor* best=nullptr;Subject chosen{};float nearest=1e30f;
    for(const auto& actor:aquarium_simulation_->actors()){
        if(actor.id.rfind(prefix,0)!=0)continue;
        const auto target=subject(actor);float sx,sy,depth;
        if(!camera_.worldToScreen(target.center,viewport.w,viewport.h,sx,sy,depth))continue;
        const float radius=std::max(12.0f,target.radius*viewport.h/
            (2*std::max(1.0f,depth)*std::tan(camera_.pose().preset.fov_y_deg*3.14159265f/360)));
        if(std::hypot(x-viewport.x-sx,y-viewport.y-sy)<=radius && depth<nearest){
            best=&actor;chosen=target;nearest=depth;
        }
    }
    return best && aquarium_inspection_camera_->focusPokemon(best->id,chosen.center,chosen.radius,camera_);
}
}
