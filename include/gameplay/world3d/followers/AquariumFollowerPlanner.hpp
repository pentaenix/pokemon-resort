#pragma once
#include "gameplay/world3d/npc/AquariumVisitors.hpp"
#include <optional>
#include <random>

namespace pr::gameplay::world3d::followers {
struct AquariumFollowerTrailTarget {
    std::optional<npc::VisitorCell> cell;
    void observe(bool step_active,npc::VisitorCell from) { if(step_active)cell=from; }
};
std::vector<npc::VisitorCell> aquariumFollowerTrailRoute(const std::vector<npc::VisitorCell>&,
    npc::VisitorCell follower,npc::VisitorCell target);
struct AquariumFollowerPlan {
    npc::VisitorWatchSpot target;
    std::vector<npc::VisitorCell> route;
};
npc::VisitorCell aquariumFollowerBehind(npc::VisitorCell player,FacingDirection facing);
// Receives combined terrain/player/NPC occupancy; never emits room travel.
std::optional<AquariumFollowerPlan> planAquariumFollowerVisit(int width,int height,
    const std::vector<unsigned char>& walkable,npc::VisitorCell follower,npc::VisitorCell player,
    const std::vector<npc::VisitorWatchSpot>& spots,const std::set<std::string>& seen,
    bool return_to_player,std::mt19937& rng,FacingDirection player_facing=FacingDirection::North,
    std::optional<npc::VisitorCell> trailing_cell={},bool join_owner=false);
}
