#include "core/config/Json.hpp"
#include "gameplay/world3d/aquarium/AquariumBodyNavigation.hpp"
#include "gameplay/world3d/aquarium/AquariumMotion.hpp"
#include "gameplay/world3d/aquarium/AquariumSimulation.hpp"
#include "aquarium_geometry/Kernel.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
namespace aq=pr::gameplay::world3d::aquarium;
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
float distance(aq::Point3 a,aq::Point3 b) {
    return std::sqrt((a[0]-b[0])*(a[0]-b[0])+(a[1]-b[1])*(a[1]-b[1])+(a[2]-b[2])*(a[2]-b[2]));
}
aq::AquariumNavigation box() {
    aq::AquariumNavigation nav;
    nav.valid=true; nav.export_units_per_meter=16.0f;
    nav.layers.push_back({"water",0.0f,4.0f,{{{{-4,-4},{4,-4},{4,4},{-4,4}}}}});
    return nav;
}

void cruisersTurnWithForwardTravel() {
    aq::AquariumMotionPose pose;
    aq::AquariumMotionLimits limits{20,15,0,10,false,true};
    const auto slight=aq::proposeAquariumMotion(pose,
        {{.0174524f,0,.9998477f},20,.6f,aq::AquariumLocomotion::Forward},limits,1.0f/60);
    require(slight.pose.yaw>0 && slight.pose.yaw<.03f,
        "cruiser applied full rudder instead of easing a small heading correction");
    const aq::AquariumMotionIntent reverse{{0,0,-1},20,.6f,aq::AquariumLocomotion::Forward};
    for(int i=0;i<600;++i) {
        const auto step=aq::proposeAquariumCruise(pose,reverse,limits,1.0f/60,
            [](auto,auto){return true;});
        require(distance(pose.position,step.pose.position)>=.0064f,
            "cruiser rotated in place toward a rearward target");
        const float yaw=step.pose.yaw*3.14159265f/180;
        require(step.displacement[0]*std::sin(yaw)+step.displacement[2]*std::cos(yaw)>0,
            "cruiser slid backwards instead of following its heading");
        pose=step.pose;
    }
    const auto blocked=aq::proposeAquariumCruise(pose,reverse,limits,.1f,
        [](auto,auto){return false;});
    require(blocked.pose.yaw==pose.yaw && distance(blocked.pose.position,pose.position)==0,
        "blocked cruiser changed orientation without accepted displacement");
}

void cruiserBankIgnoresSteeringCorrections() {
    aq::AquariumCruiseBank bank;
    float roll=0;
    constexpr float dt=1.0f/60;
    for (int frame=0;frame<600;++frame) {
        roll=bank.update(roll,(frame/15)%2 ? 1.0f : -1.0f,true,dt);
        require(std::abs(roll)<.001f,"brief alternating steering made cruiser rock side to side");
    }
    for (int frame=0;frame<360;++frame) {
        const float next=bank.update(roll,1,true,dt);
        require(std::abs(next-roll)<=2*dt+.00001f,"cruiser bank exceeded gentle roll rate");
        roll=next;
    }
    require(roll<-3 && roll>=-6,"sustained turn lost its restrained bank");
    for (int frame=0;frame<30;++frame) roll=bank.update(roll,-1,true,dt);
    require(roll<0,"brief opposite steering flipped the bank");
    for (int frame=0;frame<1800;++frame) roll=bank.update(roll,0,true,dt);
    require(std::abs(roll)<.001f,"cruiser failed to return level after turning");
}

