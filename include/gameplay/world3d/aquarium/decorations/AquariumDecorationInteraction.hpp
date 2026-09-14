#pragma once
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"
#include <SDL.h>
#include <algorithm>
#include <cmath>

namespace pr::gameplay::world3d::aquarium::decorations {
inline bool decorationDragStarted(SDL_Point origin,SDL_Point current) {
    return std::hypot(float(current.x-origin.x),float(current.y-origin.y))>8.0f;
}
// InputRouter calls the raw event hook BEFORE its semantic pointer callbacks.
inline bool usesDecorationPointerCallback(const SDL_Event& event) {
    return event.type==SDL_MOUSEMOTION ||
        ((event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP) && event.button.button==SDL_BUTTON_LEFT);
}
inline camera::Gen4FollowCamera framedDecorationCamera(const camera::Gen4FollowCamera& original,
    camera::Vec3 center,camera::Vec3 half_size,int width,int height,float zoom) {
    const auto pose=original.pose();
    const float tan_y=std::tan(pose.preset.fov_y_deg*3.14159265f/360);
    const float tan_x=tan_y*width/std::max(1,height);
    const float usable=std::max(.3f,float(height-260)/std::max(1,height));
    float distance=64;
    for(float x:{-half_size.x,half_size.x})for(float y:{-half_size.y,half_size.y})for(float z:{-half_size.z,half_size.z}){
        const float forward=x*pose.forward.x+y*pose.forward.y+z*pose.forward.z;
        const float right=x*pose.right.x+y*pose.right.y+z*pose.right.z;
        const float up=x*pose.up.x+y*pose.up.y+z*pose.up.z;
        distance=std::max({distance,std::abs(right)/(tan_x*.85f)-forward,
            std::abs(up)/(tan_y*usable*.85f)-forward});
    }
    distance*=zoom;
    const float offset=distance*tan_y*(1-usable);
    auto result=original;
    result.setManualPose({center.x-pose.forward.x*distance-pose.up.x*offset,
        center.y-pose.forward.y*distance-pose.up.y*offset,
        center.z-pose.forward.z*distance-pose.up.z*offset},
        std::atan2(pose.forward.x,pose.forward.z)*180/3.14159265f,
        std::asin(std::clamp(pose.forward.y,-1.0f,1.0f))*180/3.14159265f);
    return result;
}
}
