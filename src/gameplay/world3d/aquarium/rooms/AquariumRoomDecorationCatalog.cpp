#include "gameplay/world3d/aquarium/rooms/AquariumRoomDecorationCatalog.hpp"
#include "core/config/Json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace pr::gameplay::world3d::aquarium::rooms {
namespace {
int integer(const JsonValue& object,const char* key,int minimum,int maximum) {
    const auto* value=object.get(key);
    if(!value||!value->isNumber()||!std::isfinite(value->asNumber())||
        value->asNumber()!=std::floor(value->asNumber())||value->asNumber()<minimum||value->asNumber()>maximum)
        throw std::runtime_error(std::string("Invalid room-decoration integer: ")+key);
    return int(value->asNumber());
}

void applyCatalogConfig(const std::filesystem::path& path,std::vector<RoomDecorationAsset>& entries) {
    const auto root=parseJsonFile(path.string());
    const auto* schema=root.get("schema");
    const auto* version=root.get("schemaVersion");
    const auto* assets=root.get("assets");
    if(!schema||!schema->isString()||schema->asString()!="pokemon-resort-aquarium-room-decorations"||
        !version||!version->isNumber()||version->asNumber()!=1||!assets||!assets->isArray())
        throw std::runtime_error("Invalid aquarium room-decoration catalog header");
    for(const auto& configured:assets->asArray()) {
        const auto* id=configured.get("assetId");
        const auto* collision=configured.get("collision");
        if(!id||!id->isString()||!collision||!collision->isObject())
            throw std::runtime_error("Invalid aquarium room-decoration asset entry");
        const auto found=std::find_if(entries.begin(),entries.end(),[&](const auto& entry){return entry.id==id->asString();});
        if(found==entries.end()) throw std::runtime_error("Configured room-decoration asset is missing: "+id->asString());
        if(const auto* category=configured.get("category")) {
            if(!category->isString()||category->asString().empty())
                throw std::runtime_error("Invalid room-decoration category: "+id->asString());
            if(std::find(kRoomDecorationCategories,std::end(kRoomDecorationCategories),category->asString())==
                std::end(kRoomDecorationCategories))
                throw std::runtime_error("Unknown room-decoration category: "+category->asString());
            found->category=category->asString();
        }
        if(const auto* surface=configured.get("surfaceEffect")) {
            if(!surface->isString())
                throw std::runtime_error("Invalid room-decoration surface effect: "+id->asString());
            if(surface->asString()=="none") found->surface_effect=RoomDecorationSurfaceEffect::None;
            else if(surface->asString()=="shallow-water")
                found->surface_effect=RoomDecorationSurfaceEffect::ShallowWater;
            else throw std::runtime_error("Unknown room-decoration surface effect: "+id->asString());
        }
        if(const auto* playback=configured.get("animationPlaybackRate")) {
            if(!playback->isNumber()||!std::isfinite(playback->asNumber())||
                std::abs(playback->asNumber())<0.01||std::abs(playback->asNumber())>8.0)
                throw std::runtime_error("Invalid room-decoration animation playback: "+id->asString());
            found->animation_playback_rate=float(playback->asNumber());
        }
        const auto* mode=collision->get("mode");
        if(!mode||!mode->isString()) throw std::runtime_error("Missing room-decoration collision mode: "+id->asString());
        if(mode->asString()=="solid-bounds") found->collision.mode=RoomDecorationCollisionMode::SolidBounds;
        else if(mode->asString()=="none") found->collision.mode=RoomDecorationCollisionMode::None;
        else if(mode->asString()=="cell-mask") found->collision.mode=RoomDecorationCollisionMode::CellMask;
        else throw std::runtime_error("Unknown room-decoration collision mode: "+mode->asString());
        found->collision.blocked_cells.clear();
        if(found->collision.mode==RoomDecorationCollisionMode::CellMask) {
            const auto* cells=collision->get("blockedCells");
            if(!cells||!cells->isArray()||cells->asArray().empty()||cells->asArray().size()>256)
                throw std::runtime_error("Invalid room-decoration collision mask: "+id->asString());
            for(const auto& cell:cells->asArray()) {
                Cell parsed{integer(cell,"column",-64,64),integer(cell,"row",-64,64)};
                if(std::find_if(found->collision.blocked_cells.begin(),found->collision.blocked_cells.end(),
                    [&](Cell existing){return existing.column==parsed.column&&existing.row==parsed.row;})!=
                    found->collision.blocked_cells.end())
                    throw std::runtime_error("Duplicate room-decoration collision cell: "+id->asString());
                found->collision.blocked_cells.push_back(parsed);
            }
        }
    }
}
}

std::string roomDecorationCategory(const std::string& asset_id) {
    std::string name=asset_id;
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char value){return char(std::tolower(value));});
    const auto has=[&](const char* word){return name.find(word)!=std::string::npos;};
    if(has("gate")||has("stair")||has("ruin")||has("fountain")||has("shirne")||
        has("shrine")||has("bell")||has("tile")) return "structures";
    if(has("chair")||has("table")||has("desk")||has("shelf")||has("cashier")||
        has("reji")||has("vase")||has("parasol")) return "furniture";
    if(has("plant")||has("tree")||has("stump")||has("thicket")||has("icicle")||
        has("dust")) return "nature";
    return "equipment";
}