void generatedTunnelHasNoInvisibleFloorBarrier() {
    namespace geo=pr::aquarium::geometry;
    geo::AquariumBuildRequest request;
    request.tank.id="deep-tunnel-motion";
    request.tank.footprint.width_cells=16;request.tank.footprint.depth_cells=12;
    request.tank.height_steps=12;request.tank.depth_steps=8;
    geo::TunnelDesign tunnel;
    tunnel.id="cross-room";
    for(int x=0;x<=16;++x) tunnel.centreline_cells.push_back({x,6});
    request.tank.tunnels.push_back(tunnel);
    const auto generated=geo::buildAquarium(request);
    require(generated.validation.valid(),"generated deep tunnel fixture invalid");
    aq::AquariumNavigation nav;nav.valid=true;
    for(const auto& source:generated.navigation.layers) {
        aq::SwimVolumeLayer layer;
        layer.y_bottom=source.floor_y/16;layer.y_top=source.ceiling_y/16;
        aq::PolygonWithHoles polygon(1);
        for(auto p:source.area.outer) polygon.front().push_back({p.x/16,p.y/16});
        for(const auto& ring:source.area.holes) {
            polygon.emplace_back();
            for(auto p:ring) polygon.back().push_back({p.x/16,p.y/16});
        }
        layer.polygons.push_back(polygon);nav.layers.push_back(layer);
    }
    aq::AquariumBodyClearance body{.2f,-.2f,.2f};
    require(aq::aquariumBodySegmentNavigable(nav,body,{-3,-2,-3},{-3,3,-3}),
        "tunnel glass floor created an invisible barrier outside its footprint");
    aq::AquariumBodyNavigation route(nav,body);
    require(!route.route({-3,-2,-3},{3,3,3}).waypoints.empty(),
        "generated tunnel prevented routing between deep and upper water");
    const auto& dry=generated.navigation.dry_volumes.front();
    aq::Point3 center{};
    for(auto p:dry.area.outer) {center[0]+=p.x/16;center[2]+=p.y/16;}
    center[0]/=dry.area.outer.size();center[2]/=dry.area.outer.size();center[1]=1;
    require(!aq::containsPoint(nav,center),"tunnel repair flooded the dry corridor");
}

void bodyClearanceCoversEntireSweptVolume() {
    auto nav=box();
    nav.layers.front().y_top=1.0f;
    auto middle=nav.layers.front(); middle.id="dry-middle"; middle.y_bottom=1.0f; middle.y_top=2.0f;
    middle.polygons.front().push_back({{-1,-1},{-1,1},{1,1},{1,-1}});
    nav.layers.push_back(middle);
    auto upper=nav.layers.front(); upper.id="upper"; upper.y_bottom=2.0f; upper.y_top=4.0f;
    nav.layers.push_back(upper);
    aq::AquariumBodyClearance body{0.1f,-1.2f,1.2f};
    require(!aq::containsAquariumBody(nav,body,{0,1.5f,0}),
        "full body clearance allowed a dry tunnel between valid top/bottom samples");
    require(!aq::aquariumBodySegmentNavigable(nav,body,{-3,1.5f,0},{3,1.5f,0}),
        "swept body crossed a dry middle layer despite clear origin endpoints");
    require(aq::containsAquariumBody(nav,body,{3,1.5f,0}),
        "shared layer boundaries incorrectly became solid shelves");
    body.floor=true; body.floor_query_y=0.5f;
    require(aq::aquariumBodySegmentNavigable(nav,body,{-3,-2,0},{3,-2,0}),
        "floor contact navigation lost its authored query-plane convention");
}

