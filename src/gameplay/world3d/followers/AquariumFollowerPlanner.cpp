#include "gameplay/world3d/followers/AquariumFollowerPlanner.hpp"
#include <algorithm>
#include <cstdlib>

namespace pr::gameplay::world3d::followers {
std::vector<npc::VisitorCell> aquariumFollowerTrailRoute(const std::vector<npc::VisitorCell>& trail,
    npc::VisitorCell follower,npc::VisitorCell target) {
    for(std::size_t i=trail.size();i>0;--i)if(trail[i-1]==follower) {
        std::vector<npc::VisitorCell> route;auto previous=follower;
        for(std::size_t j=i;j<trail.size();++j) {
            const auto p=trail[j];
            if(std::abs(p.x-previous.x)+std::abs(p.y-previous.y)!=1)return {};
            route.push_back(p);if(p==target)return route;previous=p;
        }
        return {};
    }
    return {};
}
npc::VisitorCell aquariumFollowerBehind(npc::VisitorCell p,FacingDirection facing) {
    switch(facing) {
    case FacingDirection::North:++p.y;break;
    case FacingDirection::South:--p.y;break;
    case FacingDirection::East:--p.x;break;
    case FacingDirection::West:++p.x;break;
    }
    return p;
}
std::optional<AquariumFollowerPlan> planAquariumFollowerVisit(int w,int h,
    const std::vector<unsigned char>& free,npc::VisitorCell follower,npc::VisitorCell player,
    const std::vector<npc::VisitorWatchSpot>& spots,const std::set<std::string>& seen,
    bool return_to_player,std::mt19937& rng,FacingDirection player_facing,
    std::optional<npc::VisitorCell> trailing_cell,bool join_owner) {
    const auto available=[&](npc::VisitorCell p){return p.x>=0&&p.y>=0&&p.x<w&&p.y<h&&
        free.size()==std::size_t(w)*h&&free[p.y*w+p.x]&&!(p==player);};
    std::vector<npc::VisitorWatchSpot> choices;
    if(!return_to_player)for(const auto& s:spots)
        if(available(s.cell)&&!(s.cell==follower)&&std::abs(s.cell.x-player.x)+std::abs(s.cell.y-player.y)<=12)choices.push_back(s);
    std::shuffle(choices.begin(),choices.end(),rng);
    std::stable_sort(choices.begin(),choices.end(),[&](const auto& a,const auto& b){return seen.count(a.tank_id)<seen.count(b.tank_id);});
    // A small bounded number of alternatives prevents expensive crowd replans.
    if(choices.size()>8)choices.resize(8);
    if(join_owner&&!return_to_player) {
        const auto owner_view=std::find_if(spots.begin(),spots.end(),[&](const auto& s){return s.cell==player&&s.facing==player_facing;});
        if(owner_view!=spots.end()) {
            const bool north_south=player_facing==FacingDirection::North||player_facing==FacingDirection::South;
            std::vector<npc::VisitorWatchSpot> beside;
            for(const auto& s:spots)if(s.tank_id==owner_view->tank_id&&s.facing==player_facing&&available(s.cell)&&
                (north_south?(s.cell.y==player.y&&std::abs(s.cell.x-player.x)==1):
                    (s.cell.x==player.x&&std::abs(s.cell.y-player.y)==1)))beside.push_back(s);
            std::shuffle(beside.begin(),beside.end(),rng);
            choices.insert(choices.begin(),beside.begin(),beside.end());
        }
    }
    const auto behind=trailing_cell.value_or(aquariumFollowerBehind(player,player_facing));
    if(available(behind))choices.push_back({behind,player_facing,{}});
    for(const auto& s:choices) {
        auto route=npc::aquariumVisitorRoute(w,h,free,follower,s.cell);
        if((!route.empty()||follower==s.cell)&&route.size()<=40)return AquariumFollowerPlan{s,std::move(route)};
    }
    return {};
}
}
