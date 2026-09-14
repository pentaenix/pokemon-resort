#include "gameplay/world3d/aquarium/AquariumSchoolSimulation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace pr::gameplay::world3d::aquarium {
namespace {
Point3 difference(Point3 a, Point3 b) { return {a[0]-b[0],a[1]-b[1],a[2]-b[2]}; }
float dot(Point3 a, Point3 b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
float length(Point3 a) { return std::sqrt(dot(a,a)); }
Point3 unit(Point3 a) {
    const float d=length(a);
    return d>0.00001f ? Point3{a[0]/d,a[1]/d,a[2]/d} : Point3{};
}
void add(Point3& a, Point3 b, float weight) {
    for (int i=0;i<3;++i) a[i]+=b[i]*weight;
}
}

std::uint32_t aquariumSchoolHash(const std::string& id) {
    std::uint32_t hash=2166136261U;
    for (unsigned char c:id) { hash^=c; hash*=16777619U; }
    return hash;
}

void steerAquariumSchool(AquariumSchoolState& school, float dt) {
    const auto& fish=school.snapshot;
    school.steering.resize(fish.size());
    school.spatial_order.resize(fish.size());
    std::iota(school.spatial_order.begin(),school.spatial_order.end(),0);
    // Also gives deterministic floating-point reduction order under permutation.
    std::sort(school.spatial_order.begin(),school.spatial_order.end(),[&](auto a,auto b) {
        return fish[a].position[0]==fish[b].position[0] ? fish[a].id<fish[b].id :
            fish[a].position[0]<fish[b].position[0];
    });
    if (fish.empty()) return;
    school.elapsed_seconds+=dt;
    school.center={};
    float body_length=0;
    for (auto index:school.spatial_order) {
        add(school.center,fish[index].position,1.0f/fish.size());
        body_length+=2.0f*fish[index].half_length/fish.size();
    }
    school.comfortable_radius=std::max(0.45f,body_length*1.15f*std::cbrt(float(fish.size())));
    const Point3 wanted=unit(difference(school.destination,school.center));
    const float blend=1.0f-std::exp(-dt/0.6f);
    Point3 heading=school.heading;
    add(heading,difference(wanted,heading),blend);
    school.heading=length(heading)>0.01f ? unit(heading) : wanted;
    float trailing=0;
    for (const auto& member:fish)
        trailing=std::max(trailing,-dot(difference(member.position,school.center),school.heading));
    const float group_pace=std::clamp(1.0f-
        std::max(0.0f,trailing-school.comfortable_radius)*0.1f,0.7f,1.0f);

    for (std::size_t index=0;index<fish.size();++index) {
        const auto& self=fish[index];
        const float range=std::max(1.5f,self.half_length*12.0f);
        // Keep only the closest seven, not the first seven in storage order.
        std::array<std::pair<float,std::size_t>,7> near;
        near.fill({range*range,fish.size()});
        auto start=std::lower_bound(school.spatial_order.begin(),school.spatial_order.end(),
            self.position[0]-range,[&](auto i,float x) { return fish[i].position[0]<x; });
        for (auto it=start;it!=school.spatial_order.end();++it) {
            const auto other=*it;
            if (fish[other].position[0]>self.position[0]+range) break;
            if (other==index) continue;
            const auto delta=difference(fish[other].position,self.position);
            const float d2=dot(delta,delta);
            const auto less=[&](auto a,auto b) {
                return a.first==b.first ? (b.second==fish.size() ||
                    fish[a.second].id<fish[b.second].id) : a.first<b.first;
            };
            const std::pair<float,std::size_t> entry{d2,other};
            if (less(entry,near.back())) {
                near.back()=entry;
                std::sort(near.begin(),near.end(),[&](auto a,auto b) {
                    if (a.second==fish.size()) return false;
                    return less(a,b);
                });
            }
        }
        Point3 alignment{}, center{}, separation{};
        int neighbours=0;
        for (auto [d2,other]:near) {
            if (other==fish.size()) continue;
            const auto& companion=fish[other];
            add(alignment,unit(companion.velocity),1);
            add(center,companion.position,1);
            ++neighbours;
            const auto away=difference(self.position,companion.position);
            const float horizontal=std::max(0.15f,(self.half_length+companion.half_length)*1.5f);
            const float vertical=std::max(0.12f,(self.half_height+companion.half_height)*1.8f);
            const float normalized=std::sqrt((away[0]*away[0]+away[2]*away[2])/
                (horizontal*horizontal)+away[1]*away[1]/(vertical*vertical));
            if (normalized<1.0f && d2>0.000001f)
                add(separation,unit(away),(1.0f-normalized)*1.8f);
        }
        Point3 direction=school.heading;
        if (neighbours>0) {
            add(direction,unit(alignment),0.6f);
            for (auto& component:center) component/=neighbours;
            const auto to_neighbours=difference(center,self.position);
            const float comfort=std::max(0.4f,self.half_length*4.0f);
            add(direction,unit(to_neighbours),
                std::clamp((length(to_neighbours)-comfort)/range,0.0f,0.65f));
        }
        // No attraction inside the group comfort zone, and no permanent slots.
        const auto to_center=difference(school.center,self.position);
        add(direction,unit(to_center),std::clamp(
            (length(to_center)-school.comfortable_radius)/school.comfortable_radius,0.0f,1.2f));
        add(direction,separation,1.0f);
        const auto seed=aquariumSchoolHash(self.id);
        const float phase=float(seed%1009U)*0.006227f;
        direction[1]+=0.12f*std::sin(school.elapsed_seconds*0.23f+phase);
        const float ahead=dot(difference(self.position,school.center),school.heading);
        const float adjustment=std::clamp(-ahead/std::max(0.5f,school.comfortable_radius)*0.2f,-0.18f,0.25f);
        const float individual=0.9f+float(seed%101U)*0.002f;
        school.steering[index]={unit(direction),self.cruise_speed*
            std::clamp(group_pace*individual+adjustment,0.65f,1.25f)};
    }
}
} // namespace pr::gameplay::world3d::aquarium