void routesAroundObstaclesAndRefinesNarrowPassages() {
    auto nav=box();
    nav.layers.front().polygons.front().push_back({{-1,-2},{-1,2},{1,2},{1,-2}});
    aq::AquariumBodyClearance body{0.15f,-0.2f,0.7f};
    aq::AquariumBodyNavigation graph(nav,body);
    const auto started=std::chrono::steady_clock::now();
    auto query=graph.beginRoute({-3,1,0},{3,1,0});
    std::vector<double> batch_ms;
    while (!query->complete) {
        const auto before=query->result.expanded_nodes;
        const auto begin=std::chrono::steady_clock::now();
        graph.advanceRoute(*query,24);
        batch_ms.push_back(std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-begin).count());
        require(query->result.expanded_nodes-before<=24,
            "incremental aquarium routing exceeded its fixed-step expansion budget");
    }
    const auto route=query->result;
    require(route.status==aq::AquariumRouteStatus::Routed && route.waypoints.size()>1,
        "body route failed to go around an obstacle without line of sight");
    aq::Point3 previous{-3,1,0};
    for (auto waypoint:route.waypoints) {
        require(aq::aquariumBodySegmentNavigable(nav,body,previous,waypoint),
            "smoothed route cut through glass with part of the body");
        previous=waypoint;
    }
    require(graph.route({-3,1,0},{3,1,0}).waypoints==route.waypoints,
        "cached route changed deterministic waypoint ordering");
    std::sort(batch_ms.begin(),batch_ms.end());
    std::cout << "aquarium_motion: routed_fixture_total_ms=" <<
        std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()
        << " batch_p95_ms=" << batch_ms[batch_ms.size()*95/100]
        << " expanded=" << route.expanded_nodes << '\n';

    nav.layers.front().polygons={{{{0,0},{2,0},{2,0.12f},{4,0.12f},
        {4,0},{6,0},{6,2},{4,2},{4,0.42f},{2,0.42f},{2,2},{0,2}}}};
    body.radius=0.06f; body.floor=true; body.floor_query_y=1.0f; body.floor_origin_y=1.0f;
    aq::AquariumBodyNavigation narrow(nav,body);
    const auto refined=narrow.route({1,1,1},{5,1,1});
    require(refined.status==aq::AquariumRouteStatus::Routed,
        "quarter-cell refinement failed a navigable passage missed by the half-cell lattice");
    nav.layers.front().polygons={{{{0,0},{2,0},{2,2},{0,2}}},{{{4,0},{6,0},{6,2},{4,2}}}};
    aq::AquariumBodyNavigation disconnected(nav,body);
    require(disconnected.route({1,1,1},{5,1,1}).waypoints.empty(),
        "route crossed disconnected water components");
}

void layeredRoutesUseWaterAboveTunnels() {
    auto nav=box(); nav.layers.front().y_top=2.0f;
    nav.layers.front().polygons.front().push_back({{-1,-4},{-1,4},{1,4},{1,-4}});
    auto upper=nav.layers.front(); upper.id="over-tunnel"; upper.y_bottom=2.0f; upper.y_top=5.0f;
    upper.polygons.front().resize(1);
    nav.layers.push_back(upper);
    aq::AquariumBodyClearance body{0.15f,-0.25f,0.8f};
    aq::AquariumBodyNavigation graph(nav,body);
    const auto route=graph.route({-3,1,0},{3,1,0});
    require(route.status==aq::AquariumRouteStatus::Routed,
        "layered route failed to ascend above a dry tunnel");
    require(std::any_of(route.waypoints.begin(),route.waypoints.end(),
        [](auto p) { return p[1]>=2.25f; }),
        "layered route did not lift the entire body over the tunnel");
}

void locomotionAndVisualPitchAreIndependent() {
    aq::AquariumMotionPose pose;
    aq::AquariumMotionLimits limits{90,30,0,6,false};
    for (int i=0; i<180; ++i) {
        const auto prior=pose;
        const auto step=aq::proposeAquariumMotion(pose,{{0,1,0.3f},10,0.5f,aq::AquariumLocomotion::Forward},limits,1.0f/60);
        require(pose.position==prior.position && pose.yaw==prior.yaw,
            "motion proposal mutated committed state before containment validation");
        pose=step.pose;
        require(std::abs(pose.pitch)<=6.001f,"visual lean exceeded its authored pitch limit");
    }
    require(pose.position[1]>0.8f,"visual lean still limits the swimmer's vertical travel speed");
    limits.presentation_lean=0;
    const auto hover=aq::proposeAquariumMotion({},{{0,1,0},2,0.5f,aq::AquariumLocomotion::Hover},limits,0.1f);
    require(hover.pose.position[1]>0.04f && hover.pose.pitch==0,
        "upright hovering locomotion cannot ascend independently of model posture");
    aq::AquariumMotionProgress progress;
    bool blocked=false;
    for (int i=0;i<125;++i) blocked=progress.observe({}, {0,2,0},0.5f,1.0f/60);
    require(blocked,"valid zero displacement did not trigger the two-second progress watchdog");
    progress.reset();
    for (int i=0;i<600;++i) require(!progress.observe({i*0.001f,0,0},{3,0,0},0.06f,1.0f/60),
        "slow but genuine displacement was mistaken for a stuck animal");
}

