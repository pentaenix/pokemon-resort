#include "ui/Overworld3DTestScreen.hpp"
#include "core/app/AppPaths.hpp"
#include "core/config/ConfigLoader.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>

namespace pr {
namespace rooms=gameplay::world3d::aquarium::rooms;
namespace aqc=gameplay::world3d::aquarium::construction;
namespace {
std::filesystem::path profilePath(const std::string& root) {
    const auto persistence=loadConfigFromJson(
        (std::filesystem::path(root)/"config/title_screen.json").string()).persistence;
    auto profile=std::filesystem::path(persistence.resort_profile_file_name).stem().string();
    if(profile.empty()) profile="default";
    for(char& c:profile) if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
        (c>='0'&&c<='9')||c=='_'||c=='-')) c='_';
    return resolveSaveDirectory(persistence,root)/"aquariums"/profile;
}
const char* buildingFile="aquarium_builder_lab.room.json";
}

void Overworld3DTestScreen::loadAquariumRooms(
    std::vector<gameplay::world3d::characters::LoadedWorldChunk>& chunks) {
    const auto source=std::find_if(chunks.begin(),chunks.end(),[](const auto& c){return c.id=="aquarium_builder_lab";});
    if(source==chunks.end()) return;
    aquarium_room_style_=source->scene;
    aquarium_room_read_only_=false;
    aquarium_building_=rooms::roomLayoutFromScene(source->scene);
    try {
        rooms::AquariumRoomStore store(profilePath(project_root_)/buildingFile);
        if(store.exists()) {
            const auto loaded=store.load();
            if(!loaded.document || !rooms::findRoom(*loaded.document,"aquarium_builder_lab"))
                throw std::runtime_error("Unrecoverable or newer building document");
            aquarium_building_=*loaded.document;
        }
        auto projected=chunks;
        int next_origin=4096;
        for(const auto& c:chunks) next_origin=std::max(next_origin,c.origin_tile_x+4096);
        for(const auto& room:aquarium_building_->rooms) {
            auto scene=rooms::projectBuildingRoom(aquarium_room_style_,*aquarium_building_,room);
            auto found=std::find_if(projected.begin(),projected.end(),[&](const auto& c){return c.id==room.id;});
            if(found!=projected.end()) {
                if(room.id!="aquarium_builder_lab") throw std::runtime_error("Room ID collides with a source map");
                found->scene=std::move(scene);
            } else {
                projected.push_back({room.id,std::move(scene),next_origin,0});
                next_origin+=4096;
            }
        }
        chunks=std::move(projected);
    } catch(const std::exception& e) {
        aquarium_room_read_only_=true;
        aquarium_building_=rooms::roomLayoutFromScene(aquarium_room_style_);
        std::cerr<<"[AquariumRoom] event=load_rejected reason="<<e.what()<<'\n';
    }
}

std::vector<rooms::RoomOccupancy> Overworld3DTestScreen::aquariumRoomOccupancy() const {
    std::vector<rooms::RoomOccupancy> result;
    if(!aquarium_building_) return result;
    for(const auto& room:aquarium_building_->rooms) {
        aqc::AquariumDesignDocument doc;
        if(room.id==scene_.id) doc=aquarium_construction_.committedDesign();
        else {
            aqc::AquariumDesignStore store(profilePath(project_root_)/(room.id+".aquarium.json"));
            const auto loaded=store.load();
            if(loaded.status!=aqc::AquariumStoreLoadStatus::Missing && !loaded.document)
                throw std::runtime_error("Cannot safely inspect another room's tanks");
            if(loaded.document) doc=*loaded.document;
            if(loaded.document && doc.map_id!=room.id)
                throw std::runtime_error("Another room's tank document has the wrong map ID");
        }
        const auto frame=doc.room_frame.value_or(pr::aquarium::geometry::GridCell{});
        rooms::RoomOccupancy occupied{room.id,{}};
        for(const auto& tank:doc.tanks) for(auto p:aqc::tankFootprintCells(tank))
            for(int y=0;y<2;++y) for(int x=0;x<2;++x)
                occupied.cells.push_back({p.column+frame.column+x,p.row+frame.row+y});
        result.push_back(std::move(occupied));
    }
    return result;
}

