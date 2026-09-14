#include "gameplay/world3d/aquarium/AquariumSimulation.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>

namespace pr::gameplay::world3d::aquarium {
namespace {
constexpr float kPi=3.14159265358979323846f;
float length(Point3 p) { return std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]); }
Point3 delta(Point3 a,Point3 b) { return {a[0]-b[0],a[1]-b[1],a[2]-b[2]}; }
std::uint32_t stableHash(const std::string& id) {
    std::uint32_t hash=2166136261U;
    for (unsigned char c:id) { hash^=c; hash*=16777619U; }
    return hash;
}
}

void AquariumSimulation::advanceNavigationQueries() {
    for (std::size_t checked=0; checked<swimmers_.size(); ++checked) {
        navigation_cursor_=(navigation_cursor_+1)%swimmers_.size();
        auto& swimmer=swimmers_[navigation_cursor_];
        if (!swimmer.route_query || swimmer.route_query->complete) continue;
        const auto started=std::chrono::steady_clock::now();
        const auto before=swimmer.route_query->result.expanded_nodes;
        swimmer.body_navigation->advanceRoute(*swimmer.route_query,24);
        motion_diagnostics_.route_expanded_nodes+=swimmer.route_query->result.expanded_nodes-before;
        motion_diagnostics_.route_milliseconds+=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-started).count();
        break; // One bounded batch per fixed step, round-robin across residents.
    }
}

bool AquariumSimulation::beginMotionRecovery(Swimmer& swimmer) {
    ++motion_diagnostics_.recoveries;
    if (swimmer.diagnostic_cooldown<=0.0f) {
        std::clog << "[aquarium.ai] event=recovery actor=" << swimmer.actor.id
            << " reason=" << aquariumMotionBlockageName(swimmer.motion_blockage) << '\n';
        swimmer.diagnostic_cooldown=10.0f;
    }
    swimmer.motion_progress.reset();
    swimmer.motion_direction_initialized=false;
    swimmer.route_waypoints.clear();
    swimmer.route_query.reset();
    swimmer.route_direct_valid=false;
    swimmer.route_visibility_seconds=0.0f;
    swimmer.movement_blocked_seconds=0.0f;
    if(swimmer.continuous_cruise) {
        // A sub-metre escape point is not a reachable turning target for a
        // cruiser. Keep its pose and choose a fresh destination instead.
        swimmer.recovering=false;
        swimmer.cruise_check_seconds=0;
        swimmer.yield_seconds=.2f;
        return chooseTarget(swimmer);
    }
    const std::uint32_t hash=stableHash(swimmer.actor.id);
    const float reach=std::max(0.35f,std::min(1.0f,swimmer.radius));
    const float phase=static_cast<float>(hash%8U)*kPi/4.0f;
    for (float scale : {1.0f,0.5f}) {
        for (int i=0; i<24; ++i) {
            const float angle=phase+i*kPi/4.0f;
            const float vertical=swimmer.floor_navigation ? 0.0f :
                (i<8 ? 0.0f : i<16 ? 0.35f : -0.35f)*scale;
            const Point3 target{swimmer.local_position[0]+std::sin(angle)*reach*scale,
                swimmer.local_position[1]+vertical,
                swimmer.local_position[2]+std::cos(angle)*reach*scale};
            if (!containsBody(swimmer,target) ||
                !segmentNavigable(swimmer,swimmer.local_position,target) ||
                !crowdAllowsMove(swimmer,target)) continue;
            swimmer.recovery_target=target;
            swimmer.recovering=true;
            // Stable priorities prevent identical neighbours making exactly
            // the same escape/yield decision on every fixed simulation step.
            swimmer.yield_seconds=(hash%3U)*0.15f;
            return true;
        }
    }
    swimmer.recovering=false;
    swimmer.yield_seconds=0.5f+(hash%3U)*0.15f;
    chooseTarget(swimmer);
    return false;
}

