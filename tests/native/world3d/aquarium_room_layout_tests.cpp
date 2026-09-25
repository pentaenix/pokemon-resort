#include "gameplay/world3d/aquarium/rooms/AquariumBuildingLayout.hpp"
#include "core/config/Json.hpp"
#include "gameplay/world3d/aquarium/rooms/AquariumRoomRuntime.hpp"
#include "gameplay/world3d/aquarium/rooms/AquariumRoomStore.hpp"
#include "gameplay/world3d/aquarium/rooms/AquariumRoomDecorationCatalog.hpp"
#include "gameplay/world3d/interiors/DefaultRoom.hpp"
#include "gameplay/world3d/doors/DoorTravel.hpp"
#include <algorithm>
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
    future["schemaVersion"]=pr::JsonValue(6.0);
    const auto newer=room::parseBuildingLayout(pr::serializeJsonValue(future));
    require(newer.status==room::LayoutLoadStatus::NewerVersion && !newer.document,
        "newer room format was treated as editable");
    auto invalid=pr::parseJsonText(text);
    invalid["rooms"].asArray()[0]["bounds"]["widthCells"]=pr::JsonValue(12.5);
    require(room::parseBuildingLayout(pr::serializeJsonValue(invalid)).status==room::LayoutLoadStatus::Invalid,
        "fractional room dimensions were silently truncated");
    require(room::parseBuildingLayout("{}").status==room::LayoutLoadStatus::Invalid,"missing room schema accepted");
    auto styled=connect(room::Wall::West);
    styled.rooms.front().surfaces.floor_palette=4;
    styled.rooms.front().surfaces.wall_palette=2;
    styled.rooms.front().surfaces.floor_overrides.push_back({{3,4},1});
    styled.rooms.front().surfaces.floor_depth_overrides.push_back({{4,5},2});
    styled.rooms.front().surfaces.wall_overrides.push_back({room::Wall::West,5,3});
    const auto styled_round_trip=room::parseBuildingLayout(room::serializeBuildingLayout(styled));
    require(styled_round_trip.document && styled_round_trip.document->rooms.front().surfaces.floor_palette==4 &&
        styled_round_trip.document->rooms.front().surfaces.wall_palette==2,
        "room surface palettes did not round-trip");
    require(styled_round_trip.document->rooms.front().surfaces.floor_overrides.size()==1 &&
        styled_round_trip.document->rooms.front().surfaces.floor_depth_overrides.size()==1 &&
        styled_round_trip.document->rooms.front().surfaces.floor_depth_overrides.front().depth==2 &&
        styled_round_trip.document->rooms.front().surfaces.wall_overrides.size()==1,
        "sparse room surface/height painting did not round-trip");
}