void kingdraEnvelopeExploresVerticallyWithoutSpinning() {
    const auto root=std::filesystem::path(PR_SOURCE_DIR);
    const auto catalog=pr::parseJsonFile((root/"config/gameplay/world3d/aquarium_species.json").string());
    const pr::JsonValue* kingdra=nullptr;
    for (const auto& entry:catalog.get("entries")->asArray())
        if (entry.get("id")->asString()=="0230:00") kingdra=&entry;
    require(kingdra!=nullptr,"Kingdra fixture is missing from the curated catalog");
    require(kingdra->get("behavior")->get("movementProfile")->asString()=="hover",
        "Kingdra must use upright hover locomotion, not large-cruiser");
    auto nav=box();
    nav.layers.front().y_top=10.0f;
    nav.layers.front().polygons={{{{-7,-7},{7,-7},{7,7},{-7,7}}}};
    aq::AquariumPlayerTankSimulationInput tank;
    tank.tank_id="kingdra-motion"; tank.navigation=nav;
    aq::AquariumSwimmerDefinition fish;
    fish.actor.id="kingdra"; fish.actor.species="kingdra"; fish.actor.model_scale=0.17f;
    fish.actor.model_path=kingdra->get("model")->get("path")->asString();
    fish.actor.animation=kingdra->get("presentation")->get("animation")->asString();
    fish.movement.behavior="wander"; fish.movement.forward_only=false;
    fish.movement.speed_meters_per_second=0.55f;
    fish.movement.has_baked_physical_envelope=true;
    const auto& bounds=kingdra->get("physicalEnvelope")->get("boundsModelUnits")->asArray();
    for (int i=0;i<6;++i) fish.movement.baked_physical_envelope[i]=static_cast<float>(bounds[i].asNumber());
    fish.seed=230; tank.swimmers.push_back(fish);
    pr::gameplay::world3d::SceneConfig scene;
    aq::AquariumSimulation simulation(root,scene,nullptr), replay(root,scene,nullptr);
    simulation.replacePlayerTanks({tank}); replay.replacePlayerTanks({tank});
    require(simulation.actors().size()==1,"baked Kingdra did not fit the feasible motion fixture");
    float low=simulation.actors()[0].world_position[1], high=low;
    auto anchor=simulation.actors()[0].world_position;
    float maximum_still=0, still=0;
    const auto begin=std::chrono::steady_clock::now();
    for (int frame=0;frame<60*30;++frame) {
        simulation.update(1.0/60); replay.update(1.0/60);
        const auto& actor=simulation.actors()[0];
        require(actor.world_position==replay.actors()[0].world_position,
            "identical seeds did not replay aquarium motion deterministically");
        require(std::abs(actor.world_pitch_degrees)<0.001f,"Kingdra stopped being upright during vertical travel");
        low=std::min(low,actor.world_position[1]); high=std::max(high,actor.world_position[1]);
        if (distance(anchor,actor.world_position)>0.8f) { anchor=actor.world_position; still=0; }
        else still+=1.0f/60;
        maximum_still=std::max(maximum_still,still);
    }
    require(high-low>16.0f,"Kingdra failed to explore at least one cell of usable depth");
    require(maximum_still<2.0f,"Kingdra stopped translating for two seconds in an unobstructed feasible tank");
    std::cout << "aquarium_motion: kingdra_vertical_cells=" << (high-low)/16.0f
        << " maximum_still_seconds=" << maximum_still << " replay_30s_ms="
        << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count() << '\n';
}

