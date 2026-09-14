#include "gameplay/world3d/aquarium/AquariumInspectionCamera.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace aq=pr::gameplay::world3d::aquarium;
namespace cam=pr::gameplay::world3d::camera;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
    aq::AquariumTankRuntime tank;tank.placement_id="tank:test";tank.world_center={0,40,0};
    tank.half_width_world=160;tank.half_depth_world=64;tank.water_bottom_world=-32;tank.water_top_world=144;
    tank.units_per_meter_world=16;tank.inspection_camera.enabled=true;
    tank.inspection_camera.smooth=0;tank.inspection_camera.has_framed_inspection_view=true;
    tank.inspection_camera.interaction_reach_tiles=2;
    cam::Gen4FollowCamera camera({});camera.setTarget({120,0,80});
    aq::AquariumInspectionCamera inspection({tank});
    check(inspection.tryBegin({120,0,80},pr::gameplay::world3d::FacingDirection::North,16,camera),"inspection did not enter");
    inspection.update(0,camera);
    check(std::abs(camera.pose().forward.x)<.15f,"first view must look ahead instead of across tank toward center");
    check(!inspection.hidesOverworldActors(),"player hidden in first view");
    check(inspection.enterFocused(16,camera,1.6f),"whole-tank stage did not enter");inspection.update(0,camera);
    check(!inspection.hidesOverworldActors(),"player hidden in overview");
    for(float x:{-160.0f,160.0f})for(float y:{-32.0f,144.0f})for(float z:{-64.0f,64.0f}){
        float sx,sy,depth;check(camera.worldToScreen({x,y,z},1280,800,sx,sy,depth)&&sx>0&&sx<1280&&sy>0&&sy<800,
            "overview must frame the complete tank bounds");
    }
    const auto position=camera.pose().position;const auto heading=camera.pose().forward;
    inspection.setPointerLook(1,-1);inspection.update(.2,camera);
    check(camera.pose().position.x==position.x&&camera.pose().position.y==position.y&&camera.pose().position.z==position.z,
        "mouse look must rotate without translating the camera");
    check(camera.pose().forward.x!=heading.x,"mouse look did not turn camera");
    check(inspection.focusPokemon("tank:test:fish",{0,30,0},8,camera),"Pokemon focus did not enter");
    const auto outside=camera.pose().position;
    inspection.trackPokemon({8,35,0});inspection.update(0,camera);
    check(camera.pose().position.z>=tank.half_depth_world+24 && camera.pose().position.z<outside.z,
        "Pokemon focus must approach the glass without crossing it");
    check(camera.pose().preset.fov_y_deg<30,"small Pokemon must receive optical close-up zoom outside the tank");
    inspection.trackPokemon({8,20,-50});inspection.update(0,camera);
    check(camera.pose().position.z>=tank.half_depth_world+24 && camera.pose().preset.fov_y_deg<30,
        "distant Pokemon must stay zoomed while camera remains outside glass");
    check(inspection.stage()==aq::AquariumInspectionCamera::Stage::Pokemon,"Pokemon stage missing");
    check(inspection.leavePokemon()&&inspection.focused(),"accept must return from Pokemon to overview");
    inspection.beginExit({120,0,80},camera);check(!inspection.active(),"next accept must exit overview");
    check(std::abs(camera.pose().preset.fov_y_deg-30)<.01f,"exit must restore normal field of view");
    tank.half_width_world=tank.half_depth_world=12;tank.water_bottom_world=0;tank.water_top_world=32;
    aq::AquariumInspectionCamera small({tank});camera.setTarget({0,0,28});
    check(small.tryBegin({0,0,28},pr::gameplay::world3d::FacingDirection::North,16,camera),"small tank entry failed");
    small.update(0,camera);const auto first=camera.pose().position;
    small.enterFocused(16,camera);small.update(0,camera);const auto second=camera.pose().position;
    check(std::hypot(second.y-16,second.z)>std::hypot(first.y-16,first.z),
        "second accept must zoom out even for a small tank");
    auto peer=tank;peer.placement_id="peer";peer.world_center={80,16,0};
    auto behind=tank;behind.placement_id="behind";behind.world_center={0,16,200};
    auto overhead=tank;overhead.placement_id="overhead";overhead.world_center={0,200,100};
    overhead.water_bottom_world=180;overhead.water_top_world=220;
    auto blocker=tank;blocker.placement_id="foreground";blocker.world_center={0,16,60};
    auto side=tank;side.placement_id="foreground-side";side.world_center={100,16,60};
    aq::AquariumInspectionCamera visibility({tank,peer,behind,overhead,blocker,side});
    check(visibility.tryBegin({0,0,28},pr::gameplay::world3d::FacingDirection::North,16,camera),"visibility fixture entry failed");
    camera.setManualPose({0,16,100},180,0);
    const auto hidden=visibility.hiddenPlacementIds(camera.pose());
    auto hiddenId=[&](const std::string& id){return std::find(hidden.begin(),hidden.end(),id)!=hidden.end();};
    check(!hiddenId(tank.placement_id)&&!hiddenId("peer")&&hiddenId("behind")&&hiddenId("overhead")&&
        hiddenId("foreground")&&!hiddenId("foreground-side"),
        "inspection must hide foreground obstructions while preserving same-depth and non-obstructing peers");
    auto below=tank;below.placement_id="below-camera";below.world_center={0,16,100};
    aq::AquariumInspectionCamera elevated({tank,peer,below});
    check(elevated.tryBegin({0,0,28},pr::gameplay::world3d::FacingDirection::North,16,camera),
        "elevated first-view fixture entry failed");
    camera.setManualPose({0,40,100},180,0);
    const auto elevated_hidden=elevated.hiddenPlacementIds(camera.pose());
    check(std::find(elevated_hidden.begin(),elevated_hidden.end(),"below-camera")!=elevated_hidden.end(),
        "first view must hide a rear tank beneath the camera, even above its water surface");
    check(std::find(elevated_hidden.begin(),elevated_hidden.end(),"peer")==elevated_hidden.end(),
        "elevated first view must preserve neighboring exhibits");
    std::cout<<"aquarium_inspection_tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
