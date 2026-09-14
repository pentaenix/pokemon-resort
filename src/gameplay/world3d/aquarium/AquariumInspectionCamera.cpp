#include "gameplay/world3d/aquarium/AquariumInspectionCamera.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pr::gameplay::world3d::aquarium {
namespace {

constexpr float kPi = 3.14159265358979323846f;

camera::Vec3 facingVector(FacingDirection facing) {
    switch (facing) {
        case FacingDirection::North: return {0.0f, 0.0f, -1.0f};
        case FacingDirection::East: return {1.0f, 0.0f, 0.0f};
        case FacingDirection::South: return {0.0f, 0.0f, 1.0f};
        case FacingDirection::West: return {-1.0f, 0.0f, 0.0f};
    }
    return {0.0f, 0.0f, -1.0f};
}

camera::Vec3 add(camera::Vec3 a, camera::Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

camera::Vec3 subtract(camera::Vec3 a, camera::Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

camera::Vec3 scale(camera::Vec3 value, float amount) {
    return {value.x * amount, value.y * amount, value.z * amount};
}

camera::Vec3 facingRight(camera::Vec3 forward) {
    return {-forward.z, 0.0f, forward.x};
}

float length(camera::Vec3 value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

camera::Vec3 moveToward(camera::Vec3 current, camera::Vec3 target, float maximum_delta) {
    const camera::Vec3 delta = subtract(target, current);
    const float distance = length(delta);
    if (distance <= maximum_delta || distance <= 0.0001f) return target;
    return add(current, scale(delta, maximum_delta / distance));
}

camera::Vec3 rotateLocal(float x, float y, float z, float yaw_degrees) {
    const float yaw = yaw_degrees * kPi / 180.0f;
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    return {x * c + z * s, y, -x * s + z * c};
}

camera::Vec3 tankLookAtTarget(const AquariumTankRuntime& tank) {
    const auto& tuning = tank.inspection_camera;
    const camera::Vec3 look_offset = rotateLocal(
        tuning.look_at_x_meters * tank.units_per_meter_world,
        tuning.look_at_y_meters * tank.units_per_meter_world,
        tuning.look_at_z_meters * tank.units_per_meter_world,
        tank.yaw_degrees);
    camera::Vec3 target = add(
        camera::Vec3{tank.world_center[0], tank.world_center[1], tank.world_center[2]},
        look_offset);
    const float look_margin = std::max(0.05f, tank.units_per_meter_world * 0.02f);
    const float lower = tank.water_bottom_world + look_margin;
    const float upper = tank.water_top_world - look_margin;
    target.y = lower <= upper
        ? std::clamp(target.y, lower, upper)
        : (tank.water_bottom_world + tank.water_top_world) * 0.5f;
    return target;
}

bool rayHitsTank(
    camera::Vec3 origin,
    camera::Vec3 direction,
    const AquariumTankRuntime& tank,
    float reach,
    float* distance_out) {
    const camera::Vec3 local_origin = rotateLocal(
        origin.x - tank.world_center[0], 0.0f,
        origin.z - tank.world_center[2], -tank.yaw_degrees);
    const camera::Vec3 local_direction = rotateLocal(
        direction.x, 0.0f, direction.z, -tank.yaw_degrees);
    float near_t = 0.0f;
    float far_t = reach;
    const auto clip_axis = [&](float position, float direction_axis, float half_extent) {
        if (std::abs(direction_axis) < 0.0001f) return std::abs(position) <= half_extent;
        float a = (-half_extent - position) / direction_axis;
        float b = (half_extent - position) / direction_axis;
        if (a > b) std::swap(a, b);
        near_t = std::max(near_t, a);
        far_t = std::min(far_t, b);
        return near_t <= far_t;
    };
    if (!clip_axis(local_origin.x, local_direction.x, tank.half_width_world) ||
        !clip_axis(local_origin.z, local_direction.z, tank.half_depth_world) ||
        far_t < 0.0f || near_t > reach) return false;
    if (distance_out) *distance_out = std::max(0.0f, near_t);
    return true;
}

void applyManualLookAt(
    camera::Gen4FollowCamera& camera,
    camera::Vec3 position,
    camera::Vec3 target) {
    const camera::Vec3 delta = subtract(target, position);
    const float horizontal = std::sqrt(delta.x * delta.x + delta.z * delta.z);
    const float yaw = std::atan2(delta.x, delta.z) * 180.0f / kPi;
    const float pitch = std::atan2(delta.y, std::max(0.0001f, horizontal)) * 180.0f / kPi;
    camera.setManualPose(position, yaw, pitch);
}

} // namespace

AquariumInspectionCamera::AquariumInspectionCamera(std::vector<AquariumTankRuntime> tanks)
    : tanks_(std::move(tanks)) {}

float AquariumInspectionCamera::wallClipRadiusWorld(float tile_size) const {
    if (stage_ != Stage::Focused || active_tank_index_ >= tanks_.size()) return 0.0f;
    return std::max(0.0f,
        tanks_[active_tank_index_].inspection_camera.focused_wall_clip_radius_tiles) *
        std::max(0.0f, tile_size);
}

bool AquariumInspectionCamera::tryBegin(
    camera::Vec3 player_position,
    FacingDirection facing,
    float tile_size,
    const camera::Gen4FollowCamera& current_camera) {
    const camera::Vec3 forward = facingVector(facing);
    if (active()) return false;
    const AquariumTankRuntime* selected = nullptr;
    std::size_t selected_index = 0;
    float nearest = std::numeric_limits<float>::max();
    for (std::size_t index = 0; index < tanks_.size(); ++index) {
        const AquariumTankRuntime& tank = tanks_[index];
        if (!tank.inspection_camera.enabled) continue;
        float distance = 0.0f;
        if (rayHitsTank(
                player_position, forward, tank,
                tank.inspection_camera.interaction_reach_tiles * tile_size, &distance) &&
            distance < nearest) {
            nearest = distance;
            selected = &tank;
            selected_index = index;
        }
    }
    if (!selected) return false;

    const auto& tuning = selected->inspection_camera;
    const auto pose = current_camera.pose();
    const camera::Vec3 player_to_camera = subtract(pose.position, player_position);
    const float follow_horizontal_distance = std::sqrt(
        player_to_camera.x * player_to_camera.x + player_to_camera.z * player_to_camera.z);
    if (tuning.has_framed_inspection_view) {
        const camera::Vec3 local_forward = rotateLocal(
            forward.x, forward.y, forward.z, -selected->yaw_degrees);
        const bool front_face = std::abs(local_forward.z) >= std::abs(local_forward.x);
        desired_position_ = add(
            add(
                player_position,
                scale(forward, -tuning.inspection_behind_player_tiles * tile_size)),
            scale(facingRight(forward), tuning.side_tiles * tile_size));
        desired_position_.y = selected->floor_y_world +
            (front_face
                ? tuning.inspection_front_height_tiles
                : tuning.inspection_side_height_tiles) * tile_size;
    } else {
        // Compatibility for existing authored tanks: rotate the known-good
        // follow-camera distance behind the player's approach direction.
        const camera::Vec3 approach_base = add(
            player_position,
            scale(forward, -follow_horizontal_distance));
        desired_position_ = add(
            add(approach_base, scale(facingRight(forward), tuning.side_tiles * tile_size)),
            scale(forward, tuning.closer_tiles * tile_size));
        desired_position_.y = pose.position.y - tuning.lower_tiles * tile_size;
    }
    desired_position_.y = std::max(
        desired_position_.y,
        selected->floor_y_world + tile_size);
    // Look through the glass directly ahead of the player, not diagonally
    // toward the tank center when inspecting near either end of a long tank.
    desired_look_at_ = add(player_position,scale(forward,tile_size*3));
    desired_look_at_.y=player_position.y+tile_size;
    // A rotating smooth transition reads naturally from the conventional
    // north-facing camera. Other faces snap so the camera never sweeps through
    // walls or the floor while rotating around the room.
    approach_facing_ = facing;
    smooth_ = facing == FacingDirection::North ? tuning.smooth : 0.0f;
    return_smooth_ = facing == FacingDirection::North ? tuning.return_smooth : 0.0f;
    original_near_clip_ = pose.preset.near_clip;
    original_fov_=current_fov_=desired_fov_=pose.preset.fov_y_deg;
    return_target_ = player_position;
    return_position_ = pose.position;
    active_tank_index_ = selected_index;
    active_placement_id_ = selected->placement_id;
    stage_ = Stage::Inspecting;

    current_position_ = smooth_ <= 0.0f ? desired_position_ : pose.position;
    current_position_.y = std::max(current_position_.y, selected->floor_y_world + tile_size);
    current_look_at_ = smooth_ <= 0.0f
        ? desired_look_at_
        : add(pose.position, scale(pose.forward, std::max(1.0f, length(subtract(desired_look_at_, pose.position)))));
    return true;
}

bool AquariumInspectionCamera::enterFocused(
    float tile_size,
    camera::Gen4FollowCamera& camera,float aspect) {
    if (stage_ != Stage::Inspecting || active_tank_index_ >= tanks_.size()) return false;

    const AquariumTankRuntime& tank = tanks_[active_tank_index_];
    const AquariumInspectionCameraConfig& tuning = tank.inspection_camera;
    const camera::Vec3 forward = facingVector(approach_facing_);
    const float bottom=tank.water_bottom_world,top=tank.water_top_world;
    desired_look_at_={tank.world_center[0],(bottom+top)*.5f,tank.world_center[2]};
    const camera::Vec3 view_forward{forward.x*.94f,-.342f,forward.z*.94f};
    const auto right=facingRight(forward);
    const camera::Vec3 up{forward.x*.342f,.94f,forward.z*.342f};
    const float tan_y=std::tan(camera.pose().preset.fov_y_deg*kPi/360)*.82f;
    float distance=tile_size*5;
    for(float x:{-tank.half_width_world,tank.half_width_world})
        for(float z:{-tank.half_depth_world,tank.half_depth_world})
            for(float y:{bottom,top}){
                auto p=rotateLocal(x,y-desired_look_at_.y,z,tank.yaw_degrees);
                const float depth=p.x*view_forward.x+p.y*view_forward.y+p.z*view_forward.z;
                const float horizontal=p.x*right.x+p.z*right.z;
                const float vertical=p.x*up.x+p.y*up.y+p.z*up.z;
                distance=std::max({distance,std::abs(horizontal)/(tan_y*std::max(.5f,aspect))-depth,
                    std::abs(vertical)/tan_y-depth});
            }
    // Include the player and retain a clear above-floor camera position.
    distance=std::max(distance,(tank.floor_y_world+tile_size*2-desired_look_at_.y)/.342f);
    // Even a small tank must expand the view on the second accept, never move
    // closer than the first over-the-shoulder composition.
    distance=std::max(distance,length(subtract(desired_position_,desired_look_at_))*1.08f);
    desired_position_=subtract(desired_look_at_,scale(view_forward,distance));
    overview_position_=desired_position_;overview_target_=desired_look_at_;
    pointer_x_=pointer_y_=look_x_=look_y_=0;

    const auto pose = camera.pose();
    current_position_ = pose.position;
    const float existing_view_distance = std::max(
        1.0f, length(subtract(current_look_at_, current_position_)));
    current_look_at_ = add(current_position_, scale(pose.forward, existing_view_distance));
    if (smooth_ <= 0.0f) {
        current_position_ = desired_position_;
        current_look_at_ = desired_look_at_;
    }
    camera.setNearClip(tuning.focused_near_clip);
    hide_overworld_actors_ = false;
    stage_ = Stage::Focused;
    return true;
}

bool AquariumInspectionCamera::focusPokemon(const std::string& id,camera::Vec3 position,float radius,camera::Gen4FollowCamera& camera) {
    if(stage_!=Stage::Focused)return false;
    pokemon_id_=id;
    pokemon_radius_=std::max(3.0f,radius);
    current_position_=camera.pose().position;
    stage_=Stage::Pokemon;trackPokemon(position);return true;
}
bool AquariumInspectionCamera::leavePokemon() {
    if(stage_!=Stage::Pokemon)return false;
    pokemon_id_.clear();desired_position_=overview_position_;desired_look_at_=overview_target_;
    desired_fov_=original_fov_;
    pointer_x_=pointer_y_=look_x_=look_y_=0;stage_=Stage::Focused;return true;
}
void AquariumInspectionCamera::trackPokemon(camera::Vec3 position) {
    if(stage_!=Stage::Pokemon)return;
    const auto& tank=tanks_[active_tank_index_];
    const auto outward=scale(facingVector(approach_facing_),-1);
    const auto local=rotateLocal(position.x-tank.world_center[0],0,position.z-tank.world_center[2],-tank.yaw_degrees);
    const auto direction=rotateLocal(outward.x,0,outward.z,-tank.yaw_degrees);
    float to_glass=std::numeric_limits<float>::max();
    if(std::abs(direction.x)>.001f)to_glass=std::min(to_glass,
        ((direction.x>0?tank.half_width_world:-tank.half_width_world)-local.x)/direction.x);
    if(std::abs(direction.z)>.001f)to_glass=std::min(to_glass,
        ((direction.z>0?tank.half_depth_world:-tank.half_depth_world)-local.z)/direction.z);
    const float clearance=std::max(24.0f,tank.inspection_camera.focused_near_clip+8);
    const float normal_distance=pokemon_radius_*1.6f/std::tan(original_fov_*kPi/360);
    const float distance=std::max(normal_distance,std::max(0.0f,to_glass)+clearance);
    desired_position_=add(position,scale(outward,distance));
    desired_position_.y=std::max(tank.floor_y_world+24,position.y+pokemon_radius_*.25f);
    desired_look_at_=position;
    // Physical movement stops outside the approached glass face. Optical zoom
    // supplies the remaining close-up for small residents deeper in the water.
    const float view_distance=length(subtract(desired_position_,position));
    desired_fov_=std::clamp(2*std::atan(pokemon_radius_*1.6f/std::max(1.0f,view_distance))*180/kPi,3.0f,original_fov_);
}
std::vector<std::string> AquariumInspectionCamera::hiddenPlacementIds(const camera::Gen4FollowCamera::Pose& pose) const {
    std::vector<std::string> hidden;
    if(!active()||returning())return hidden;
    struct ViewBounds {float left=1e20f,right=-1e20f,bottom=1e20f,top=-1e20f,depth=0;};
    const auto projected=[&](const AquariumTankRuntime& tank){
        ViewBounds box;
        box.depth=(tank.world_center[0]-pose.position.x)*pose.forward.x+
            ((tank.water_bottom_world+tank.water_top_world)*.5f-pose.position.y)*pose.forward.y+
            (tank.world_center[2]-pose.position.z)*pose.forward.z;
        for(float x:{-tank.half_width_world,tank.half_width_world})for(float z:{-tank.half_depth_world,tank.half_depth_world})
            for(float y:{tank.water_bottom_world,tank.water_top_world}){
                auto p=rotateLocal(x,0,z,tank.yaw_degrees);
                p=add(p,{tank.world_center[0]-pose.position.x,y-pose.position.y,tank.world_center[2]-pose.position.z});
                const float d=p.x*pose.forward.x+p.y*pose.forward.y+p.z*pose.forward.z;
                if(d<=.01f)continue;
                const float u=(p.x*pose.right.x+p.y*pose.right.y+p.z*pose.right.z)/d;
                const float v=(p.x*pose.up.x+p.y*pose.up.y+p.z*pose.up.z)/d;
                box.left=std::min(box.left,u);box.right=std::max(box.right,u);
                box.bottom=std::min(box.bottom,v);box.top=std::max(box.top,v);
            }
        return box;
    };
    const auto target=projected(tanks_[active_tank_index_]);
    for(const auto& tank:tanks_){
        if(tank.placement_id==active_placement_id_)continue;
        const auto local=rotateLocal(pose.position.x-tank.world_center[0],0,
            pose.position.z-tank.world_center[2],-tank.yaw_degrees);
        // The first-view camera can sit just above a rear tank. Its water and
        // glass still cover the lower view even though the eye is not inside it.
        const bool above_or_overlapping=std::abs(local.x)<=tank.half_width_world &&
            std::abs(local.z)<=tank.half_depth_world;
        float furthest=-std::numeric_limits<float>::max();
        for(float x:{-tank.half_width_world,tank.half_width_world})for(float z:{-tank.half_depth_world,tank.half_depth_world})
            for(float y:{tank.water_bottom_world,tank.water_top_world}){
                const auto corner=rotateLocal(x,0,z,tank.yaw_degrees);
                furthest=std::max(furthest,(corner.x+tank.world_center[0]-pose.position.x)*pose.forward.x+
                    (y-pose.position.y)*pose.forward.y+(corner.z+tank.world_center[2]-pose.position.z)*pose.forward.z);
            }
        const auto bounds=projected(tank);
        const bool overlaps_view=bounds.right>target.left&&bounds.left<target.right&&
            bounds.top>target.bottom&&bounds.bottom<target.top;
        // Foreground occluders are visible but block the exhibit. A one-cell
        // depth tolerance keeps neighboring tanks in the same row present.
        // A center behind the eye does not imply the entire tank is behind it:
        // large tanks can straddle the camera plane and remain an obstruction.
        const bool foreground=overlaps_view && bounds.depth<target.depth-16;
        if(above_or_overlapping||furthest<0||foreground)hidden.push_back(tank.placement_id);
    }
    return hidden;
}
void AquariumInspectionCamera::setPointerLook(float x,float y) {
    if(stage_==Stage::Focused){pointer_x_=std::clamp(x,-1.0f,1.0f);pointer_y_=std::clamp(y,-1.0f,1.0f);}
}

void AquariumInspectionCamera::beginExit(
    camera::Vec3 player_position,
    camera::Gen4FollowCamera& camera) {
    if (!active() || stage_ == Stage::Returning) return;
    desired_fov_=original_fov_;
    return_target_ = player_position;
    // Actors belong to normal world presentation, not to the camera's return
    // interpolation. Reveal them as soon as the player exits focused mode.
    hide_overworld_actors_ = false;
    if (smooth_ <= 0.0f) {
        auto preset=camera.pose().preset;preset.fov_y_deg=original_fov_;
        camera= camera::Gen4FollowCamera(preset);
        camera.setNearClip(original_near_clip_);
        camera.setTarget(return_target_);
        close();
        return;
    }

    const auto pose = camera.pose();
    current_position_ = pose.position;
    const float existing_view_distance = std::max(
        1.0f, length(subtract(current_look_at_, current_position_)));
    current_look_at_ = add(current_position_, scale(pose.forward, existing_view_distance));
    desired_position_ = return_position_;
    desired_look_at_ = return_target_;
    smooth_ = return_smooth_;
    stage_ = Stage::Returning;
}

void AquariumInspectionCamera::updateReturnTarget(camera::Vec3 player_position) {
    if (stage_ != Stage::Returning) return;
    const camera::Vec3 player_delta = subtract(player_position, return_target_);
    return_target_ = player_position;
    return_position_ = add(return_position_, player_delta);
    desired_position_ = return_position_;
    desired_look_at_ = return_target_;
}

void AquariumInspectionCamera::update(double dt_seconds, camera::Gen4FollowCamera& camera) {
    if (!active()) return;
    const float zoom_blend=smooth_<=0?1.0f:1-std::exp(-6*float(std::max(0.0,dt_seconds)));
    current_fov_+=(desired_fov_-current_fov_)*zoom_blend;
    auto preset=camera.pose().preset;preset.fov_y_deg=current_fov_;
    camera=camera::Gen4FollowCamera(preset);
    if (smooth_ > 0.0f) {
        const float step = smooth_ * static_cast<float>(std::clamp(dt_seconds, 0.0, 0.1));
        current_position_ = moveToward(current_position_, desired_position_, step);
        current_look_at_ = moveToward(current_look_at_, desired_look_at_, step);
    } else {
        current_position_ = desired_position_;
        current_look_at_ = desired_look_at_;
    }
    if (stage_ == Stage::Returning &&
        length(subtract(current_position_, desired_position_)) <= 0.0001f &&
        length(subtract(current_look_at_, desired_look_at_)) <= 0.0001f) {
        camera.setNearClip(original_near_clip_);
        preset.fov_y_deg=original_fov_;preset.near_clip=original_near_clip_;
        camera=camera::Gen4FollowCamera(preset);
        camera.setTarget(return_target_);
        close();
        return;
    }
    auto look=current_look_at_;
    if(stage_==Stage::Focused){
        const float blend=1-std::exp(-6*float(std::max(0.0,dt_seconds)));
        look_x_+=(pointer_x_-look_x_)*blend;look_y_+=(pointer_y_-look_y_)*blend;
        const auto right=facingRight(facingVector(approach_facing_));
        const float amount=length(subtract(current_look_at_,current_position_))*.045f;
        look=add(look,scale(right,look_x_*amount));look.y-=look_y_*amount;
    }
    applyManualLookAt(camera, current_position_, look);
}

void AquariumInspectionCamera::close() {
    stage_ = Stage::Inactive;
    hide_overworld_actors_ = false;
    active_placement_id_.clear();
    pokemon_id_.clear();pointer_x_=pointer_y_=look_x_=look_y_=0;
}

} // namespace pr::gameplay::world3d::aquarium
