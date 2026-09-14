#include "gameplay/world3d/aquarium/rooms/AquariumBuildingLayout.hpp"
#include "core/config/Json.hpp"
#include "gameplay/world3d/aquarium/rooms/AquariumRoomRuntime.hpp"
#include "gameplay/world3d/aquarium/rooms/AquariumRoomStore.hpp"
#include "gameplay/world3d/interiors/DefaultRoom.hpp"
#include "gameplay/world3d/doors/DoorTravel.hpp"
#include <fstream>
#include <unistd.h>

#include <iostream>
#include <stdexcept>

namespace {
namespace room=pr::gameplay::world3d::aquarium::rooms;
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
room::BuildingLayout existing() {
    room::BuildingLayout layout;
    layout.id="aquarium";
    layout.rooms.push_back({"A",{0,0,31,21},{{"outside",room::Wall::South,15}}});
    layout.external_connections.push_back({"outside_link",{"A","outside"},"resort","aquarium_exit"});
    return layout;
}
room::BuildingLayout connect(room::Wall wall) {
    const auto proposal=room::proposeConnectedRoom(existing(),"A",wall,5,"to_B","B","to_A","AB");
    require(proposal.document.has_value(),"valid adjacent-room proposal failed");
    return *proposal.document;
}

void independentDoorsAndDefaultSize() {
    for (auto wall:{room::Wall::North,room::Wall::East,room::Wall::South,room::Wall::West}) {
        auto layout=connect(wall);
        const auto* b=room::findRoom(layout,"B");
        const auto* receiving=room::findDoor(*b,"to_A");
        require(receiving->wall==room::oppositeWall(wall),"receiving door must be on the opposite wall");
        const int length=wall==room::Wall::North || wall==room::Wall::South ? b->bounds.width : b->bounds.depth;
        require(receiving->offset*2+1==length,"new receiving doorway must be exactly centred on whole cells");
        require(room::findRoom(layout,"A")->bounds.width==31,"new-room defaults resized existing room");
        const auto original=room::serializeBuildingLayout(layout);
        const auto moved=room::proposeDoorMove(layout,{"B","to_A"},3);
        require(moved.document.has_value(),"valid receiving-door slide was rejected");
        require(room::findDoor(*room::findRoom(*moved.document,"A"),"to_B")->offset==5,
            "moving B's doorway changed A's doorway");
        const auto landing=room::resolveDoorLanding(*moved.document,{"A","to_B"});
        const auto expected=room::doorLandingCell(*room::findRoom(*moved.document,"B"),
            *room::findDoor(*room::findRoom(*moved.document,"B"),"to_A"));
        require(landing && landing->cell.column==expected.column && landing->cell.row==expected.row && landing->facing==wall,
            "travel did not resolve the moved destination door with inward facing");
        const auto back=room::resolveDoorLanding(*moved.document,{"B","to_A"});
        require(back && back->endpoint.room_id=="A" && back->endpoint.door_id=="to_B",
            "independent door edit broke the reverse link");
        require(room::serializeBuildingLayout(layout)==original,"candidate door edit mutated committed input");
        const auto resized=room::proposeRoomResize(*moved.document,"B",{-3,-2,23,19});
        require(resized.document && resized.document->revision==moved.document->revision+1,
            "room resize failed to preserve a monotonic revision");
        require(room::findRoom(*resized.document,"A")->bounds.column==0 &&
            room::findDoor(*room::findRoom(*resized.document,"A"),"to_B")->offset==5,
            "resizing B changed A's coordinates");
    }
}

void invalidEditsProtectTanksAndLinks() {
    auto layout=connect(room::Wall::North);
    require(!room::proposeDoorMove(layout,{"A","to_B"},0).document,"door corner overlap was accepted");
    require(!room::proposeDoorMove(layout,{"A","missing"},4).document,"missing door was silently created");
    require(!room::proposeDoorMove(layout,{"A","to_B"},7,{{"A",{{7,0}}}}).document,
        "doorway moved into a tank footprint");
    require(!room::proposeRoomResize(layout,"A",{0,0,20,21},{{"A",{{25,10}}}}).document,
        "shrinking room discarded occupied tank cells");
    room::RoomOccupancy barrier{"A",{}};
    for (int x=0;x<31;++x) barrier.cells.push_back({x,10});
    require(!room::validateBuildingLayout(layout,{barrier}).empty(),"tank wall severed door-to-door circulation");
    auto wrong=layout;
    wrong.rooms.back().doors.front().wall=room::Wall::West;
    require(!room::validateBuildingLayout(wrong).empty(),"non-opposite walls were accepted");
    wrong=layout; wrong.connections[0].second.door_id="missing";
    require(!room::validateBuildingLayout(wrong).empty(),"dangling door endpoint was accepted");
    wrong=layout; wrong.connections.push_back(wrong.connections[0]);
    require(!room::validateBuildingLayout(wrong).empty(),"duplicate door links were accepted");
    require(layout.external_connections[0].destination_map_id=="resort" &&
        layout.external_connections[0].destination_anchor_id=="aquarium_exit",
        "room operation modified the existing external exit");
}

void serializationAndFutureVersions() {
    const auto text=room::serializeBuildingLayout(connect(room::Wall::West));
    const auto parsed=room::parseBuildingLayout(text);
    require(parsed.status==room::LayoutLoadStatus::Loaded && parsed.document &&
        room::serializeBuildingLayout(*parsed.document)==text,"canonical room JSON did not round-trip");
    auto future=pr::parseJsonText(text);
    future["schemaVersion"]=pr::JsonValue(2.0);
    const auto newer=room::parseBuildingLayout(pr::serializeJsonValue(future));
    require(newer.status==room::LayoutLoadStatus::NewerVersion && !newer.document,
        "newer room format was treated as editable");
    auto invalid=pr::parseJsonText(text);
    invalid["rooms"].asArray()[0]["bounds"]["widthCells"]=pr::JsonValue(12.5);
    require(room::parseBuildingLayout(pr::serializeJsonValue(invalid)).status==room::LayoutLoadStatus::Invalid,
        "fractional room dimensions were silently truncated");
    require(room::parseBuildingLayout("{}").status==room::LayoutLoadStatus::Invalid,"missing room schema accepted");
}

void runtimeProjectionAndClearance() {
    pr::gameplay::world3d::SceneConfig scene;
    scene.id="aquarium_builder_lab"; scene.map_type="interior";
    scene.grid.width=24; scene.grid.height=18; scene.grid.tile_size=16;
    scene.interior.default_room.wall_face_offset_tiles=.5f;
    scene.interior.openings={{"south",11,13}};
    scene.anchors={{"from_resort",12,17,pr::gameplay::world3d::FacingDirection::North}};
    scene.links={{"exit","0",""}};
    for(int x=11;x<=13;++x) {
        pr::gameplay::world3d::DoorTriggerConfig t;
        t.id="exit"+std::to_string(x); t.link_id="exit"; t.tile_x=x; t.tile_y=18;
        scene.door_triggers.push_back(t);
    }
    auto layout=room::roomLayoutFromScene(scene);
    require(room::validateBuildingLayout(layout).empty(),"unnamed external destination anchor must be preserved");
    const auto resized=room::proposeRoomResize(layout,scene.id,{0,0,30,25});
    require(resized.document.has_value(),"safe room expansion rejected");
    const auto projected=room::projectRoomLayout(scene,resized.document->rooms.front());
    require(projected.grid.width==30 && projected.terrain.collision.size()==25 &&
        projected.terrain.collision.back().size()==30,"room projection did not resize terrain planes");
    require(projected.anchors.front().tile_y==24 && projected.anchors.front().tile_x==12,
        "south arrival must move with wall without sideways recentering");
    for(const auto& t:projected.door_triggers)
        require(t.tile_y==25 && t.tile_x>=11 && t.tile_x<=13 && t.link_id=="exit",
            "all three south triggers must move with the room wall and retain their link");
    require(scene.grid.width==24 && scene.anchors.front().tile_y==17,"projection mutated source map");
    require(room::roomDrawingCellFits(scene,21,15),"last complete safe drawing cell rejected");
    require(!room::roomDrawingCellFits(scene,22,15) && !room::roomDrawingCellFits(scene,21,16),
        "east/south half-cell wall strip is drawable");
    require(!room::roomDrawingCellFits(scene,0,2) && !room::roomDrawingCellFits(scene,2,0),
        "north/west clearance must use the same wall-face transform");
    pr::gameplay::world3d::aquarium::AquariumConstructionConfig config;
    config.enabled=true;
    config=room::roomConstructionConfig(projected,config,true);
    require(config.return_cell.column==12 && config.return_cell.row==24,"safe return cell stayed at old south wall");
    for(auto p:config.allowed_cells)
        require(room::roomDrawingCellFits(projected,p.column,p.row),"build mask contains partial wall cells");
}

void wallHandlesAndProjectedLinks() {
    namespace world=pr::gameplay::world3d;
    auto original=connect(room::Wall::East);
    const auto west=room::proposeWallMove(original,"A",room::Wall::West,-4);
    require(west.document && room::findRoom(*west.document,"A")->bounds.width==35,
        "west handle must move west wall, keeping east wall fixed");
    require(room::findDoor(*room::findRoom(*west.document,"A"),"outside")->offset==19,
        "west expansion moved the south door's stable position");
    const auto north=room::proposeWallMove(*west.document,"A",room::Wall::North,-3);
    require(north.document && room::findRoom(*north.document,"A")->bounds.depth==24 &&
        room::findDoor(*room::findRoom(*north.document,"A"),"to_B")->offset==8,
        "north expansion moved the east door's stable position");
    require(!room::proposeWallMove(original,"A",room::Wall::East,20,{{"A",{{25,10}}}}).document,
        "wall handle discarded occupied cells");
    world::SceneConfig style;
    style.map_type="interior";style.grid.tile_size=16;
    for(auto wall:{room::Wall::North,room::Wall::East,room::Wall::South,room::Wall::West}) {
        const auto layout=connect(wall);
        std::vector<world::characters::LoadedWorldChunk> chunks;
        for(const auto& r:layout.rooms)
            chunks.push_back({r.id,room::projectBuildingRoom(style,layout,r),int(chunks.size())*4096,0});
        for(const auto& chunk:chunks) for(const auto& trigger:chunk.scene.door_triggers) {
            if(trigger.link_id!="AB") continue;
            const int dx=trigger.tile_x<0?-1:trigger.tile_x>=chunk.scene.grid.width?1:0;
            const int dy=trigger.tile_y<0?-1:trigger.tile_y>=chunk.scene.grid.height?1:0;
            const int x=chunk.origin_tile_x+trigger.tile_x,y=trigger.tile_y;
            const auto hit=world::doors::findDoorTrigger(chunks,x-dx,y-dy,x,y,dx,dy);
            require(hit.has_value(),"one of three doorway cells cannot be entered");
            const auto destination=world::doors::resolveDoorDestination(chunks,*hit);
            require(destination && destination->chunk->id!=chunk.id,
                "generated doorway does not resolve the linked receiving room");
            require(trigger.script_id=="aquarium_room_transfer",
                "internal doorway must use facing-relative travel, not south-only exit");
        }
    }
}

void fastRoomScriptIsRegistered() {
    const auto root=std::filesystem::path(PR_SOURCE_DIR)/"config/gameplay/world3d/scripts";
    const auto script=pr::parseJsonFile((root/"doors/aquarium_room_transfer.json").string());
    const auto& actions=script.get("actions")->asArray();
    require(actions.size()==4,"fast room travel gained a hidden animation wait");
    require(actions[0].get("action")->asString()=="TRANSITION_CLOSE" &&
        actions[1].get("action")->asString()=="TELEPORT_TO_LINK" &&
        actions[2].get("action")->asString()=="TRANSITION_OPEN" &&
        actions[3].get("direction")->asString()=="forward",
        "fast room travel must mask loading and use receiving-door facing");
    const double duration=actions[0].get("durationSeconds")->asNumber()+
        actions[2].get("durationSeconds")->asNumber()+actions[3].get("durationSeconds")->asNumber();
    require(duration>.4 && duration<.5,"room travel scripted delay is no longer under half a second");
    const auto catalog=pr::parseJsonFile((root/"script_catalog.json").string());
    bool found=false;
    for(const auto& item:catalog.get("scripts")->asArray())
        found|=item.get("path")->asString()=="doors/aquarium_room_transfer.json";
    require(found,"fast room travel script missing from runtime catalog");
}

void durableRoomStore() {
    char pattern[]="/tmp/aquarium-room-test-XXXXXX";
    const auto* directory=::mkdtemp(pattern);
    require(directory!=nullptr,"test temp directory failed");
    const std::filesystem::path root=directory, path=root/"room.json";
    room::AquariumRoomStore store(path);
    auto first=existing(); std::string error;
    require(!store.exists() && store.save(first,error),"initial room save failed");
    auto second=*room::proposeRoomResize(first,"A",{0,0,35,25}).document;
    require(store.save(second,error) && store.load().document->revision==1,"room save did not round-trip");
    {std::ofstream out(path); out<<"broken";}
    require(store.load().document && store.load().document->revision==0,"corrupt primary did not recover backup");
    auto future=pr::parseJsonText(room::serializeBuildingLayout(second));
    future["schemaVersion"]=pr::JsonValue(999.0);
    {std::ofstream out(path.string()+".tmp"); out<<pr::serializeJsonValue(future);}
    require(store.load().status==room::LayoutLoadStatus::NewerVersion && !store.save(first,error),
        "future temporary save must be preserved read-only");
    std::filesystem::remove_all(root); // exact, newly-created test-only directory
}
}
int main() {
    try { independentDoorsAndDefaultSize(); invalidEditsProtectTanksAndLinks(); serializationAndFutureVersions();
        runtimeProjectionAndClearance(); wallHandlesAndProjectedLinks(); fastRoomScriptIsRegistered(); durableRoomStore(); }
    catch (const std::exception& error) { std::cerr<<"aquarium_room_layout_tests: "<<error.what()<<'\n'; return 1; }
    std::cout<<"aquarium_room_layout_tests: independent doors, safe resizing and JSON passed\n";
}
