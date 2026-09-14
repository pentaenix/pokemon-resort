#include "gameplay/world3d/aquarium/AquariumSimulation.hpp"

#include <algorithm>
#include <cmath>

namespace pr::gameplay::world3d::aquarium {
namespace {
float distance(Point3 a, Point3 b) {
    return std::sqrt((a[0]-b[0])*(a[0]-b[0])+(a[1]-b[1])*(a[1]-b[1])+(a[2]-b[2])*(a[2]-b[2]));
}
Point3 ahead(Point3 origin, Point3 direction, float reach) {
    for (int axis=0;axis<3;++axis) origin[axis]+=direction[axis]*reach;
    return origin;
}
}

void AquariumSimulation::prepareSchools(float dt) {
    for (auto& [id, school]:schools_) { school.members.clear(); school.snapshot.clear(); }
    for (std::size_t i=0;i<swimmers_.size();++i) {
        auto& fish=swimmers_[i];
        fish.school_grouped=false;
        const bool orphan=fish.behavior==Swimmer::Behavior::Escort &&
            std::none_of(swimmers_.begin(),swimmers_.end(),[&](const auto& other) {
                return other.actor.id==fish.follow_actor_id && other.tank_id==fish.tank_id;
            });
        if (orphan) { fish.behavior=Swimmer::Behavior::School; chooseTarget(fish); }
        if (fish.behavior==Swimmer::Behavior::School) schools_[fish.school_id].members.push_back(i);
    }
    for (auto& [id,school]:schools_) {
        if (school.members.size()<2) continue;
        std::sort(school.members.begin(),school.members.end(),[&](auto a,auto b) {
            return swimmers_[a].actor.id<swimmers_[b].actor.id;
        });
        Point3 center{};
        for (auto i:school.members) {
            const auto& fish=swimmers_[i];
            school.snapshot.push_back({fish.actor.id,fish.local_position,fish.snapshot_velocity,
                std::max(fish.body_half_length,fish.body_half_width),fish.body_half_height,fish.speed});
            for (int axis=0;axis<3;++axis) center[axis]+=fish.local_position[axis]/school.members.size();
        }
        school.destination_seconds-=dt;
        if (!school.has_destination || school.destination_seconds<=0 ||
            distance(center,school.destination)<std::max(0.65f,school.comfortable_radius)) {
            // Only borrows a member's body-clear sampling/RNG, not its motion:
            // no permanent leader, formation slots or phase-offset orbit.
            auto& sampler=swimmers_[school.members.front()];
            for (int attempt=0;attempt<12;++attempt) {
                if (!chooseTarget(sampler)) break;
                if (distance(center,sampler.local_target)<school.comfortable_radius*1.5f) continue;
                school.destination=sampler.local_target;
                school.has_destination=true;
                school.destination_seconds=std::clamp(distance(center,school.destination)/
                    std::max(0.05f,sampler.speed)*2.0f,12.0f,40.0f);
                break;
            }
            if (!school.has_destination) school.destination=center;
        }
        steerAquariumSchool(school,dt);
        for (std::size_t member=0;member<school.members.size();++member) {
            auto& fish=swimmers_[school.members[member]];
            fish.school_grouped=true;
            fish.school_speed=school.steering[member].speed;
            fish.school_steering_seconds-=dt;
            fish.school_visibility_seconds-=dt;
            if (fish.school_visibility_seconds<=0 || school.destination!=fish.school_visibility_goal) {
                fish.school_steering_seconds=0;
                fish.school_goal_visible=segmentNavigable(fish,fish.local_position,school.destination);
                fish.school_visibility_goal=school.destination;
                fish.school_visibility_seconds=0.35f+float(aquariumSchoolHash(fish.actor.id)%17U)*0.01f;
            }
            if (fish.school_steering_seconds>0) continue;
            fish.school_steering_seconds=0.12f+float(aquariumSchoolHash(fish.actor.id)%7U)*0.01f;
            const float reach=std::max(fish.radius*2.0f,fish.school_speed*1.2f);
            const auto target=ahead(fish.local_position,school.steering[member].direction,reach);
            // Social steering in open water; a stable destination for the
            // existing body-aware router when a wall/tunnel separates members.
            if (fish.school_goal_visible &&
                containsBody(fish,target) && segmentNavigable(fish,fish.local_position,target)) {
                fish.school_target=target;
            } else {
                fish.school_target=school.destination;
            }
        }
    }
}

void AquariumSimulation::updateSchool(Swimmer& fish, float dt) {
    if (!fish.school_grouped) {
        if (distance(fish.local_position,fish.local_target)<std::max(0.05f,fish.speed*dt*2) ||
            !containsBody(fish,fish.local_target)) chooseTarget(fish);
        if (!moveTowardTarget(fish,fish.local_target,dt) && shouldRetargetBlockedMovement(fish,dt))
            chooseTarget(fish);
        return;
    }
    const float cruise=fish.speed;
    fish.speed=fish.school_speed;
    moveTowardTarget(fish,fish.school_target,dt);
    fish.speed=cruise;
}
} // namespace pr::gameplay::world3d::aquarium