bool AquariumSimulation::moveTowardTarget(Swimmer& swimmer,Point3 requested,float dt) {
    swimmer.diagnostic_cooldown=std::max(0.0f,swimmer.diagnostic_cooldown-dt);
    swimmer.route_retry_seconds=std::max(0.0f,swimmer.route_retry_seconds-dt);
    if (swimmer.yield_seconds>0.0f) {
        swimmer.yield_seconds-=dt;
        swimmer.motion_blockage=AquariumMotionBlockage::Yielding;
        return true;
    }
    if (swimmer.recovering && length(delta(swimmer.recovery_target,swimmer.local_position))<0.08f) {
        swimmer.recovering=false;
        swimmer.motion_progress.reset();
        swimmer.route_waypoints.clear();
    }
    Point3 target=swimmer.recovering ? swimmer.recovery_target : requested;
    const auto stalled = [&] {
        if (swimmer.motion_progress.observe(swimmer.local_position,target,swimmer.speed,dt)) {
            beginMotionRecovery(swimmer);
            return true;
        }
        return false;
    };
    if (length(delta(target,swimmer.local_position))<0.0001f) return true;

    swimmer.route_visibility_seconds-=dt;
    const bool goal_changed=length(delta(target,swimmer.route_goal))>0.15f;
    if (goal_changed) {
        swimmer.route_waypoints.clear();
        swimmer.route_query.reset();
    }
    if (goal_changed || (!swimmer.route_direct_valid && swimmer.route_visibility_seconds<=0.0f)) {
        swimmer.route_direct_valid=segmentNavigable(swimmer,swimmer.local_position,target);
        swimmer.route_visibility_seconds=0.5f;
        swimmer.route_goal=target;
    }
    if (!swimmer.route_direct_valid) {
        while (!swimmer.route_waypoints.empty() &&
            length(delta(swimmer.route_waypoints.front(),swimmer.local_position))<
                std::max(0.05f,swimmer.speed*dt*2.0f)) {
            swimmer.route_waypoints.erase(swimmer.route_waypoints.begin());
        }
        if (!swimmer.route_waypoints.empty() && !segmentNavigable(
            swimmer,swimmer.local_position,swimmer.route_waypoints.front())) {
            swimmer.route_waypoints.clear();
        }
        if (swimmer.route_waypoints.empty()) {
            if (swimmer.route_retry_seconds>0.0f) return stalled();
            if (!swimmer.body_navigation) {
                const auto body=bodyClearance(swimmer);
                const BodyNavigationKey key{swimmer.tank_id,body.radius,body.lower_extent,
                    body.upper_extent,body.floor,body.floor_query_y,body.floor ? body.floor_origin_y : 0.0f};
                auto& navigation=body_navigation_cache_[key];
                if (!navigation) navigation=std::make_shared<AquariumBodyNavigation>(swimmer.navigation,body);
                swimmer.body_navigation=navigation;
            }
            if (!swimmer.route_query) {
                swimmer.route_query=swimmer.body_navigation->beginRoute(swimmer.local_position,target);
                swimmer.route_goal=target;
                ++motion_diagnostics_.route_queries;
            }
            if (!swimmer.route_query->complete) {
                swimmer.motion_progress.reset();
                return true; // Intentional planning wait is not stuck movement.
            }
            const auto route=std::move(swimmer.route_query->result);
            swimmer.route_query.reset();
            swimmer.route_waypoints=route.waypoints;
            if (swimmer.route_waypoints.empty()) {
                ++motion_diagnostics_.unreachable_routes;
                swimmer.motion_blockage=route.status==AquariumRouteStatus::SearchLimit ?
                    AquariumMotionBlockage::SearchLimit : AquariumMotionBlockage::NoRoute;
                swimmer.route_retry_seconds=1.0f;
                return stalled();
            }
        }
        // At most three extra segments per step: smooth the route without a
        // full-route visibility scan or removing obstacle/body validation.
        for (int look=0; look<3 && swimmer.route_waypoints.size()>1; ++look) {
            if (!segmentNavigable(swimmer,swimmer.local_position,swimmer.route_waypoints[1])) break;
            swimmer.route_waypoints.erase(swimmer.route_waypoints.begin());
        }
        target=swimmer.route_waypoints.front();
    } else {
        swimmer.route_waypoints.clear();
        swimmer.route_query.reset();
    }

    Point3 direction=delta(target,swimmer.local_position);
    const float distance=length(direction);
    if (distance<0.0001f) return true;
    for (float& component:direction) component/=distance;
    if (swimmer.motion_smoothing_seconds>0.0f && swimmer.motion_direction_initialized) {
        const float blend=1.0f-std::exp(-dt/std::max(0.01f,swimmer.motion_smoothing_seconds));
        for (int i=0; i<3; ++i)
            direction[i]=swimmer.motion_direction[i]+(direction[i]-swimmer.motion_direction[i])*blend;
        const float magnitude=length(direction);
        if (magnitude>0.00001f) for (float& component:direction) component/=magnitude;
    }
    swimmer.motion_direction=direction;
    swimmer.motion_direction_initialized=true;
    direction=crowdAdjustedDirection(swimmer,direction);
    const AquariumMotionPose current{swimmer.local_position,
        swimmer.actor.world_yaw_degrees-swimmer.tank.yaw_degrees,
        swimmer.actor.world_pitch_degrees,swimmer.travel_pitch_degrees};
    const AquariumMotionIntent intent{direction,distance,swimmer.speed,
        swimmer.forward_only ? AquariumLocomotion::Forward :
        swimmer.floor_navigation ? AquariumLocomotion::Ground : AquariumLocomotion::Hover};
    AquariumMotionLimits limits{swimmer.turn_speed,swimmer.pitch_turn_speed,
        swimmer.base_pitch_degrees,swimmer.swim_pitch_degrees,swimmer.floor_navigation,
        swimmer.continuous_cruise};
    if(swimmer.continuous_cruise) {
        const float radius=std::max(0.6f,swimmer.radius*1.5f);
        limits.turn_speed=std::min({limits.turn_speed,32.0f,swimmer.speed/radius*180.0f/kPi});
    }
    AquariumMotionStepResult proposal;
    swimmer.cruise_check_seconds-=dt;
    if(swimmer.continuous_cruise && swimmer.cruise_check_seconds<=0) {
        proposal=proposeAquariumCruise(current,intent,limits,dt,[&](Point3 a,Point3 b) {
            return containsBody(swimmer,b) && segmentNavigable(swimmer,a,b);
        });
        swimmer.cruise_direction=proposal.steering_direction;
        swimmer.cruise_speed_scale=proposal.steering_speed_scale;
        swimmer.cruise_check_seconds=.20f+float(stableHash(swimmer.actor.id)%7)*.01f;
    } else {
        auto cached=intent;
        if(swimmer.continuous_cruise) {
            cached.direction=swimmer.cruise_direction;
            cached.speed*=swimmer.cruise_speed_scale;
        }
        proposal=proposeAquariumMotion(current,cached,limits,dt);
    }
    swimmer.motion_blockage=proposal.blockage;
    bool accepted=true;
    if (!containsBody(swimmer,proposal.pose.position) ||
        !segmentNavigable(swimmer,current.position,proposal.pose.position)) {
        swimmer.motion_blockage=AquariumMotionBlockage::Boundary;
        swimmer.route_direct_valid=false;
        swimmer.route_visibility_seconds=0.0f;
        accepted=false;
        swimmer.cruise_check_seconds=0;
    } else if (!crowdAllowsMove(swimmer,proposal.pose.position)) {
        swimmer.motion_blockage=AquariumMotionBlockage::Crowd;
        accepted=false;
    }
    if (accepted) {
        if(swimmer.habitat_tour) {
            const float yaw_rate=std::remainder(proposal.pose.yaw-current.yaw,360.0f)/std::max(dt,.001f);
            float bank=swimmer.cruise_bank.update(swimmer.actor.world_roll_degrees,
                yaw_rate/std::max(1.0f,limits.turn_speed),length(proposal.displacement)>.00001f,dt);
            const auto bank_fits = [&](float angle) {
                auto body=bodyClearance(swimmer);
                const float sine=std::sin(std::abs(angle)*kPi/180.0f);
                const float extra=swimmer.radius*sine;
                body.radius+=std::max(std::abs(body.lower_extent),std::abs(body.upper_extent))*sine;
                body.lower_extent-=extra;body.upper_extent+=extra;
                return containsAquariumBody(swimmer.navigation,body,proposal.pose.position);
            };
            // Fit the available lean instead of snapping straight to zero near glass.
            // Containment takes priority over presentation easing.
            if (!bank_fits(bank)) {
                float low=0,high=1;
                for (int i=0;i<8;++i) {
                    const float middle=(low+high)*.5f;
                    if (bank_fits(bank*middle)) low=middle; else high=middle;
                }
                bank*=low;
            }
            swimmer.actor.world_roll_degrees=bank;
        }
        swimmer.local_position=proposal.pose.position;
        swimmer.actor.world_yaw_degrees=proposal.pose.yaw+swimmer.tank.yaw_degrees;
        swimmer.actor.world_pitch_degrees=proposal.pose.pitch;
        swimmer.travel_pitch_degrees=proposal.pose.travel_pitch;
    } else if (swimmer.recovering && !swimmer.continuous_cruise) {
        // The yaw-invariant clearance envelope already contains a bounded
        // turn in place. Only recovery may turn after a rejected translation.
        swimmer.actor.world_yaw_degrees=proposal.pose.yaw+swimmer.tank.yaw_degrees;
        swimmer.actor.world_pitch_degrees=proposal.pose.pitch;
        swimmer.travel_pitch_degrees=proposal.pose.travel_pitch;
    }
    return stalled() || accepted;
}

} // namespace pr::gameplay::world3d::aquarium
