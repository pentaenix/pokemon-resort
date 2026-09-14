#pragma once
#include "gameplay/world3d/aquarium/AquariumSimulation.hpp"
#include "gameplay/attend/rendering/AttendPokemonModel.hpp"
#include <algorithm>
#include <cmath>

namespace pr::gameplay::world3d::aquarium::rendering {
// Decorations are ordinary glTF props in the overworld's reflected camera
// basis (right = forward x up), not Attend's mtxLookAt basis. Their exterior
// winding therefore needs the opposite cull direction. Keep curated resident
// presentation unchanged; double-sided materials bypass culling altogether.
inline bool aquariumUsesClockwiseBackfaceCull(const AquariumPokemonActor& actor) {
    return actor.id.rfind("decoration:",0)==0;
}
inline void aquariumPokemonPlacementMatrix(const AquariumPokemonActor& actor, float (&matrix)[16]) {
    std::fill(std::begin(matrix), std::end(matrix), 0.0f);
    constexpr float kPi = 3.14159265358979323846f;
    const float yaw = actor.world_yaw_degrees * kPi / 180.0f;
    const float pitch = actor.world_pitch_degrees * kPi / 180.0f;
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    matrix[0] = cy * actor.model_scale;
    matrix[2] = -sy * actor.model_scale;
    matrix[4] = sy * sp * actor.model_scale;
    matrix[5] = cp * actor.model_scale;
    matrix[6] = cy * sp * actor.model_scale;
    matrix[8] = sy * cp * actor.model_scale;
    matrix[9] = -sp * actor.model_scale;
    matrix[10] = cy * cp * actor.model_scale;
    const float roll=actor.world_roll_degrees*kPi/180.0f;
    const float cr=std::cos(roll),sr=std::sin(roll);
    for(int row=0;row<3;++row) {
        const float x=matrix[row],y=matrix[4+row];
        matrix[row]=x*cr+y*sr;
        matrix[4+row]=y*cr-x*sr;
    }
    matrix[12] = actor.world_position[0];
    matrix[13] = actor.world_position[1];
    matrix[14] = actor.world_position[2];
    matrix[15] = 1.0f;
}

inline std::array<float,4> aquariumPulseBounds(const pr::gameplay::attend::rendering::AttendPokemonModel& source) {
    std::array<float,4> bounds{0,0,0,1};
        std::array<float,3> low{1e20f,1e20f,1e20f},high{-1e20f,-1e20f,-1e20f};
        for(const auto& primitive:source.primitives) {
            if(primitive.material<0 || primitive.material>=int(source.materials.size())) continue;
            const auto& name=source.materials[primitive.material].name;
            if(name!="BodyANeolant_Inc" && name!="BodyBNeolant_Inc") continue;
            for(const auto& v:primitive.vertices) {
                const float p[]{v.x,v.y,v.z};
                for(int axis=0;axis<3;++axis) {low[axis]=std::min(low[axis],p[axis]);high[axis]=std::max(high[axis],p[axis]);}
            }
        }
        if(low[0]<=high[0]) {
            float radius_squared=0;
            for(int axis=0;axis<3;++axis) {
                bounds[axis]=(low[axis]+high[axis])*.5f;
                radius_squared+=(high[axis]-low[axis])*(high[axis]-low[axis])*.25f;
            }
            bounds[3]=std::max(1.0f,std::sqrt(radius_squared));
        }

    return bounds;
}
}
