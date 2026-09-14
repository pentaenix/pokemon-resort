#include "ui/Overworld3DTestScreen.hpp"
#include <algorithm>
#include <set>

namespace pr {
void Overworld3DTestScreen::configureAquariumVisitors() {
    namespace npc=gameplay::world3d::npc;
    namespace rooms=gameplay::world3d::aquarium::rooms;
    using Facing=gameplay::world3d::FacingDirection;
    if(!aquarium_building_||!aquarium_collision_overlay_)return;
    const auto* room=rooms::findRoom(*aquarium_building_,scene_.id);
    if(!room)return;
    const auto revision=scene_.id+":"+std::to_string(aquarium_building_->revision)+":"+
        std::to_string(player_aquarium_runtime_.revision);
    if(npc_actor_driver_->aquariumVisitorRevision()==revision)return;
    if(!aquarium_visitors_) {
        aquarium_visitors_=std::make_shared<npc::AquariumVisitorSession>();
        aquarium_visitors_->config=npc::loadAquariumVisitorConfig(project_root_);
    }
    npc::VisitorRoomPlan plan;plan.revision=revision;
    for(const auto& door:room->doors) {
        const auto cell=rooms::doorLandingCell(*room,door);
        npc::VisitorPortal p;p.id=door.id;
        p.cell={cell.column-room->bounds.column,cell.row-room->bounds.row};
        if(const auto destination=rooms::resolveDoorLanding(*aquarium_building_,{room->id,door.id})) {
            p.destination_room=destination->endpoint.room_id;p.destination_door=destination->endpoint.door_id;
        } else {
            bool external=false;
            for(const auto& link:aquarium_building_->external_connections)
                if(link.interior.room_id==room->id&&link.interior.door_id==door.id)external=true;
            if(!external)continue;
        }
        plan.portals.push_back(std::move(p));
    }
    const auto free=[&](int x,int y){return x>=0&&y>=0&&x<scene_.grid.width&&y<scene_.grid.height&&
        !aquarium_collision_overlay_->tileBlocked(x,y)&&!aquarium_collision_overlay_->tileIsActualWater(x,y);};
    for(const auto& tank:player_aquarium_runtime_.tanks) {
        auto& context=plan.exhibits[tank.design.id];context.tags.insert("tank");
        for(const auto& population:player_aquarium_runtime_.simulation_tanks)if(population.tank_id==tank.design.id)
            for(const auto& resident:population.swimmers)
                if(const auto* species=aquarium_species_catalog_.findApproved(resident.movement.id))
                    if(!species->display_name.empty()&&std::find(context.pokemon.begin(),context.pokemon.end(),species->display_name)==context.pokemon.end())
                        context.pokemon.push_back(species->display_name);
        if(context.pokemon.empty())context.tags.insert("empty");
        if(!tank.design.tunnels.empty())context.tags.insert("tunnel");
        namespace decor=gameplay::world3d::aquarium::decorations;
        for(const auto& arrangement:aquarium_construction_.committedDesign().tank_decorations)
            if(arrangement.tank_id==tank.design.id)for(const auto& object:arrangement.objects) {
                context.tags.insert("decorations");
                switch(decor::decorationCategory(object.asset_id)) {
                case decor::Category::Rocks:context.tags.insert("rocks");break;
                case decor::Category::Corals:context.tags.insert("corals");break;
                case decor::Category::Plants:context.tags.insert("plants");break;
                case decor::Category::Other:break;
                }
            }
        std::set<std::pair<int,int>> solid;
        for(const auto p:tank.build.collision.blocked_cells)for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)
            solid.emplace(p.column+dx,p.row+dy);
        for(const auto p:tank.build.collision.dry_corridor_cells)solid.erase({p.column,p.row});
        std::set<std::pair<int,int>> added;
        for(const auto [x,y]:solid)for(auto d:{npc::VisitorCell{0,-1},{1,0},{0,1},{-1,0}}) {
            const int px=x+d.x,py=y+d.y;
            if(!free(px,py)||added.count({px,py}))continue;
            int exits=0;
            for(auto step:{npc::VisitorCell{0,-1},{1,0},{0,1},{-1,0}})exits+=free(px+step.x,py+step.y);
            // Never settle in a one-cell passage or tunnel that would block circulation.
            if(exits<3)continue;
            added.emplace(px,py);
            const auto face=d.x>0?Facing::West:d.x<0?Facing::East:d.y>0?Facing::North:Facing::South;
            plan.spots.push_back({{px,py},face,tank.design.id});
        }
    }
    npc_actor_driver_->configureAquariumVisitors(aquarium_visitors_,std::move(plan));
    if(follower_controller_)follower_controller_->setAquariumInterest(
        npc_actor_driver_->aquariumVisitorPlan(),aquarium_visitors_->config);
}
} // namespace pr