std::vector<pr::aquarium::geometry::GridCell> Overworld3DTestScreen::aquariumCombinedCollisionCells() {
    auto cells=player_aquarium_runtime_.collision_cells;
    if(!room_decoration_catalog_loaded_) {
        room_decoration_catalog_.scan(project_root_);room_decoration_catalog_loaded_=true;
    }
    const auto* room=aquarium_building_?rooms::findRoom(*aquarium_building_,scene_.id):nullptr;
    if(room) for(const auto& object:room->decorations) {
        const auto* asset=room_decoration_catalog_.resolve(object.asset_id);
        if(!asset){cells.push_back({object.cell.column,object.cell.row});continue;}
        for(const auto occupied:rooms::roomDecorationCollisionCells(
            *asset,object.cell,object.yaw_quarter_turns,scene_.grid.tile_size))
            cells.push_back({occupied.column,occupied.row});
    }
    return cells;
}

bool Overworld3DTestScreen::playerOnAquariumDecorationWaterSurface() {
    if(!aquarium_building_)return false;
    if(!room_decoration_catalog_loaded_) {
        room_decoration_catalog_.scan(project_root_);room_decoration_catalog_loaded_=true;
    }
    const auto* room=rooms::findRoom(*aquarium_building_,scene_.id);
    if(!room)return false;
    for(const auto& object:room->decorations) {
        const auto* asset=room_decoration_catalog_.resolve(object.asset_id);
        if(!asset||asset->surface_effect!=rooms::RoomDecorationSurfaceEffect::ShallowWater)continue;
        for(const auto cell:rooms::roomDecorationPlacementCells(
            *asset,object.cell,object.yaw_quarter_turns,scene_.grid.tile_size))
            if(cell.column==player_.tileX()&&cell.row==player_.tileY())return true;
    }
    return false;
}

bool Overworld3DTestScreen::beginAquariumRoomResize() {
    const auto state=aquarium_construction_.state();
    if(!aquarium_building_ || !rooms::findRoom(*aquarium_building_,scene_.id) ||
        aquarium_room_read_only_ || aquarium_commit_future_.valid() ||
        (state!=aqc::ConstructionState::Browse && state!=aqc::ConstructionState::Selected)) return false;
    resetAquariumConstructionPointerOperation();
    aquarium_pointer_controls_cursor_=false;
    aquarium_construction_.clearSelection();
    aquarium_room_candidate_=*aquarium_building_;
    aquarium_room_draft_=rooms::findRoom(*aquarium_building_,scene_.id)->bounds;
    aquarium_room_edit_mode_=aqc::ConstructionRoomEditMode::Layout;
    aquarium_room_palette_index_=0;
    aquarium_room_transition_kind_=rooms::RoomTransitionKind::Stairs;
    aquarium_room_error_.clear(); aquarium_room_gesture_.reset();
    aquarium_room_paint_gesture_.reset();
    aquarium_room_level_selection_baseline_.reset();
    aquarium_room_level_selection_ready_=false;
    aquarium_room_level_knob_dragging_=false;
    aquarium_room_paint_wall_.reset();
    aquarium_room_wall_camera_pan_=0.0f;
    aquarium_room_terrain_camera_initialized_=false;
    aquarium_room_terrain_camera_panning_=false;
    aquarium_room_terrain_top_down_=false;
    aquarium_room_handle_focus_=0;
    if(!room_decoration_catalog_loaded_) {
        room_decoration_catalog_.scan(project_root_);room_decoration_catalog_loaded_=true;
    }
    room_decoration_asset_.reset();room_decoration_selected_.reset();
    room_decoration_draft_.reset();room_decoration_dragging_=false;
    room_decoration_category_index_=0;room_decoration_asset_index_=0;
    return true;
}

bool Overworld3DTestScreen::adjustAquariumRoomSize(int,int) {
    // Wheel zoom: keep the current camera centre; wall dragging never chases itself.
    if(!aquarium_room_draft_ || aquarium_room_gesture_) return false;
    if((aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Levels||
        aquarium_room_edit_mode_==aqc::ConstructionRoomEditMode::Transitions)&&
        aquarium_room_terrain_top_down_) {
        frameAquariumRoomTerrainEditor();return true;
    }
    const auto overview=aqc::trackAquariumConstructionCursor(
        aquarium_construction_camera_tracking_,scene_.grid.width,scene_.grid.height,
        scene_.grid.tile_size,scene_.interior.floor_datum,camera_.pose().preset.fov_y_deg,
        float(app_config_.window.virtual_width)/std::max(1,app_config_.window.virtual_height),
        follow_camera_base_preset_.pitch_deg,aquarium_construction_.cursor(),false,.1,0);
    camera_.setManualPose(overview.position,overview.yaw_degrees,overview.pitch_degrees);
    return true;
}