void inferredRoomTransitions() {
    auto layout=existing();
    auto& room_a=layout.rooms.front();
    room_a.surfaces.floor_depth_overrides={{{5,5},1}};
    auto ramp=room::proposeRoomTransition(layout,"A",{5,5},room::RoomTransitionKind::Ramp);
    require(ramp.document.has_value(),"valid inferred room ramp was rejected");
    const auto* edited=room::findRoom(*ramp.document,"A");
    require(edited&&edited->transitions.size()==1&&edited->transitions.front().upper_cell.row==4,
        "transition did not infer its deterministic north attachment");

    pr::gameplay::world3d::SceneConfig scene;
    scene.id="A";scene.map_type="interior";scene.grid.width=31;scene.grid.height=21;
    scene.grid.tile_size=16;scene.terrain.height_per_floor=16;
    const auto projected=room::projectBuildingRoom(scene,*ramp.document,*edited);
    require(projected.terrain.specials[5][5]==2,
        "north room ramp did not project onto the lower cell");
    require(projected.terrain.transition_edges.size()==1 &&
        projected.terrain.transition_edges.front().lower_x==5 &&
        projected.terrain.transition_edges.front().lower_y==5 &&
        projected.terrain.transition_edges.front().upper_x==5 &&
        projected.terrain.transition_edges.front().upper_y==4 &&
        projected.terrain.transition_edges.front().stairs,
        "legacy room ramp was not presented as a stair transition");
    const auto layer=std::find_if(projected.tile_layers.layers.begin(),projected.tile_layers.layers.end(),
        [](const auto& value){return value.id=="aquarium_room_transitions";});
    require(layer!=projected.tile_layers.layers.end()&&layer->cells[5][5]==-1,
        "plain ramp unexpectedly gained a stair overlay");

    auto stairs=room::proposeRoomTransition(*ramp.document,"A",{5,5},room::RoomTransitionKind::Stairs);
    require(stairs.document.has_value(),"valid inferred room stairs were rejected");
    const auto* stair_room=room::findRoom(*stairs.document,"A");
    const auto stair_scene=room::projectBuildingRoom(scene,*stairs.document,*stair_room);
    require(stair_scene.terrain.specials[5][5]==2,
        "changing transition kind unexpectedly changed its direction");
    require(stair_scene.terrain.transition_edges.size()==1 &&
        stair_scene.terrain.transition_edges.front().stairs,
        "stairs were not projected as stepped render geometry");
    const auto stair_layer=std::find_if(stair_scene.tile_layers.layers.begin(),stair_scene.tile_layers.layers.end(),
        [](const auto& value){return value.id=="aquarium_room_transitions";});
    require(stair_layer!=stair_scene.tile_layers.layers.end()&&stair_layer->cells[5][5]==-1,
        "procedural stairs unexpectedly retained a coplanar RTPKS slope overlay");
    const auto round_trip=room::parseBuildingLayout(room::serializeBuildingLayout(*stairs.document));
    require(round_trip.document&&room::findRoom(*round_trip.document,"A")->transitions.size()==1,
        "room transition did not survive schema round trip");

    auto ambiguous=*stairs.document;
    // North and east are both implicit depth-zero landings around this lower cell.
    const auto cycled=room::proposeRoomTransition(ambiguous,"A",{5,5},room::RoomTransitionKind::Stairs);
    require(cycled.document&&room::findRoom(*cycled.document,"A")->transitions.front().upper_cell.column==6,
        "clicking an ambiguous transition did not cycle to its next valid edge");
    const auto south=room::proposeRoomTransition(*cycled.document,"A",{5,5},room::RoomTransitionKind::Stairs);
    require(south.document.has_value(),"south transition cycle was rejected");
    const auto west=room::proposeRoomTransition(*south.document,"A",{5,5},room::RoomTransitionKind::Stairs);
    require(west.document.has_value(),"west transition cycle was rejected");
    const auto removed=room::proposeRoomTransition(*west.document,"A",{5,5},room::RoomTransitionKind::Stairs);
    require(removed.document&&room::findRoom(*removed.document,"A")->transitions.empty(),
        "cycling past the final valid transition edge did not remove it");
    auto invalid=ambiguous;
    room::findRoom(invalid,"A")->transitions.front().upper_cell={7,5};
    require(!room::validateBuildingLayout(invalid).empty(),"non-cardinal room transition was accepted");
    auto too_tall=ambiguous;
    room::findRoom(too_tall,"A")->surfaces.floor_depth_overrides={{{5,5},2}};
    require(!room::validateBuildingLayout(too_tall).empty(),
        "aquarium stair spanning more than one floor was accepted");

    auto flight_layout=existing();
    room::findRoom(flight_layout,"A")->surfaces.floor_depth_overrides={{{8,11},3}};
    const auto flight=room::proposeRoomStairFlight(flight_layout,"A",{8,8},{8,11});
    require(flight.document.has_value(),"valid three-level stair flight was rejected");
    const auto* flight_room=room::findRoom(*flight.document,"A");
    require(flight_room&&flight_room->transitions.size()==3&&
        room::roomFloorDepth(*flight_room,{8,8})==0&&
        room::roomFloorDepth(*flight_room,{8,9})==1&&
        room::roomFloorDepth(*flight_room,{8,10})==2&&
        room::roomFloorDepth(*flight_room,{8,11})==3,
        "stair flight did not create deterministic one-level intermediate bands");
    require(!room::proposeRoomStairFlight(flight_layout,"A",{8,9},{8,11}).document,
        "stair flight accepted fewer cells than its level change requires");

    auto wide_layout=existing();
    auto* wide_room=room::findRoom(wide_layout,"A");
    for(int row=9;row<=11;++row)for(int column=8;column<=9;++column)
        wide_room->surfaces.floor_depth_overrides.push_back({{column,row},3});
    const auto wide=room::proposeRoomStairArea(wide_layout,"A",{8,9},{9,11});
    require(wide.document.has_value(),"three-by-two stair area beside level zero was rejected");
    const auto* built=room::findRoom(*wide.document,"A");
    require(built&&built->transitions.size()==6&&
        room::roomFloorDepth(*built,{8,9})==1&&room::roomFloorDepth(*built,{9,9})==1&&
        room::roomFloorDepth(*built,{8,10})==2&&room::roomFloorDepth(*built,{9,10})==2&&
        room::roomFloorDepth(*built,{8,11})==3&&room::roomFloorDepth(*built,{9,11})==3,
        "wide stair area did not derive parallel intermediate depth bands");
    const auto removed_wide=room::proposeRoomStairRemovalArea(*wide.document,"A",{9,10},{9,10});
    const auto* restored=removed_wide.document?room::findRoom(*removed_wide.document,"A"):nullptr;
    require(restored&&restored->transitions.empty(),
        "marking one stair block did not remove its complete generated flight");
    for(int row=9;row<=11;++row)for(int column=8;column<=9;++column)
        require(room::roomFloorDepth(*restored,{column,row})==3,
            "removing a generated stair flight did not restore its lower platform");
    require(!room::proposeRoomStairArea(wide_layout,"A",{8,10},{9,11}).document,
        "short stair area accepted a three-level rise");
    auto cut_layout=wide_layout;
    auto& cut_depths=room::findRoom(cut_layout,"A")->surfaces.floor_depth_overrides;
    std::find_if(cut_depths.begin(),cut_depths.end(),[](const auto& item) {
        return item.cell.column==8&&item.cell.row==9;
    })->depth=1;
    require(!room::proposeRoomStairArea(cut_layout,"A",{8,9},{9,11}).document,
        "stair drawing cut through a non-uniform terrain selection");
    auto oversized_layout=existing();
    auto* oversized_room=room::findRoom(oversized_layout,"A");
    for(int row=7;row<=11;++row)for(int column=8;column<=9;++column)
        oversized_room->surfaces.floor_depth_overrides.push_back({{column,row},3});
    const auto oversized=room::proposeRoomStairArea(oversized_layout,"A",{8,7},{9,11});
    const auto* fitted=oversized.document?room::findRoom(*oversized.document,"A"):nullptr;
    require(fitted&&fitted->transitions.size()==6&&
        std::all_of(fitted->transitions.begin(),fitted->transitions.end(),[](const auto& item) {
            return item.lower_cell.row<=9;
        }),"oversized selection made a three-level stair flight longer than three cells");
}