void kyogreToursDepthWithoutOrbitingOnePoint() {
    const auto catalog=pr::parseJsonFile(std::string(PR_SOURCE_DIR)+"/config/gameplay/world3d/aquarium_species.json");
    const pr::JsonValue* entry=nullptr;
    for(const auto& item:catalog.get("entries")->asArray())
        if(item.get("id")->asString()=="0382:00") entry=&item;
    require(entry,"Kyogre catalogue fixture missing");
    auto nav=box();
    // Reported room_1 tank: 24x19 cells, fifteen depth/eight height levels.
    nav.layers.front().y_bottom=-7.25f;nav.layers.front().y_top=7.89f;
    nav.layers.front().polygons={{{{-11.94f,-9.44f},{11.94f,-9.44f},
        {11.94f,9.44f},{-11.94f,9.44f}}}};
    aq::AquariumPlayerTankSimulationInput tank;tank.tank_id="kyogre-tour";tank.navigation=nav;
    aq::AquariumSwimmerDefinition fish;
    fish.actor.id="tank_1:0382:00:0";fish.actor.species="kyogre";fish.actor.model_scale=.17f;
    fish.actor.model_scale*=float(entry->get("presentation")->get("scaleMultiplier")->asNumber());
    fish.actor.model_path=entry->get("model")->get("path")->asString();
    fish.actor.animation=entry->get("presentation")->get("animation")->asString();
    fish.movement.behavior="wander";fish.movement.forward_only=true;
    fish.movement.continuous_cruise=true;fish.movement.habitat_tour=true;
    fish.movement.speed_meters_per_second=.585f;fish.movement.swim_pitch_degrees=10;
    fish.movement.has_baked_physical_envelope=true;
    const auto& bounds=entry->get("physicalEnvelope")->get("boundsModelUnits")->asArray();
    for(int i=0;i<6;++i) fish.movement.baked_physical_envelope[i]=float(bounds[i].asNumber());
    fish.seed=2166136261U;
    for(unsigned char c:fish.actor.id) {fish.seed^=c;fish.seed*=16777619U;}
    tank.swimmers.push_back(fish);
    const auto solo_tank=tank;
    for(const auto& item:catalog.get("entries")->asArray()) if(item.get("id")->asString()=="0223:00") {
        require(item.get("behavior")->get("movementProfile")->asString()=="escort",
            "curated Remoraid must request escort behavior, not independent schooling");
        for(int i=0;i<4;++i) {
            auto escort=fish;
            escort.actor.id="tank_1:0223:00:"+std::to_string(i);
            escort.actor.species="remoraid";
            escort.actor.model_scale=.17f*float(item.get("presentation")->get("scaleMultiplier")->asNumber());
            escort.actor.model_path=item.get("model")->get("path")->asString();
            escort.actor.animation=item.get("presentation")->get("animation")->asString();
            escort.movement.behavior="escort";escort.movement.follow_actor_id=fish.actor.id;
            escort.movement.continuous_cruise=escort.movement.habitat_tour=false;
            const auto& envelope=item.get("physicalEnvelope")->get("boundsModelUnits")->asArray();
            for(int axis=0;axis<6;++axis) escort.movement.baked_physical_envelope[axis]=float(envelope[axis].asNumber());
            escort.seed=223+i;escort.formation_index=i;escort.formation_count=4;
            tank.swimmers.push_back(escort);
        }
    }
    pr::gameplay::world3d::SceneConfig scene;
    aq::AquariumSimulation simulation(PR_SOURCE_DIR,scene,nullptr);simulation.replacePlayerTanks({tank});
    aq::AquariumSimulation solo(PR_SOURCE_DIR,scene,nullptr);solo.replacePlayerTanks({solo_tank});
    require(simulation.actors().size()==5,"Kyogre and four Remoraid could not spawn in the reported tank fixture");
    float low=10000,high=-10000,bank=0,travel=0;
    float still_seconds=0,maximum_still=0;
    auto previous=simulation.actors()[0].world_position;
    float previous_yaw=simulation.actors()[0].world_yaw_degrees;
    for(int frame=0;frame<60*180;++frame) {
        simulation.update(1.0/60);
        solo.update(1.0/60);
        const auto& actor=simulation.actors()[0];
        require(actor.world_position==solo.actors()[0].world_position &&
            actor.world_yaw_degrees==solo.actors()[0].world_yaw_degrees &&
            actor.world_roll_degrees==solo.actors()[0].world_roll_degrees,
            "Kyogre's own Remoraid changed its path, heading or bank");
        low=std::min(low,actor.world_position[1]);high=std::max(high,actor.world_position[1]);
        still_seconds=distance(previous,actor.world_position)<.00001f ? still_seconds+1.0f/60 : 0;
        maximum_still=std::max(maximum_still,still_seconds);
        if(distance(previous,actor.world_position)<.00001f)
            require(std::abs(std::remainder(actor.world_yaw_degrees-previous_yaw,360.0f))<.001f,
                "Kyogre pivoted without translation during its habitat tour");
        previous_yaw=actor.world_yaw_degrees;
        bank=std::max(bank,std::abs(actor.world_roll_degrees));
        travel+=distance(previous,actor.world_position)/16;previous=actor.world_position;
        require(std::abs(actor.world_roll_degrees)<=12.001f,"Kyogre banking exceeded gentle presentation limit");
    }
    std::cout<<"aquarium_motion: kyogre_tour_depth_metres="<<(high-low)/16
        <<" travel_metres="<<travel<<" peak_bank_degrees="<<bank<<'\n';
    require(high-low>5.5f*16 && travel>30 && bank>.5f,
        "Kyogre did not explore depth with forward travel and gentle banking");
    require(maximum_still<2,"Kyogre froze for two seconds in the reported 24x19 tank");
}

