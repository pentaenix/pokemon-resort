#include "gameplay/world3d/aquarium/AquariumSimulation.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
namespace aq=pr::gameplay::world3d::aquarium;
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
float distance(aq::Point3 a,aq::Point3 b) {
    return std::sqrt((a[0]-b[0])*(a[0]-b[0])+(a[1]-b[1])*(a[1]-b[1])+(a[2]-b[2])*(a[2]-b[2]));
}
aq::AquariumPlayerTankSimulationInput fixture(int count,float radius=5) {
    aq::AquariumPlayerTankSimulationInput tank;
    tank.tank_id="school-cylinder";
    tank.navigation.valid=true; tank.navigation.export_units_per_meter=16;
    aq::PolygonRing ring;
    for (int i=0;i<48;++i) {
        const float angle=i*6.283185307f/48;
        ring.push_back({radius*std::cos(angle),radius*std::sin(angle)});
    }
    tank.navigation.layers.push_back({"water",0,6,{{ring}}});
    for (int i=0;i<count;++i) {
        aq::AquariumSwimmerDefinition fish;
        fish.actor.id="fish-"+std::to_string(i);
        fish.actor.species="wishiwashi"; fish.actor.form="00";
        fish.actor.model_path="baked-school-fixture.glbz"; fish.actor.model_scale=1;
        fish.actor.animation="walk";
        fish.movement.id=fish.actor.id; // Stock entries must not split schools.
        fish.movement.behavior="school"; fish.movement.forward_only=true;
        fish.movement.speed_meters_per_second=0.65f;
        fish.movement.turn_degrees_per_second=100;
        fish.movement.swim_pitch_degrees=18;
        fish.movement.has_baked_physical_envelope=true;
        fish.movement.baked_physical_envelope={-1,1,-0.8f,0.8f,-2,2};
        fish.seed=746+i*17; fish.formation_index=i; fish.formation_count=count;
        tank.swimmers.push_back(fish);
    }
    return tank;
}

void localSteeringIsOrderIndependentAndCatchesStragglers() {
    aq::AquariumSchoolState school;
    school.destination={0,1,20};
    for (int i=0;i<12;++i) school.snapshot.push_back({std::to_string(i),
        {float(i%3)-1,1+float(i%2)*0.3f,float(i/3)*0.6f},{0,0,0.5f},0.15f,0.08f,0.5f});
    auto reverse=school;
    std::reverse(reverse.snapshot.begin(),reverse.snapshot.end());
    aq::steerAquariumSchool(school,1.0f/60);
    aq::steerAquariumSchool(reverse,1.0f/60);
    for (std::size_t i=0;i<school.snapshot.size();++i) {
        const auto j=school.snapshot.size()-1-i;
        require(distance(school.steering[i].direction,reverse.steering[j].direction)<0.00001f &&
            std::abs(school.steering[i].speed-reverse.steering[j].speed)<0.00001f,
            "school snapshot steering depends on population storage order");
        require(school.steering[i].speed<=0.62501f,"school catch-up exceeded 25 percent of cruise");
    }
    school.snapshot.front().position={0,1,-6};
    school.snapshot.back().position={0,1,6};
    aq::steerAquariumSchool(school,1.0f/60);
    require(school.steering.front().speed>school.steering.back().speed,
        "school straggler did not catch up while the front eased back");
}

void cylinderFormsTravelingCloud() {
    auto tank=fixture(12);
    auto reverse=tank;
    std::reverse(reverse.swimmers.begin(),reverse.swimmers.end());
    pr::gameplay::world3d::SceneConfig scene;
    aq::AquariumSimulation simulation(PR_SOURCE_DIR,scene,nullptr), replay(PR_SOURCE_DIR,scene,nullptr);
    simulation.replacePlayerTanks({tank}); replay.replacePlayerTanks({reverse});
    require(simulation.actors().size()==12,"school fixture failed separated spawning");
    require(simulation.actors()[0].animation_time_seconds!=simulation.actors()[1].animation_time_seconds,
        "school spawn synchronized animation phases");
    aq::AquariumBodyClearance body{0.245f,-0.05f,0.05f};
    aq::Point3 previous_center{};
    double travel=0, lateral=0, vertical=0, radius=0, alignment=0;
    int samples=0;
    for (int frame=0;frame<60*45;++frame) {
        simulation.update(1.0/60); replay.update(1.0/60);
        aq::Point3 center{};
        for (std::size_t i=0;i<simulation.actors().size();++i) {
            const auto& actor=simulation.actors()[i];
            require(actor.id==replay.actors()[i].id && actor.world_position==replay.actors()[i].world_position,
                "school runtime changes when stocking input order is reversed");
            auto p=actor.world_position; for (auto& c:p) c/=16;
            require(aq::containsAquariumBody(tank.navigation,body,p),"school body penetrated cylinder glass");
            require(actor.animation=="walk","school changed curated animation");
            for (int axis=0;axis<3;++axis) center[axis]+=p[axis]/12;
        }
        if (frame%30!=0) continue;
        const aq::Point3 delta{center[0]-previous_center[0],center[1]-previous_center[1],center[2]-previous_center[2]};
        const float horizontal=std::hypot(delta[0],delta[2]);
        if (frame>60*5 && horizontal>0.01f) {
            travel+=distance(center,previous_center);
            double side=0,height=0,extent=0,heading_x=0,heading_z=0;
            for (const auto& actor:simulation.actors()) {
                const float x=actor.world_position[0]/16-center[0];
                const float y=actor.world_position[1]/16-center[1];
                const float z=actor.world_position[2]/16-center[2];
                const float perpendicular=(x*delta[2]-z*delta[0])/horizontal;
                side+=perpendicular*perpendicular/12; height+=y*y/12;
                extent+=std::sqrt(x*x+y*y+z*z)/12;
                heading_x+=std::sin(actor.world_yaw_degrees*0.0174532925f)/12;
                heading_z+=std::cos(actor.world_yaw_degrees*0.0174532925f)/12;
            }
            lateral+=std::sqrt(side); vertical+=std::sqrt(height); radius+=extent;
            alignment+=std::hypot(heading_x,heading_z); ++samples;
        }
        previous_center=center;
    }
    std::cout<<"aquarium_school: travel="<<travel<<" lateral_rms="<<lateral/samples
        <<" vertical_rms="<<vertical/samples<<" radius="<<radius/samples<<" alignment="<<alignment/samples<<'\n';
    require(samples>30 && travel>5,"school centroid failed purposeful travel");
    require(lateral/samples>0.2 && vertical/samples>0.08,"school collapsed into a line or flat plane");
    require(radius/samples<2.0,"school dispersed across the tank instead of staying together");
    require(alignment/samples>0.65,"school members do not share a coherent travel heading");
}

