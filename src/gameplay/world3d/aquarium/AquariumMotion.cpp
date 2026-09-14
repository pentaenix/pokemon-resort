#include "gameplay/world3d/aquarium/AquariumMotion.hpp"

#include <algorithm>
#include <cmath>

namespace pr::gameplay::world3d::aquarium {
namespace {
constexpr float kRadians = 3.14159265358979323846f/180.0f;
float wrap(float a) { return std::remainder(a,360.0f); }
float approach(float a,float b,float step) { return wrap(a+std::clamp(wrap(b-a),-step,step)); }
float length(Point3 a) { return std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]); }
float distance(Point3 a,Point3 b) { return length({a[0]-b[0],a[1]-b[1],a[2]-b[2]}); }
}

AquariumMotionStepResult proposeAquariumMotion(const AquariumMotionPose& pose,
    const AquariumMotionIntent& intent, const AquariumMotionLimits& limits, float dt) {
    AquariumMotionStepResult out;
    out.pose=pose;
    Point3 direction=intent.direction;
    if (limits.floor) direction[1]=0.0f;
    const float magnitude=length(direction);
    if (dt<=0.0f || magnitude<0.00001f || intent.distance<=0.00001f || intent.speed<=0.0f) return out;
    for (float& component:direction) component/=magnitude;
    const float horizontal=std::hypot(direction[0],direction[2]);
    const float desired_yaw=horizontal>0.00001f ? std::atan2(direction[0],direction[2])/kRadians : pose.yaw;
    // Ease into a cruising heading instead of applying full rudder to tiny
    // target changes. The arc predictor uses this same motion law.
    const float yaw_step=limits.continuous_cruise
        ? std::min(limits.turn_speed*dt, std::abs(wrap(desired_yaw-pose.yaw))*(1-std::exp(-dt/0.8f)))
        : limits.turn_speed*dt;
    out.pose.yaw=approach(pose.yaw,desired_yaw,yaw_step);
    const float path_pitch=std::atan2(direction[1],horizontal)/kRadians;
    out.pose.pitch=approach(pose.pitch,limits.base_pitch-
        std::clamp(path_pitch,-limits.presentation_lean,limits.presentation_lean),limits.pitch_speed*dt);
    float step=limits.continuous_cruise ? intent.speed*dt : std::min(intent.distance,intent.speed*dt);
    if (intent.locomotion==AquariumLocomotion::Forward) {
        // A small visual lean must not impose a tiny climb rate on the animal.
        const float climb_limit=limits.continuous_cruise ? 20.0f : 65.0f;
        out.pose.travel_pitch=approach(pose.travel_pitch,
            limits.floor ? 0.0f : std::clamp(path_pitch,-climb_limit,climb_limit),
            (limits.continuous_cruise ? std::min(15.0f,limits.pitch_speed) :
                std::max(30.0f,limits.pitch_speed))*dt);
        const float yaw=out.pose.yaw*kRadians, pitch=out.pose.travel_pitch*kRadians;
        const Point3 forward{std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
        const float alignment=std::max(0.0f,
            forward[0]*direction[0]+forward[1]*direction[1]+forward[2]*direction[2]);
        step*=limits.continuous_cruise ? std::max(0.65f,alignment) : alignment;
        direction=forward;
        if (step<0.000001f) out.blockage=AquariumMotionBlockage::Turning;
    }
    for (int axis=0; axis<3; ++axis) {
        out.displacement[axis]=direction[axis]*step;
        out.velocity[axis]=out.displacement[axis]/dt;
        out.pose.position[axis]+=out.displacement[axis];
    }
    return out;
}

AquariumMotionStepResult proposeAquariumCruise(const AquariumMotionPose& pose,
    const AquariumMotionIntent& intent,const AquariumMotionLimits& limits,float dt,
    const std::function<bool(Point3,Point3)>& clear) {
    // Preview a complete reversing arc, not merely the next frame, so large bodies steer
    // before reaching glass. Only publish a pose with accepted forward travel.
    const float horizon=180.0f/std::max(1.0f,limits.turn_speed);
    for(float speed_scale : {1.0f,0.65f,0.4f})
    for(float offset : {0.0f,45.0f,-45.0f,90.0f,-90.0f,135.0f,-135.0f,180.0f}) {
        auto candidate=intent;
        candidate.speed*=speed_scale;
        if(offset!=0) {
            const float yaw=(pose.yaw+offset)*kRadians;
            candidate.direction={std::sin(yaw),std::clamp(intent.direction[1],-.36f,.36f),std::cos(yaw)};
        }
        auto predicted=pose;
        bool safe=true;
        const int samples=std::clamp(int(std::ceil(horizon/.2f)),24,240);
        const Point3 goal{pose.position[0]+intent.direction[0]*intent.distance,
            pose.position[1]+intent.direction[1]*intent.distance,
            pose.position[2]+intent.direction[2]*intent.distance};
        for(int i=0;i<samples;++i) {
            auto future=candidate;
            if(offset==0) future.direction={goal[0]-predicted.position[0],
                goal[1]-predicted.position[1],goal[2]-predicted.position[2]};
            else future.direction[1]=std::clamp(goal[1]-predicted.position[1],-.36f,.36f);
            const auto step=proposeAquariumMotion(predicted,future,limits,horizon/samples);
            if(!clear(predicted.position,step.pose.position)) {safe=false;break;}
            predicted=step.pose;
        }
        if(!safe) continue;
        auto step=proposeAquariumMotion(pose,candidate,limits,dt);
        step.steering_direction=candidate.direction;
        step.steering_speed_scale=speed_scale;
        if(clear(pose.position,step.pose.position)) return step;
    }
    AquariumMotionStepResult stopped;
    stopped.pose=pose;
    stopped.blockage=AquariumMotionBlockage::Boundary;
    return stopped;
}

float AquariumCruiseBank::update(float roll, float turn_fraction, bool moving, float dt) {
    if (dt <= 0) return roll;
    const int sign = !moving || std::abs(turn_fraction) < .3f ? 0 : (turn_fraction > 0 ? 1 : -1);
    sustained_seconds = sign != 0 && sign == turn_sign ? sustained_seconds + dt : 0.0f;
    turn_sign = sign;
    float target = sustained_seconds >= 1.0f ? -6.0f * std::clamp(turn_fraction, -1.0f, 1.0f) : 0.0f;
    // Unwind the old lean before banking into a sustained opposite turn.
    if (target * roll < 0 && std::abs(roll) > .1f) target = 0;
    const float eased = (target - roll) * (1.0f - std::exp(-dt / 2.5f));
    return roll + std::clamp(eased, -2.0f * dt, 2.0f * dt);
}

bool AquariumMotionProgress::observe(Point3 position,Point3 destination,float speed,float dt) {
    if (!initialized) {
        anchor=position; goal=destination; seconds=0.0f; initialized=true;
    }
    const float threshold=std::clamp(speed*0.25f,0.01f,0.12f);
    if (distance(position,anchor)>=threshold) { anchor=position; seconds=0.0f; }
    else seconds+=dt;
    return seconds>=2.0f;
}

const char* aquariumMotionBlockageName(AquariumMotionBlockage reason) {
    switch (reason) {
    case AquariumMotionBlockage::None: return "none";
    case AquariumMotionBlockage::Turning: return "turning";
    case AquariumMotionBlockage::Boundary: return "boundary";
    case AquariumMotionBlockage::Crowd: return "crowd";
    case AquariumMotionBlockage::NoRoute: return "no_route";
    case AquariumMotionBlockage::SearchLimit: return "search_limit";
    case AquariumMotionBlockage::Yielding: return "yielding";
    }
    return "unknown";
}
} // namespace pr::gameplay::world3d::aquarium
