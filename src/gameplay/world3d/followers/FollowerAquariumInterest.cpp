#include "gameplay/world3d/followers/FollowerController.hpp"
#include "gameplay/world3d/followers/AquariumFollowerPlanner.hpp"
#include <algorithm>

namespace pr::gameplay::world3d::followers {
void FollowerController::setAquariumInterest(const npc::VisitorRoomPlan& plan,const npc::AquariumVisitorConfig& config) {
    if(aquarium_interest_.revision==plan.revision)return;
    aquarium_interest_=plan;aquarium_interest_config_=config;
    aquarium_goal_.reset();aquarium_route_.clear();
    aquarium_idle_seconds_=aquarium_watch_seconds_=aquarium_retry_seconds_=0;
    path_.clear();idle_actions_.clear();idle_behavior_active_=false;cancel_return_active_=false;
}
void FollowerController::updateAquariumInterest(double dt,bool /*player_activity*/) {
    if(state_!=State::Active)return;
    if(manual_debug_action_!=ManualDebugActionType::None){updateManualDebugAction(dt);return;}
    dt=std::clamp(dt,0.0,.1);
    path_.clear(); // Do not replay the owner's stale trail after an exhibit visit.
    const bool moved=player_tile_.x!=aquarium_last_player_.x||player_tile_.y!=aquarium_last_player_.y;
    aquarium_last_player_=player_tile_;
    if(moved||!player_idle_) {
        aquarium_idle_seconds_=aquarium_watch_seconds_=0;
        aquarium_retry_seconds_=0;
        aquarium_goal_.reset();aquarium_route_.clear();
    } else aquarium_idle_seconds_+=dt;
    if(follower_moving_)return;
    bool return_choice=false;
    if(aquarium_watch_seconds_>0) {
        aquarium_watch_seconds_=std::max(0.0,aquarium_watch_seconds_-dt);
        if(aquarium_goal_)follower_facing_=aquarium_goal_->facing;
        if(aquarium_watch_seconds_>0)return;
        aquarium_goal_.reset();
        return_choice=std::uniform_real_distribution<double>(0,1)(rng_)<.2;
    }
    const bool explore=player_idle_&&idle_config_.enabled&&aquarium_idle_seconds_>=idle_config_.start_after_idle_seconds;
    const auto trailing=aquarium_trailing_cell_.cell.value_or(npc::VisitorCell{follower_tile_.x,follower_tile_.y});
    if(!explore&&npc::VisitorCell{follower_tile_.x,follower_tile_.y}==trailing) {
        active_behavior_label_="none";return;
    }
    aquarium_retry_seconds_-=dt;if(aquarium_retry_seconds_>0)return;
    const int w=scene_.grid.width,h=scene_.grid.height;
    const auto available=[&](int x,int y){return x>=0&&y>=0&&x<w&&y<h&&terrain_query_&&
        !terrain_query_->tileBlocked(x,y)&&!terrain_query_->tileIsActualWater(x,y)&&
        !(x==player_tile_.x&&y==player_tile_.y)&&
        std::find(aquarium_occupied_.begin(),aquarium_occupied_.end(),std::make_pair(x,y))==aquarium_occupied_.end();};
    if(aquarium_goal_ && !available(aquarium_goal_->cell.x,aquarium_goal_->cell.y)) {
        aquarium_goal_.reset();aquarium_route_.clear();
    }
    if(!aquarium_goal_) {
        std::vector<unsigned char> free(std::size_t(w)*h,0);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x)free[y*w+x]=available(x,y);
        for(const auto& d:aquarium_interest_.portals)if(d.cell.x>=0&&d.cell.y>=0&&d.cell.x<w&&d.cell.y<h)
            free[d.cell.y*w+d.cell.x]=0;
        if(follower_tile_.x<0||follower_tile_.y<0||follower_tile_.x>=w||follower_tile_.y>=h)return;
        free[follower_tile_.y*w+follower_tile_.x]=1;
        const auto plan=planAquariumFollowerVisit(w,h,free,{follower_tile_.x,follower_tile_.y},
            {player_tile_.x,player_tile_.y},aquarium_interest_.spots,aquarium_seen_tanks_,!explore||return_choice,rng_,
            player_facing_,trailing,explore&&std::uniform_real_distribution<double>(0,1)(rng_)<.4);
        if(!plan){aquarium_retry_seconds_=1;return;}
        aquarium_goal_=plan->target;
        auto route=plan->route;
        if(!explore) {
            std::vector<npc::VisitorCell> trail;
            for(auto p:player_step_trail_)trail.push_back({p.x,p.y});
            auto snake=aquariumFollowerTrailRoute(trail,{follower_tile_.x,follower_tile_.y},trailing);
            if(!snake.empty())route=std::move(snake);
        }
        for(auto p:route)aquarium_route_.push_back({p.x,p.y});
        active_behavior_label_=aquarium_goal_->tank_id.empty()?"aquarium_return":"aquarium_inspect";
    }
    if(!aquarium_route_.empty()) {
        const auto next=aquarium_route_.front();
        if(available(next.x,next.y)) {
            const double speed=explore?aquarium_interest_config_.adult_walk_speed_multiplier:
                (player_running_?movement_config_.runSpeed()/std::max(1.0f,movement_config_.walkSpeed()):1.0);
            beginStepToTile(next,speed,false);
            if(follower_moving_){aquarium_route_.pop_front();return;}
        }
        aquarium_goal_.reset();aquarium_route_.clear();aquarium_retry_seconds_=.5;return;
    }
    if(aquarium_goal_) {
        follower_facing_=aquarium_goal_->facing;
        if(!explore){aquarium_goal_.reset();return;}
        if(!aquarium_goal_->tank_id.empty())aquarium_seen_tanks_.insert(aquarium_goal_->tank_id);
        aquarium_watch_seconds_=aquarium_goal_->tank_id.empty()?3:
            std::uniform_real_distribution<double>(aquarium_interest_config_.watch_min_seconds,
                aquarium_interest_config_.watch_max_seconds)(rng_);
    }
}
}