void Overworld3DTestScreen::frameAquariumRoomTerrainEditor() {
    if(!aquarium_room_draft_) return;
    constexpr float kPi=3.1415926535f;
    const auto bounds=*aquarium_room_draft_;
    const float tile=std::max(1.0f,scene_.grid.tile_size);
    const float width=std::max(1,bounds.width)*tile;
    const float depth=std::max(1,bounds.depth)*tile;
    const float aspect=float(std::max(1,app_config_.window.virtual_width)) /
        float(std::max(1,app_config_.window.virtual_height));
    const float tangent=std::tan(std::clamp(camera_.pose().preset.fov_y_deg,
        15.0f,70.0f)*kPi/360.0f);
    const float fit_vertical=(depth*.5f+tile*1.5f)/std::max(.1f,tangent);
    const float fit_horizontal=(width*.5f+tile*1.5f)/std::max(.1f,tangent*aspect);
    const float distance=std::min(std::max(fit_vertical,fit_horizontal)*
        aquarium_construction_camera_tracking_.zoom_scale,
        std::max(tile*8.0f,camera_.pose().preset.far_clip-tile*2.0f));
    const float origin_x=(bounds.column-scene_.interior.grid_origin_x)*tile;
    const float origin_z=(bounds.row-scene_.interior.grid_origin_y)*tile;
    if(!aquarium_room_terrain_camera_initialized_) {
        aquarium_room_terrain_camera_x_=origin_x+width*.5f;
        aquarium_room_terrain_camera_z_=origin_z+depth*.5f;
        aquarium_room_terrain_camera_initialized_=true;
    }
    aquarium_room_terrain_camera_x_=std::clamp(aquarium_room_terrain_camera_x_,
        origin_x-tile*2.0f,origin_x+width+tile*2.0f);
    aquarium_room_terrain_camera_z_=std::clamp(aquarium_room_terrain_camera_z_,
        origin_z-tile*2.0f,origin_z+depth+tile*6.0f);
    camera_.setManualPose({aquarium_room_terrain_camera_x_,scene_.interior.floor_datum+distance,
        aquarium_room_terrain_camera_z_},
        180.0f,-89.0f);
}

void Overworld3DTestScreen::previewAquariumRoomSurfaceStyle() {
    if(!aquarium_room_candidate_) return;
    const auto* room=rooms::findRoom(*aquarium_room_candidate_,scene_.id);
    if(!room) return;
    const auto projected=rooms::projectBuildingRoom(aquarium_room_style_,*aquarium_room_candidate_,*room);
    scene_.interior.default_room=projected.interior.default_room;
    scene_.tile_layers=projected.tile_layers;
    if(bgfx_renderer_ && !bgfx_renderer_->setDefaultRoomPreview(
        scene_.interior.default_room,projected.terrain,projected.tile_layers))
        aquarium_room_error_="Could not preview the room surface";
}

bool Overworld3DTestScreen::setAquariumRoomSurfacePalette(int palette) {
    if(!aquarium_room_candidate_ ||
        (aquarium_room_edit_mode_!=aqc::ConstructionRoomEditMode::Floor &&
         aquarium_room_edit_mode_!=aqc::ConstructionRoomEditMode::Walls)) return false;
    palette=std::clamp(palette,0,rooms::kRoomSurfacePaletteCount-1);
    aquarium_room_palette_index_=palette;
    aquarium_room_error_.clear();
    return true;
}

