#include "gameplay/world3d/npc/AquariumVisitors.hpp"
#include "core/config/Json.hpp"
#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <iostream>

namespace pr::gameplay::world3d::npc {
AquariumVisitorConfig loadAquariumVisitorConfig(const std::string& root) {
    AquariumVisitorConfig result;
    try {
        const auto json=parseJsonFile((std::filesystem::path(root)/"config/gameplay/world3d/aquarium_visitors.json").string());
        const auto number=[&](const char* key,double fallback,double lo,double hi) {
            const auto* v=json.get(key);
            return v && v->isNumber() && std::isfinite(v->asNumber())
                ? std::clamp(v->asNumber(),lo,hi) : fallback;
        };
        if(const auto* v=json.get("enabled");v&&v->isBool())result.enabled=v->asBool();
        result.cells_per_visitor=int(number("walkableCellsPerVisitor",32,8,4096));
        result.max_per_room=int(number("maxVisitorsPerRoom",24,0,64));
        result.initial_fill=number("initialFill",.65,0,1);
        result.watch_min_seconds=number("watchMinSeconds",15,1,600);
        result.watch_max_seconds=number("watchMaxSeconds",120,result.watch_min_seconds,1200);
        result.arrival_seconds=number("arrivalIntervalSeconds",35,5,600);
        result.exit_chance=number("exitChanceAfterWatching",.18,0,1);
        result.room_change_chance=number("roomChangeChanceAfterWatching",.25,0,1);
        result.adult_walk_speed_multiplier=number("adultWalkSpeedMultiplier",.55,.15,1.5);
    } catch(const std::exception& e) {
        std::cerr<<"[AquariumVisitors] event=config_default reason="<<e.what()<<'\n';
    }
    return result;
}
int aquariumVisitorCapacity(int cells,const AquariumVisitorConfig& c) {
    return c.enabled ? std::min(c.max_per_room,std::max(0,cells)/std::max(1,c.cells_per_visitor)) : 0;
}
std::string nextAquariumVisitorAppearance(AquariumVisitorSession& s) {
    if(s.appearances.empty())return {};
    const auto result=s.appearances[s.appearance_cursor%s.appearances.size()];
    s.appearance_cursor=(s.appearance_cursor+1)%s.appearances.size();
    return result;
}
FacingDirection aquariumVisitorViewFacing(FacingDirection world,float fx,float fz) {
    float x=0,z=0;
    switch(world){
    case FacingDirection::North:z=-1;break;
    case FacingDirection::South:z=1;break;
    case FacingDirection::East:x=1;break;
    case FacingDirection::West:x=-1;break;
    }
    if(std::abs(fx)+std::abs(fz)<.001f)return world;
    const float right=-fz*x+fx*z,away=fx*x+fz*z;
    if(std::abs(right)>std::abs(away))return right>0?FacingDirection::East:FacingDirection::West;
    return away>0?FacingDirection::North:FacingDirection::South;
}
int aquariumVisitorSpriteRow(const CharacterSpriteDefinition& def,FacingDirection facing,float fx,float fz) {
    switch(aquariumVisitorViewFacing(facing,fx,fz)) {
    case FacingDirection::North:return def.row_north;
    case FacingDirection::South:return def.row_south;
    case FacingDirection::East:return def.row_east;
    case FacingDirection::West:return def.row_west;
    }
    return def.row_south;
}
std::vector<VisitorCell> aquariumVisitorRoute(int w,int h,const std::vector<unsigned char>& free,
    VisitorCell start,VisitorCell goal) {
    const auto valid=[&](VisitorCell p){return p.x>=0&&p.y>=0&&p.x<w&&p.y<h&&free[p.y*w+p.x];};
    if(w<=0||h<=0||free.size()!=std::size_t(w)*h||!valid(start)||!valid(goal))return {};
    std::vector<int> previous(free.size(),-1);
    std::deque<VisitorCell> queue{start};previous[start.y*w+start.x]=start.y*w+start.x;
    while(!queue.empty()) {
        const auto p=queue.front();queue.pop_front();
        if(p==goal)break;
        for(auto d:{VisitorCell{0,-1},{1,0},{0,1},{-1,0}}) {
            VisitorCell n{p.x+d.x,p.y+d.y};
            if(!valid(n)||previous[n.y*w+n.x]!=-1)continue;
            previous[n.y*w+n.x]=p.y*w+p.x;queue.push_back(n);
        }
    }
    if(previous[goal.y*w+goal.x]<0)return {};
    std::vector<VisitorCell> route;
    for(int n=goal.y*w+goal.x;n!=start.y*w+start.x;n=previous[n])route.push_back({n%w,n/w});
    std::reverse(route.begin(),route.end());return route;
}
} // namespace pr::gameplay::world3d::npc