void simulatorFollowsRoutedGoalsWithoutBreakingContainment() {
    auto nav=box();
    nav.layers.front().polygons.front().push_back({{-0.5f,-4},{-0.5f,2},{0.5f,2},{0.5f,-4}});
    aq::AquariumPlayerTankSimulationInput tank;
    tank.tank_id="routed-swimmer"; tank.navigation=nav;
    aq::AquariumSwimmerDefinition fish;
    fish.actor.id="cruiser"; fish.actor.species="fixture";
    fish.actor.model_path="baked-envelope-fixture.glbz";
    fish.actor.model_scale=1.0f;
    fish.movement.behavior="wander"; fish.movement.forward_only=true;
    fish.movement.speed_meters_per_second=0.55f;
    fish.movement.swim_pitch_degrees=0.0f;
    fish.movement.has_baked_physical_envelope=true;
    fish.movement.baked_physical_envelope={-1,1,-1,1,-1,1};
    fish.movement.has_starting_position=true;
    fish.movement.starting_position_meters={-3,1,0};
    fish.seed=42; tank.swimmers.push_back(fish);
    pr::gameplay::world3d::SceneConfig scene;
    aq::AquariumSimulation simulation(PR_SOURCE_DIR,scene,nullptr);
    simulation.replacePlayerTanks({tank});
    require(simulation.actors().size()==1,"route fixture could not spawn its swimmer");
    aq::AquariumBodyClearance body{0.1825f,-0.0625f,0.0625f};
    auto previous=fish.movement.starting_position_meters;
    float travel=0;
    bool reached_other_side=false;
    for (int i=0;i<60*90;++i) {
        simulation.update(1.0/60);
        auto p=simulation.actors()[0].world_position;
        for (auto& component:p) component/=16.0f;
        require(aq::containsAquariumBody(nav,body,p),"routed cruiser penetrated glass or a dry tunnel");
        const float step=distance(p,previous);
        require(step<0.02f,"route/recovery teleported the cruiser");
        travel+=step; previous=p;
        reached_other_side|=p[0]>1.0f;
    }
    require(travel>5.0f && reached_other_side && simulation.motionDiagnostics().route_queries>0,
        "runtime failed to follow a body-clear route around a concave obstacle");
}
}

void runAquariumMotionTests() {
    cruiserBankIgnoresSteeringCorrections();
    generatedTunnelHasNoInvisibleFloorBarrier();
    kyogreToursDepthWithoutOrbitingOnePoint();
    cruisersTurnWithForwardTravel();
    bodyClearanceCoversEntireSweptVolume();
    routesAroundObstaclesAndRefinesNarrowPassages();
    layeredRoutesUseWaterAboveTunnels();
    locomotionAndVisualPitchAreIndependent();
    kingdraEnvelopeExploresVerticallyWithoutSpinning();
    simulatorFollowsRoutedGoalsWithoutBreakingContainment();
}