void Overworld3DTestScreen::cancelAquariumRoomResize() {
    if(room_decoration_draft_) {
        room_decoration_draft_.reset();room_decoration_dragging_=false;
        aquarium_room_error_.clear();
        refreshAquariumRenderActors();return;
    }
    if(aquarium_room_paint_gesture_) {
        if(aquarium_room_level_selection_ready_&&aquarium_room_level_selection_baseline_) {
            aquarium_room_candidate_=*aquarium_room_level_selection_baseline_;
            previewAquariumRoomSurfaceStyle();
        }
        aquarium_room_paint_gesture_.reset();
        aquarium_room_level_selection_baseline_.reset();
        aquarium_room_level_selection_ready_=false;
        aquarium_room_level_knob_dragging_=false;
        return;
    }
    if(aquarium_room_gesture_) {
        aquarium_room_candidate_=aquarium_room_gesture_->baseline;
        aquarium_room_draft_=rooms::findRoom(*aquarium_room_candidate_,scene_.id)->bounds;
        aquarium_room_gesture_.reset(); aquarium_room_error_.clear(); return;
    }
    if(aquarium_building_) {
        aquarium_room_candidate_=*aquarium_building_;
        previewAquariumRoomSurfaceStyle();
    }
    aquarium_room_candidate_.reset(); aquarium_room_draft_.reset(); aquarium_room_error_.clear();
    aquarium_room_paint_gesture_.reset();
    aquarium_room_level_selection_baseline_.reset();
    aquarium_room_level_selection_ready_=false;
    aquarium_room_level_knob_dragging_=false;
    aquarium_room_paint_wall_.reset();
    aquarium_room_wall_camera_pan_=0.0f;
    aquarium_room_terrain_camera_panning_=false;
    aquarium_room_terrain_top_down_=false;
    room_decoration_selected_.reset();room_decoration_draft_.reset();room_decoration_dragging_=false;
    aquarium_room_edit_mode_=aqc::ConstructionRoomEditMode::Layout;
    resetAquariumConstructionPointerOperation(); syncAquariumConstructionFocus();
}

bool Overworld3DTestScreen::commitAquariumRoomResize() {
    if(!aquarium_room_candidate_ || aquarium_room_read_only_) return false;
    if(aquarium_room_gesture_ && !finishAquariumRoomGesture()) return false;
    if(!aquarium_room_error_.empty()) {requestAquariumConstructionErrorFeedback(); return false;}
    const auto started=std::chrono::steady_clock::now();
    try {
        auto candidate=*aquarium_room_candidate_;
        const auto errors=rooms::validateBuildingLayout(candidate,aquariumRoomOccupancy());
        if(!errors.empty()) throw std::runtime_error(errors.front());
        candidate.revision=aquarium_building_->revision+1;
        // Stage every scene before the one building-document commit point.
        auto chunks=loaded_world_chunks_;
        int origin=4096;
        for(const auto& c:chunks) origin=std::max(origin,c.origin_tile_x+4096);
        for(const auto& room:candidate.rooms) {
            auto projected=rooms::projectBuildingRoom(aquarium_room_style_,candidate,room);
            auto found=std::find_if(chunks.begin(),chunks.end(),[&](const auto& c){return c.id==room.id;});
            if(found==chunks.end()) {chunks.push_back({room.id,std::move(projected),origin,0});origin+=4096;}
            else found->scene=std::move(projected);
        }
        const auto destination=std::find_if(chunks.begin(),chunks.end(),[&](const auto& c){return c.id==scene_.id;});
        const auto active=*destination;
        rooms::AquariumRoomStore store(profilePath(project_root_)/buildingFile);
        if(!store.save(candidate,aquarium_room_error_)) throw std::runtime_error(aquarium_room_error_);
        const auto controller=aquarium_construction_controller_id_;
        auto previous_session=aquarium_construction_;
        previous_session.exit();
        const bool same_frame=active.scene.interior.grid_origin_x==scene_.interior.grid_origin_x &&
            active.scene.interior.grid_origin_y==scene_.interior.grid_origin_y;
        exitAquariumConstruction();
        aquarium_building_=std::move(candidate);
        aquarium_room_candidate_.reset(); aquarium_room_gesture_.reset();
        loaded_world_chunks_=std::move(chunks);
        activateWorldMap(active);
        if(same_frame) if(const auto* config=gameplay::world3d::aquarium::aquariumMapConfig(aquarium_catalog_,scene_.id)) {
            previous_session.updateRoomBuildZone(config->construction);
            aquarium_construction_=std::move(previous_session);
        }
        if(aquarium_construction_return_cell_) {
            const auto p=*aquarium_construction_return_cell_;
            player_.teleportToTile(p.column,p.row,aquarium_construction_return_facing_);
            camera_.setTarget(player_.position());
        }
        onAquariumConstructionPressed(controller);
        aquarium_construction_save_sfx_requested_=true;
        std::cerr<<"[AquariumRoom] event=building_committed revision="<<aquarium_building_->revision
            <<" rooms="<<aquarium_building_->rooms.size()<<" cpuUs="
            <<std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count()<<'\n';
        return true;
    } catch(const std::exception& e) {
        aquarium_room_error_=e.what(); requestAquariumConstructionErrorFeedback();
        std::cerr<<"[AquariumRoom] event=commit_rejected reason="<<e.what()<<'\n'; return false;
    }
}
} // namespace pr