void performance() {
    pr::gameplay::world3d::SceneConfig scene;
    for (int count:{32,64,128}) {
        auto tank=fixture(count,16);
        aq::AquariumSimulation simulation(PR_SOURCE_DIR,scene,nullptr);
        simulation.replacePlayerTanks({tank});
        require(simulation.actors().size()==std::size_t(count),"school benchmark lost residents at spawn");
        std::vector<double> timings;
        for (int frame=0;frame<360;++frame) {
            const auto begin=std::chrono::steady_clock::now(); simulation.update(1.0/60);
            const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
            if (frame>=60) timings.push_back(ms);
        }
        std::sort(timings.begin(),timings.end());
        std::cout<<"aquarium_school: residents="<<count<<" ai_step_p95_ms="<<timings[timings.size()*95/100]<<'\n';
    }
}

void convexCacheMatchesOriginalContainment() {
    auto nav=fixture(0).navigation;
    const aq::AquariumBodyClearance body{0.245f,-0.2f,0.4f};
    for (int winding=0;winding<2;++winding) {
        const auto convex=aq::AquariumConvexWater::compile(nav);
        require(!convex.planes.empty(),"convex water cache rejected a cylinder");
        for (int i=0;i<150;++i) {
            const aq::Point3 p{std::sin(float(i)*1.3f)*5.3f,float(i%13)*0.5f,std::cos(float(i)*2.1f)*5.3f};
            require(convex.contains(body,p)==aq::containsAquariumBody(nav,body,p),
                "cached inward planes disagree with original cylinder body containment");
        }
        std::reverse(nav.layers[0].polygons[0][0].begin(),nav.layers[0].polygons[0][0].end());
    }
    nav.layers[0].polygons[0].push_back({{-1,-1},{-1,1},{1,1},{1,-1}});
    require(aq::AquariumConvexWater::compile(nav).planes.empty(),"convex fast path swallowed a tunnel hole");
}

void complexWaterPreservesContainmentAndProgress() {
    pr::gameplay::world3d::SceneConfig scene;
    for (int shape=0;shape<3;++shape) {
        auto tank=fixture(6);
        auto& layer=tank.navigation.layers.front();
        layer.polygons={{{{-4,-4},{4,-4},{4,4},{-4,4}}}};
        if (shape==1) layer.polygons={{{{-4,-4},{4,-4},{4,0},{0,0},{0,4},{-4,4}}}};
        if (shape==2) {
            auto upper=layer; upper.id="over-tunnel"; upper.y_bottom=2;
            layer.y_top=2;
            layer.polygons[0].push_back({{-0.6f,-4},{-0.6f,2},{0.6f,2},{0.6f,-4}});
            tank.navigation.layers.push_back(upper);
        }
        aq::AquariumSimulation simulation(PR_SOURCE_DIR,scene,nullptr);
        simulation.replacePlayerTanks({tank});
        require(simulation.actors().size()==6,"complex school fixture failed separated spawning");
        std::vector<aq::Point3> previous;
        for (auto actor:simulation.actors()) {
            for (auto& p:actor.world_position) p/=16;
            previous.push_back(actor.world_position);
        }
        std::vector<float> travel(6);
        const aq::AquariumBodyClearance body{0.245f,-0.05f,0.05f};
        for (int frame=0;frame<60*20;++frame) {
            simulation.update(1.0/60);
            for (std::size_t i=0;i<previous.size();++i) {
                auto p=simulation.actors()[i].world_position; for (auto& c:p) c/=16;
                require(aq::containsAquariumBody(tank.navigation,body,p),"school escaped concave/layered water");
                const float step=distance(p,previous[i]);
                require(step<0.04f,"school recovery teleported a member");
                travel[i]+=step; previous[i]=p;
            }
        }
        require(*std::min_element(travel.begin(),travel.end())>2,
            "school member made no meaningful progress in feasible complex water");
        std::cout<<"aquarium_school: shape="<<shape<<" minimum_member_travel="
            <<*std::min_element(travel.begin(),travel.end())<<'\n';
    }
}
}

void runAquariumSchoolTests() {
    std::cout << std::unitbuf;
    localSteeringIsOrderIndependentAndCatchesStragglers();
    convexCacheMatchesOriginalContainment();
    cylinderFormsTravelingCloud();
    complexWaterPreservesContainmentAndProgress();
    performance();
}
