#include "gameplay/world3d/npc/NpcActorDriver.hpp"
#include <algorithm>
#include <deque>
#include <iostream>

namespace pr::gameplay::world3d::npc {
namespace {
int roomCount(const AquariumVisitorSession& session,const std::string& room) {
    return int(std::count_if(session.visitors.begin(),session.visitors.end(),
        [&](const auto& pair){return pair.second.room==room;}));
}
}
void NpcActorDriver::configureAquariumVisitors(std::shared_ptr<AquariumVisitorSession> session,VisitorRoomPlan plan) {
    visitor_session_=std::move(session);visitor_plan_=std::move(plan);
    auto& s=*visitor_session_;
    if(s.dialogue.empty())s.dialogue=loadVisitorDialogueCatalog(project_root_);
    if(s.appearances.empty()) {
        for(const auto& d:loadTestingActorDefinitions())
            if(d.kind==NpcActorKind::Human && std::find(s.appearances.begin(),s.appearances.end(),d.character_package_path)==s.appearances.end())
                s.appearances.push_back(d.character_package_path);
        std::shuffle(s.appearances.begin(),s.appearances.end(),rng_);
    }
    const int w=scene_->grid.width,h=scene_->grid.height;
    visitor_walkable_.assign(std::size_t(w)*h,0);visitor_cells_.clear();
    // Flood from the player's component, excluding actual water and tank solids.
    std::deque<VisitorCell> frontier;
    if(!reserved_tiles_.empty())frontier.push_back({reserved_tiles_[0].first,reserved_tiles_[0].second});
    while(!frontier.empty()) {
        auto p=frontier.front();frontier.pop_front();
        if(p.x<0||p.y<0||p.x>=w||p.y>=h||visitor_walkable_[p.y*w+p.x]||
            !validWalkTile(p.x,p.y)||terrain_query_->tileIsActualWater(p.x,p.y))continue;
        visitor_walkable_[p.y*w+p.x]=1;visitor_cells_.push_back(p);
        for(auto d:{VisitorCell{0,-1},{1,0},{0,1},{-1,0}})
            if(canStepBetweenTiles(p.x,p.y,p.x+d.x,p.y+d.y))frontier.push_back({p.x+d.x,p.y+d.y});
    }
    auto reachable=[&](VisitorCell p){return p.x>=0&&p.y>=0&&p.x<w&&p.y<h&&visitor_walkable_[p.y*w+p.x];};
    auto& spots=visitor_plan_.spots;
    auto& portals=visitor_plan_.portals;
    portals.erase(std::remove_if(portals.begin(),portals.end(),[&](const auto& d){return !reachable(d.cell);}),portals.end());
    spots.erase(std::remove_if(spots.begin(),spots.end(),[&](const auto& p){
        return !reachable(p.cell)||std::any_of(visitor_plan_.portals.begin(),visitor_plan_.portals.end(),[&](const auto& door){
            return std::abs(door.cell.x-p.cell.x)+std::abs(door.cell.y-p.cell.y)<=3;
        });
    }),spots.end());
    const int capacity=aquariumVisitorCapacity(int(visitor_cells_.size()),s.config);
    s.capacities[scene_->id]=capacity;
    visitor_arrival_seconds_=s.config.arrival_seconds;
    for(auto& pair:s.visitors) {
        auto& v=pair.second;if(v.room!=scene_->id)continue;
        const bool valid_watch=std::any_of(spots.begin(),spots.end(),[&](const auto& p){return p.tank_id==v.tank&&p.cell==v.goal;});
        if(!v.tank.empty()&&!valid_watch){v.watching=false;v.has_goal=false;v.tank.clear();}
        if(const auto actor=findActor(v.id)) {
            actors_[*actor].path.clear();actors_[*actor].visitor_watching=v.watching;
        }
    }
    // Revalidate after construction, never leave visitors inside new tank solids.
    std::vector<std::string> reset;
    for(const auto& a:actors_)if(a.aquarium_visitor && (!reachable({a.tile_x,a.tile_y})||
        (a.moving&&!reachable({a.target_tile_x,a.target_tile_y}))))reset.push_back(a.definition.id);
    for(const auto& id:reset) {
        removeAquariumVisitorActor(id);auto& v=s.visitors.at(id);
        v.cell={-1,-1};v.has_goal=false;v.watching=false;
    }
    // Room shrink/tank expansion can reduce capacity while construction pauses
    // visitors. Retire surplus session residents before resuming gameplay.
    std::vector<std::string> surplus;
    int retained=0;
    for(const auto& pair:s.visitors)if(pair.second.room==scene_->id&&++retained>capacity)surplus.push_back(pair.first);
    for(const auto& id:surplus){removeAquariumVisitorActor(id);s.visitors.erase(id);}
    for(auto& pair:s.visitors)if(pair.second.room==scene_->id)attachAquariumVisitor(pair.second);
    if(s.initialized_rooms.insert(scene_->id).second && !s.appearances.empty()) {
        const int initial=s.config.initial_fill<=0?0:std::min(capacity,std::max(1,int(capacity*s.config.initial_fill)));
        for(int i=roomCount(s,scene_->id);i<initial;++i) {
            AquariumVisitorRecord v;v.id="aquarium-visitor:"+std::to_string(s.next_id++);
            v.room=scene_->id;v.appearance=nextAquariumVisitorAppearance(s);v.cell={-1,-1};
            if(i%2==0&&!spots.empty()) {
                const auto& spot=spots[rng_()%spots.size()];v.cell=spot.cell;v.goal=spot.cell;
                v.facing=spot.facing;v.tank=spot.tank_id;v.watching=true;
                v.watch_left=std::uniform_real_distribution<double>(s.config.watch_min_seconds,s.config.watch_max_seconds)(rng_);
                v.visited_tanks.insert(scene_->id+":"+v.tank);v.last_watch=v.cell;
            }
            auto& stored=s.visitors.emplace(v.id,std::move(v)).first->second;
            attachAquariumVisitor(stored);
        }
    }
    std::cerr<<"[AquariumVisitors] event=room_ready room="<<scene_->id<<" reachable_cells="<<visitor_cells_.size()
        <<" capacity="<<capacity<<" residents="<<roomCount(s,scene_->id)<<" viewing_spots="<<spots.size()<<'\n';
}

bool NpcActorDriver::attachAquariumVisitor(AquariumVisitorRecord& v) {
    if(findActor(v.id))return true;
    const int count=int(std::count_if(actors_.begin(),actors_.end(),[](const auto& a){return a.aquarium_visitor;}));
    if(count>=visitor_session_->capacities[scene_->id])return false;
    auto available=[&](VisitorCell p){return p.x>=0&&p.y>=0&&p.x<scene_->grid.width&&p.y<scene_->grid.height&&
        visitor_walkable_[p.y*scene_->grid.width+p.x]&&!tileOccupied(p.x,p.y)&&!tileReserved(p.x,p.y);};
    if(!v.arrival_door.empty()) {
        const auto door=std::find_if(visitor_plan_.portals.begin(),visitor_plan_.portals.end(),[&](const auto& d){return d.id==v.arrival_door;});
        if(door==visitor_plan_.portals.end()||!available(door->cell))return false;
        v.cell=door->cell;v.arrival_door.clear();v.watching=false;v.has_goal=false;
    } else if(!available(v.cell)) {
        auto cells=visitor_cells_;std::shuffle(cells.begin(),cells.end(),rng_);
        const auto found=std::find_if(cells.begin(),cells.end(),[&](auto p){return available(p)&&
            std::none_of(visitor_plan_.portals.begin(),visitor_plan_.portals.end(),[&](const auto& d){
                return std::abs(p.x-d.cell.x)+std::abs(p.y-d.cell.y)<3;});});
        if(found==cells.end())return false;
        v.cell=*found;v.watching=false;v.has_goal=false;
    }
    NpcActorDefinition definition;definition.id=v.id;definition.character_package_path=v.appearance;
    definition.facing=v.facing;definition.spawn_partner_pokemon=false;
    const auto index=addActorAtTile(definition,v.cell.x,v.cell.y);
    if(!index)return false;
    auto& actor=actors_[*index];actor.aquarium_visitor=true;actor.visitor_watching=v.watching;
    actor.running=false;
    actor.move_speed_units_per_second=movement_config_.walkSpeed()*float(visitor_session_->config.adult_walk_speed_multiplier);
    actor.base_move_speed_units_per_second=actor.move_speed_units_per_second;
    actor.dialogue_lines={"I love the aquarium"};actor.npc_interaction_mode="direct_dialogue";
    actor.runtime_interaction_script_id.reset();actor.wait_seconds=0;
    return true;
}

void NpcActorDriver::removeAquariumVisitorActor(const std::string& id) {
    const auto index=findActor(id);if(!index)return;
    if(interaction_locked_actor_) {
        if(*interaction_locked_actor_==*index)interaction_locked_actor_.reset();
        else if(*interaction_locked_actor_>*index)--*interaction_locked_actor_;
    }
    actors_.erase(actors_.begin()+*index);
}
std::vector<std::pair<int,int>> NpcActorDriver::occupiedActorTiles() const {
    std::vector<std::pair<int,int>> result;
    for(const auto& a:actors_) {
        result.emplace_back(a.tile_x,a.tile_y);
        if(a.moving)result.emplace_back(a.target_tile_x,a.target_tile_y);
    }
    return result;
}
bool NpcActorDriver::visitorConversationAvailable(const Actor& actor) const {
    if(!actor.aquarium_visitor)return true;
    const auto found=visitor_session_->visitors.find(actor.definition.id);
    return found!=visitor_session_->visitors.end()&&visitorCanConverse(found->second.conversation_count);
}
bool NpcActorDriver::beginVisitorConversation(Actor& actor) {
    if(!actor.aquarium_visitor)return true;
    if(!visitorConversationAvailable(actor))return false;
    auto& session=*visitor_session_;auto& v=session.visitors.at(actor.definition.id);
    if(!beginVisitorExchange(v.conversation_count))return false;
    VisitorExhibitContext context;
    if(v.watching)if(const auto exhibit=visitor_plan_.exhibits.find(v.tank);exhibit!=visitor_plan_.exhibits.end())context=exhibit->second;
    actor.dialogue_lines={chooseVisitorDialogue(session.dialogue,context,session.recent_dialogue,rng_)};
    return true;
}

void NpcActorDriver::chooseVisitorDestination(Actor& a,AquariumVisitorRecord& v) {
    auto& s=*visitor_session_;
    v.has_goal=false;v.tank.clear();v.portal.clear();a.path.clear();
    const double choice=std::uniform_real_distribution<double>(0,1)(rng_);
    std::vector<VisitorPortal> doors;
    for(const auto& d:visitor_plan_.portals) {
        const bool exit=d.destination_room.empty();
        if((!v.visited_tanks.empty()||visitor_plan_.spots.empty()) && ((exit&&choice<s.config.exit_chance)||
            (!exit&&choice>=s.config.exit_chance&&choice<s.config.exit_chance+s.config.room_change_chance)))doors.push_back(d);
    }
    std::shuffle(doors.begin(),doors.end(),rng_);
    for(const auto& d:doors) {
        const int limit=s.capacities.count(d.destination_room)?s.capacities.at(d.destination_room):s.config.max_per_room;
        if(!d.destination_room.empty()&&roomCount(s,d.destination_room)>=limit)continue;
        // All candidates already belong to the flood-filled walking component.
        v.goal=d.cell;v.portal=d.id;v.has_goal=true;break;
    }
    if(!v.has_goal) {
        auto spots=visitor_plan_.spots;std::shuffle(spots.begin(),spots.end(),rng_);
        std::stable_sort(spots.begin(),spots.end(),[&](const auto& a,const auto& b){
            return v.visited_tanks.count(scene_->id+":"+a.tank_id)<
                v.visited_tanks.count(scene_->id+":"+b.tank_id);});
        for(const auto& p:spots) {
            if(p.cell==v.last_watch || tileOccupied(p.cell.x,p.cell.y,&a)||tileReserved(p.cell.x,p.cell.y))continue;
            const bool claimed=std::any_of(s.visitors.begin(),s.visitors.end(),[&](const auto& pair){
                const auto& other=pair.second;return other.id!=v.id&&other.room==v.room&&
                    (other.has_goal||other.watching)&&other.goal==p.cell;});
            if(claimed)continue;
            v.goal=p.cell;v.facing=p.facing;v.tank=p.tank_id;v.has_goal=true;break;
        }
    }
    if(!v.has_goal&&!visitor_cells_.empty()) {
        v.goal=visitor_cells_[rng_()%visitor_cells_.size()];v.has_goal=true;
    }
    v.retry=.3;v.blocked_seconds=0;
}

void NpcActorDriver::updateAquariumVisitors(double dt) {
    if(!visitor_session_)return;
    auto& s=*visitor_session_;dt=std::clamp(dt,0.0,.1);
    int route_budget=1; // Stagger searches; large rooms must not replan every resident in one frame.
    std::vector<std::string> departed;
    for(auto& a:actors_) {
        if(!a.aquarium_visitor)continue;
        auto& v=s.visitors.at(a.definition.id);v.cell={a.tile_x,a.tile_y};
        const auto index=std::size_t(&a-actors_.data());
        if(interaction_locked_actor_==index)continue;
        if(a.moving){v.blocked_seconds=0;continue;}
        if(v.watching) {
            a.facing=v.facing;v.watch_left-=dt;
            if(v.watch_left>0)continue;
            v.watching=false;a.visitor_watching=false;v.has_goal=false;
        }
        if(v.has_goal&&v.cell==v.goal) {
            if(!v.portal.empty()) {
                const auto door=std::find_if(visitor_plan_.portals.begin(),visitor_plan_.portals.end(),[&](const auto& p){return p.id==v.portal;});
                if(door!=visitor_plan_.portals.end()) {
                    const int limit=s.capacities.count(door->destination_room)?s.capacities.at(door->destination_room):s.config.max_per_room;
                    if(door->destination_room.empty()||roomCount(s,door->destination_room)<limit) {
                        v.room=door->destination_room;v.arrival_door=door->destination_door;
                        departed.push_back(v.id);v.has_goal=false;
                        std::cerr<<"[AquariumVisitors] event="<<(v.room.empty()?"exit":"room_travel")<<" visitor="<<v.id<<" destination="<<v.room<<'\n';
                        continue;
                    }
                }
            } else if(!v.tank.empty()) {
                v.watching=true;a.visitor_watching=true;a.facing=v.facing;v.last_watch=v.cell;
                v.visited_tanks.insert(scene_->id+":"+v.tank);v.has_goal=false;
                v.watch_left=std::uniform_real_distribution<double>(s.config.watch_min_seconds,s.config.watch_max_seconds)(rng_);
                continue;
            } else {v.retry=2;}
            v.has_goal=false;
        }
        v.retry-=dt;if(v.retry>0)continue;
        if(!v.has_goal)chooseVisitorDestination(a,v);
        if(!v.has_goal)continue;
        if(a.path.empty()) {
            if(route_budget==0)continue;
            --route_budget;
            auto free=visitor_walkable_;
            for(const auto& other:actors_)if(&other!=&a) {
                auto block=[&](int x,int y){if(x>=0&&y>=0&&x<scene_->grid.width&&y<scene_->grid.height)free[y*scene_->grid.width+x]=0;};
                block(other.tile_x,other.tile_y);if(other.moving)block(other.target_tile_x,other.target_tile_y);
            }
            for(auto [x,y]:reserved_tiles_)if(x>=0&&y>=0&&x<scene_->grid.width&&y<scene_->grid.height)
                free[y*scene_->grid.width+x]=0;
            free[a.tile_y*scene_->grid.width+a.tile_x]=1;
            for(auto p:aquariumVisitorRoute(scene_->grid.width,scene_->grid.height,free,v.cell,v.goal))a.path.push_back({p.x,p.y});
        }
        if(!a.path.empty()) {
            const auto [x,y]=a.path.front();startStepToTile(a,x,y);
            if(a.moving){a.path.pop_front();continue;}
            a.path.clear();
        }
        v.retry=std::uniform_real_distribution<double>(.4,.6)(rng_);v.blocked_seconds+=.5;
        if(v.blocked_seconds>=3){v.has_goal=false;v.blocked_seconds=0;}
    }
    for(const auto& id:departed) {
        removeAquariumVisitorActor(id);
        if(s.visitors.at(id).room.empty())s.visitors.erase(id);
    }
    // Retry queued door arrivals, but never pile actors into an occupied entrance.
    visitor_arrival_seconds_-=dt;
    if(visitor_arrival_seconds_<=0) {
        visitor_arrival_seconds_=s.config.arrival_seconds;
        for(auto& pair:s.visitors)if(pair.second.room==scene_->id)attachAquariumVisitor(pair.second);
        if(roomCount(s,scene_->id)<s.capacities[scene_->id]&&!s.appearances.empty()) {
            const auto door=std::find_if(visitor_plan_.portals.begin(),visitor_plan_.portals.end(),[](const auto& d){return d.destination_room.empty();});
            if(door!=visitor_plan_.portals.end()) {
                AquariumVisitorRecord v;v.id="aquarium-visitor:"+std::to_string(s.next_id++);v.room=scene_->id;
                v.appearance=nextAquariumVisitorAppearance(s);v.arrival_door=door->id;
                auto& stored=s.visitors.emplace(v.id,std::move(v)).first->second;attachAquariumVisitor(stored);
            }
        }
    }
}
} // namespace pr::gameplay::world3d::npc