void RoomDecorationCatalog::scan(const std::filesystem::path& project_root) {
    entries_.clear();
    error_.clear();
    const auto root = project_root / "assets/aquarium_room/decorations";
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(root, error), end;
    while (!error && iterator != end) {
        const auto& entry = *iterator;
        if (entry.is_regular_file(error) && !entry.is_symlink(error) &&
            entry.path().extension() == ".glb") {
            const auto relative = std::filesystem::relative(entry.path(), root, error);
            if (!error && !relative.empty() && *relative.begin() != "..") {
                RoomDecorationAsset asset;
                asset.id = relative.generic_string();
                asset.category = roomDecorationCategory(asset.id);
                asset.path = entry.path();
                entries_.push_back(std::move(asset));
            }
        }
        iterator.increment(error);
    }
    if (error) error_ = error.message();
    std::sort(entries_.begin(), entries_.end(), [](const auto& left, const auto& right) {
        return left.id < right.id;
    });
    if(error_.empty()) try {
        applyCatalogConfig(project_root/"config/gameplay/world3d/aquarium_room_decorations.json",entries_);
    } catch(const std::exception& exception) { error_=exception.what(); }
}

std::vector<std::size_t> RoomDecorationCatalog::indices(std::string_view category) const {
    std::vector<std::size_t> result;
    for(std::size_t index=0;index<entries_.size();++index)
        if(entries_[index].category==category) result.push_back(index);
    return result;
}

std::vector<Cell> roomDecorationCollisionCells(
    const RoomDecorationAsset& asset,Cell placement_cell,int yaw_quarter_turns,float tile_world_units) {
    std::vector<Cell> result;
    if(asset.collision.mode==RoomDecorationCollisionMode::None) return result;
    if(asset.collision.mode==RoomDecorationCollisionMode::CellMask) {
        int turns=((yaw_quarter_turns%4)+4)%4;
        result.reserve(asset.collision.blocked_cells.size());
        for(auto cell:asset.collision.blocked_cells) {
            // Match the renderer's positive yaw: local +X rotates toward -Z.
            for(int turn=0;turn<turns;++turn) cell={cell.row,-cell.column};
            result.push_back({placement_cell.column+cell.column,placement_cell.row+cell.row});
        }
        return result;
    }
    if(!asset.bounds.valid||!std::isfinite(tile_world_units)||tile_world_units<=0) {
        result.push_back(placement_cell);return result;
    }
    const bool quarter_turn=(yaw_quarter_turns&1)!=0;
    const float span_x=(quarter_turn?asset.bounds.max_z-asset.bounds.min_z:
        asset.bounds.max_x-asset.bounds.min_x)*asset.base_scale;
    const float span_z=(quarter_turn?asset.bounds.max_x-asset.bounds.min_x:
        asset.bounds.max_z-asset.bounds.min_z)*asset.base_scale;
    // Authoring footprints describe discrete occupied capacity, rather than
    // every cell touched by a sub-pixel edge. Project the measured span to an
    // exact cell count so centred props do not gain a phantom border cell.
    const auto occupied_cell_count=[&](float span) {
        float cells=span/tile_world_units;
        const float nearest=std::round(cells);
        // Exported props commonly include a tiny bevel or shadow fringe. If
        // the measured bounds are within five percent of an authored whole
        // cell footprint, keep that footprint instead of inventing a phantom
        // row/column on the negative side of a centred placement.
        if(std::abs(cells-nearest)<=0.05f) cells=nearest;
        return std::max(1,int(std::ceil(cells-.0001f)));
    };
    const int count_x=occupied_cell_count(span_x);
    const int count_z=occupied_cell_count(span_z);
    const int first_x=placement_cell.column-count_x/2;
    const int first_z=placement_cell.row-count_z/2;
    const int last_x=first_x+count_x-1;
    const int last_z=first_z+count_z-1;
    for(int row=first_z;row<=last_z;++row) for(int column=first_x;column<=last_x;++column)
        result.push_back({column,row});
    return result;
}

std::vector<Cell> roomDecorationPlacementCells(
    const RoomDecorationAsset& asset,Cell placement_cell,int yaw_quarter_turns,float tile_world_units) {
    if(asset.collision.mode!=RoomDecorationCollisionMode::None)
        return roomDecorationCollisionCells(asset,placement_cell,yaw_quarter_turns,tile_world_units);
    RoomDecorationAsset footprint_asset=asset;
    footprint_asset.collision.mode=RoomDecorationCollisionMode::SolidBounds;
    return roomDecorationCollisionCells(
        footprint_asset,placement_cell,yaw_quarter_turns,tile_world_units);
}

} // namespace pr::gameplay::world3d::aquarium::rooms
