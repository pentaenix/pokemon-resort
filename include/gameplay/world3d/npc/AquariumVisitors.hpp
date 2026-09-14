#pragma once
#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/npc/AquariumVisitorDialogue.hpp"
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::npc {
struct VisitorCell {
    int x=0,y=0;
    bool operator==(const VisitorCell& other) const { return x==other.x && y==other.y; }
};
struct AquariumVisitorConfig {
    bool enabled=true;
    int cells_per_visitor=32,max_per_room=24;
    double initial_fill=.65,watch_min_seconds=15,watch_max_seconds=120;
    double arrival_seconds=35,exit_chance=.18,room_change_chance=.25;
    double adult_walk_speed_multiplier=.55;
};
AquariumVisitorConfig loadAquariumVisitorConfig(const std::string& project_root);
int aquariumVisitorCapacity(int reachable_cells,const AquariumVisitorConfig&);
// Four-connected routing uses the same blocked-cell mask as walking actors.
std::vector<VisitorCell> aquariumVisitorRoute(int width,int height,
    const std::vector<unsigned char>& walkable,VisitorCell start,VisitorCell goal);
struct VisitorWatchSpot {
    VisitorCell cell;
    FacingDirection facing=FacingDirection::North;
    std::string tank_id;
};
struct VisitorPortal {
    std::string id,destination_room,destination_door; // Empty room means exit/despawn.
    VisitorCell cell;
};
struct VisitorRoomPlan {
    std::string revision;
    std::vector<VisitorWatchSpot> spots;
    std::vector<VisitorPortal> portals;
    std::map<std::string,VisitorExhibitContext> exhibits;
};
struct AquariumVisitorRecord {
    std::string id,appearance,room,arrival_door,tank,portal;
    VisitorCell cell,goal,last_watch{-1,-1};
    FacingDirection facing=FacingDirection::South;
    bool watching=false,has_goal=false;
    double watch_left=0,retry=0,blocked_seconds=0;
    std::set<std::string> visited_tanks;
    unsigned conversation_count=0;
};
// Session-only: survives driver replacement on room travel, never edits saves.
struct AquariumVisitorSession {
    AquariumVisitorConfig config;
    std::map<std::string,AquariumVisitorRecord> visitors;
    std::set<std::string> initialized_rooms;
    std::map<std::string,int> capacities;
    std::vector<std::string> appearances;
    VisitorDialogueCatalog dialogue;
    std::deque<std::string> recent_dialogue;
    std::size_t appearance_cursor=0;
    std::uint64_t next_id=1;
};
std::string nextAquariumVisitorAppearance(AquariumVisitorSession&);
FacingDirection aquariumVisitorViewFacing(FacingDirection world_facing,float forward_x,float forward_z);
int aquariumVisitorSpriteRow(const CharacterSpriteDefinition&,FacingDirection,float forward_x,float forward_z);
} // namespace pr::gameplay::world3d::npc