void floorDepthProjectionAndSafety() {
    pr::gameplay::world3d::SceneConfig scene;
    scene.id="aquarium_builder_lab";scene.map_type="interior";
    scene.grid.width=12;scene.grid.height=10;scene.grid.tile_size=16;
    scene.terrain.height_per_floor=8;
    auto layout=room::roomLayoutFromScene(scene);
    auto& target=layout.rooms.front();
    target.surfaces.floor_depth_overrides={{{4,4},1},{{5,4},3},{{6,4},5}};
    const auto projected=room::projectRoomLayout(scene,target);
    require(projected.terrain.base_height_world==-40.0f,
        "procedural room floor baseline did not reserve five depression levels");
    require(projected.terrain.heights[3][3]==5&&projected.terrain.heights[4][4]==4&&
        projected.terrain.heights[4][5]==2&&projected.terrain.heights[4][6]==0,
        "room floor depths did not project to deterministic terrain heights");
    require(projected.terrain.base_height_world+
        projected.terrain.heights[3][3]*projected.terrain.height_per_floor==0.0f &&
        projected.terrain.base_height_world+
        projected.terrain.heights[4][5]*projected.terrain.height_per_floor==-24.0f,
        "default and three-step floor world heights are incorrect");
    require(projected.terrain.base_height_world+
        projected.terrain.heights[4][6]*projected.terrain.height_per_floor==-40.0f,
        "fifth room depth did not project five complete floor steps below datum");
    require(room::validateBuildingLayout(layout).empty(),
        "valid fifth room depression level was rejected");
    auto too_deep=layout;
    room::findRoom(too_deep,"aquarium_builder_lab")->surfaces.floor_depth_overrides.push_back(
        {{7,4},6});
    require(!room::validateBuildingLayout(too_deep).empty(),
        "room depression deeper than five levels was accepted");

    auto doorway=existing();
    const auto protected_cell=room::protectedDoorCells(
        doorway.rooms.front(),doorway.rooms.front().doors.front()).front();
    doorway.rooms.front().surfaces.floor_depth_overrides.push_back({{
        protected_cell.column-doorway.rooms.front().bounds.column,
        protected_cell.row-doorway.rooms.front().bounds.row},1});
    require(room::validateBuildingLayout(doorway).empty(),
        "lowered doorway lane was rejected by permissive height editing");
    auto wall_edge=existing();
    wall_edge.rooms.front().surfaces.floor_depth_overrides.push_back({{0,8},1});
    require(room::validateBuildingLayout(wall_edge).empty(),
        "lowered outer wall edge was rejected");

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

void roomDecorationLibrary() {
    room::RoomDecorationCatalog catalog;
    catalog.scan(PR_SOURCE_DIR);
    require(catalog.error().empty(),"room decoration library could not be scanned");
    require(catalog.entries().size()>=4,"moved aquarium signs are missing from room decorations");
    const auto find=[&](const char* id)->const room::RoomDecorationAsset* {
        const auto found=std::find_if(catalog.entries().begin(),catalog.entries().end(),
            [&](const auto& asset){return asset.id==id;});
        return found==catalog.entries().end()?nullptr:&*found;
    };
    const auto* sign=find("signs/sign01_preview.glb");
    require(sign&&sign->category=="equipment","sign category inference changed unexpectedly");
    const auto sign_cells=room::roomDecorationCollisionCells(*sign,{4,6},0,16);
    require(sign_cells.size()==1&&sign_cells.front().column==4&&sign_cells.front().row==6,
        "room signs must occupy exactly their placement cell");
    const auto* gate=find("c15_gate_02_preview.glb");
    require(gate&&gate->category=="structures"&&
        gate->collision.mode==room::RoomDecorationCollisionMode::CellMask,
        "gate collision profile was not applied from the asset catalog");
    require(!catalog.indices("structures").empty()&&!catalog.indices("furniture").empty()&&
        !catalog.indices("nature").empty()&&!catalog.indices("equipment").empty(),
        "room-decoration tabs did not receive best-estimate asset categories");
    const auto horizontal=room::roomDecorationCollisionCells(*gate,{10,8},0,16);
    require(horizontal.size()==2&&horizontal[0].column==7&&horizontal[0].row==8&&
        horizontal[1].column==13&&horizontal[1].row==8,
        "gate mask must block only its two pillar cells");
    const auto vertical=room::roomDecorationCollisionCells(*gate,{10,8},1,16);
    require(vertical.size()==2&&vertical[0].column==10&&vertical[0].row==11&&
        vertical[1].column==10&&vertical[1].row==5,
        "gate collision mask did not rotate with the decoration");
    const auto* asymmetric=find("c4_gate_01_preview.glb");
    require(asymmetric,"asymmetric gate is missing from the room decoration library");
    const auto asymmetric_vertical=room::roomDecorationCollisionCells(*asymmetric,{10,8},1,16);
    require(asymmetric_vertical.size()==5&&asymmetric_vertical.front().row==12&&
        asymmetric_vertical.back().row==4,
        "asymmetric gate collision mask rotates opposite to its visual model");
    room::RoomDecorationAsset measured;
    measured.bounds.valid=true;measured.bounds.min_x=-28;measured.bounds.max_x=28;
    measured.bounds.min_z=-24;measured.bounds.max_z=24;
    const auto measured_cells=room::roomDecorationCollisionCells(measured,{8,8},0,16);
    require(measured_cells.size()==12,
        "measured decoration footprint gained a partial-edge border cell");
    room::RoomDecorationAsset beveled_bench;
    beveled_bench.bounds.valid=true;
    beveled_bench.bounds.min_x=-8.15f;beveled_bench.bounds.max_x=8.14f;
    beveled_bench.bounds.min_z=-24.08f;beveled_bench.bounds.max_z=24.07f;
    const auto bench_cells=room::roomDecorationCollisionCells(beveled_bench,{12,8},0,16);
    require(bench_cells.size()==3&&bench_cells.front().column==12&&
        bench_cells.front().row==7&&bench_cells.back().row==9,
        "near-cell bevels must not add a phantom north or west collision cell");
    const auto* fountain=find("c15_fountain_01_preview.glb");
    require(fountain&&fountain->collision.mode==room::RoomDecorationCollisionMode::None&&
        fountain->surface_effect==room::RoomDecorationSurfaceEffect::ShallowWater,
        "floor fountain must be walkable shallow water");
    require(room::roomDecorationCollisionCells(*fountain,{8,8},0,16).empty(),
        "floor fountain unexpectedly blocks player movement");
    require(!room::roomDecorationPlacementCells(*fountain,{8,8},0,16).empty(),
        "walkable floor fountain lost its editor placement footprint");
    const auto* small_fountain=find("fountain_01_preview.glb");
    require(small_fountain&&small_fountain->animation_playback_rate<0.0f,
        "small fountain lost its corrected animation direction");
    const auto small_fountain_cells=room::roomDecorationCollisionCells(
        *small_fountain,{8,8},0,16);
    require(small_fountain_cells.size()==9&&
        std::none_of(small_fountain_cells.begin(),small_fountain_cells.end(),[](const auto cell) {
            return cell.column<7||cell.column>9||cell.row<7||cell.row>9;
        }),"small fountain must use its authored three-by-three collision mask");
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
        floorDepthProjectionAndSafety(); inferredRoomTransitions();
        runtimeProjectionAndClearance(); wallHandlesAndProjectedLinks(); fastRoomScriptIsRegistered();
        roomDecorationLibrary(); durableRoomStore(); }
    catch (const std::exception& error) { std::cerr<<"aquarium_room_layout_tests: "<<error.what()<<'\n'; return 1; }
    std::cout<<"aquarium_room_layout_tests: independent doors, safe resizing and JSON passed\n";
}
