#include "gameplay/world3d/aquarium/rooms/AquariumBuildingLayout.hpp"
#include "core/config/Json.hpp"

#include <cmath>
#include <stdexcept>

namespace pr::gameplay::world3d::aquarium::rooms {
namespace {
const JsonValue& field(const JsonValue& value,const char* key) {
    if (!value.isObject() || !value.get(key)) throw std::runtime_error(std::string("Missing building field: ")+key);
    return *value.get(key);
}
std::string string(const JsonValue& value,const char* key) {
    const auto& result=field(value,key);
    if (!result.isString()) throw std::runtime_error(std::string("Expected building string: ")+key);
    return result.asString();
}
double integer(const JsonValue& value,const char* key,double minimum,double maximum) {
    const auto& result=field(value,key);
    if (!result.isNumber() || !std::isfinite(result.asNumber()) ||
        result.asNumber()!=std::floor(result.asNumber()) || result.asNumber()<minimum || result.asNumber()>maximum)
        throw std::runtime_error(std::string("Invalid building integer: ")+key);
    return result.asNumber();
}
const JsonValue::Array& array(const JsonValue& value,const char* key) {
    const auto& result=field(value,key);
    if (!result.isArray() || result.asArray().size()>1024) throw std::runtime_error(std::string("Invalid building array: ")+key);
    return result.asArray();
}
Wall wall(const std::string& name) {
    for (auto value:{Wall::North,Wall::East,Wall::South,Wall::West}) if (name==wallName(value)) return value;
    throw std::runtime_error("Invalid door wall");
}
RoomTransitionKind transitionKind(const std::string& name) {
    if(name=="ramp")return RoomTransitionKind::Ramp;
    if(name=="stairs")return RoomTransitionKind::Stairs;
    throw std::runtime_error("Invalid room transition kind");
}
DoorEndpoint endpoint(const JsonValue& value) { return {string(value,"roomId"),string(value,"doorId")}; }
JsonValue endpointJson(const DoorEndpoint& value) {
    return JsonValue(JsonValue::Object{{"roomId",JsonValue(value.room_id)},{"doorId",JsonValue(value.door_id)}});
}
JsonValue number(double value) { return JsonValue(value); }
}

LayoutLoadResult parseBuildingLayout(const std::string& text) {
    try {
        const auto root=parseJsonText(text);
        if (string(root,"schema")!="pokemon-resort-aquarium-building") throw std::runtime_error("Wrong building schema");
        const auto version=integer(root,"schemaVersion",1,9007199254740991.0);
        if (version>5) return {LayoutLoadStatus::NewerVersion,std::nullopt,"Newer building schema; preserve without overwrite"};
        BuildingLayout layout;
        layout.id=string(root,"buildingId");
        layout.revision=static_cast<std::uint64_t>(integer(root,"revision",0,9007199254740991.0));
        for (const auto& value:array(root,"rooms")) {
            RoomLayout room;
            room.id=string(value,"id");
            const auto& bounds=field(value,"bounds");
            room.bounds={int(integer(bounds,"column",-4096,4096)),int(integer(bounds,"row",-4096,4096)),
                int(integer(bounds,"widthCells",8,128)),int(integer(bounds,"depthCells",8,128))};
            for (const auto& door:array(value,"doors")) room.doors.push_back({string(door,"id"),
                wall(string(door,"wall")),int(integer(door,"offsetCells",0,127))});
            if(const auto* surfaces=value.get("surfaces");surfaces&&surfaces->isObject()) {
                room.surfaces.floor_palette=int(integer(*surfaces,"floorPalette",0,kRoomSurfacePaletteCount-1));
                room.surfaces.wall_palette=int(integer(*surfaces,"wallPalette",0,kRoomSurfacePaletteCount-1));
                if(const auto* floors=surfaces->get("floorOverrides");floors&&floors->isArray()) {
                    if(floors->asArray().size()>16384) throw std::runtime_error("Too many floor overrides");
                    for(const auto& item:floors->asArray()) room.surfaces.floor_overrides.push_back({
                        {int(integer(item,"column",0,127)),int(integer(item,"row",0,127))},
                        int(integer(item,"palette",0,kRoomSurfacePaletteCount-1))});
                }
                if(const auto* depths=surfaces->get("floorDepthOverrides");depths&&depths->isArray()) {
                    if(depths->asArray().size()>16384) throw std::runtime_error("Too many floor depth overrides");
                    for(const auto& item:depths->asArray()) room.surfaces.floor_depth_overrides.push_back({
                        {int(integer(item,"column",0,127)),int(integer(item,"row",0,127))},
                        int(integer(item,"depth",1,3))});
                }
                if(const auto* walls=surfaces->get("wallOverrides");walls&&walls->isArray()) {
                    if(walls->asArray().size()>512) throw std::runtime_error("Too many wall overrides");
                    for(const auto& item:walls->asArray()) room.surfaces.wall_overrides.push_back({
                        wall(string(item,"wall")),int(integer(item,"segment",0,127)),
                        int(integer(item,"palette",0,kRoomSurfacePaletteCount-1))});
                }
            }
            if(const auto* decorations=value.get("decorations");decorations&&decorations->isArray()) {
                if(decorations->asArray().size()>kRoomDecorationLimit) throw std::runtime_error("Too many room decorations");
                for(const auto& item:decorations->asArray()) room.decorations.push_back({
                    string(item,"id"),string(item,"assetId"),
                    {int(integer(item,"column",0,127)),int(integer(item,"row",0,127))},
                    int(integer(item,"yawQuarterTurns",0,3))});
            }
            if(const auto* transitions=value.get("transitions");transitions&&transitions->isArray()) {
                if(transitions->asArray().size()>512) throw std::runtime_error("Too many room transitions");
                for(const auto& item:transitions->asArray()) room.transitions.push_back({
                    string(item,"id"),transitionKind(string(item,"kind")),
                    {int(integer(item,"lowerColumn",0,127)),int(integer(item,"lowerRow",0,127))},
                    {int(integer(item,"upperColumn",0,127)),int(integer(item,"upperRow",0,127))}});
            }
            layout.rooms.push_back(std::move(room));
        }
        for (const auto& value:array(root,"connections")) layout.connections.push_back({string(value,"id"),
            endpoint(field(value,"first")),endpoint(field(value,"second"))});
        for (const auto& value:array(root,"externalConnections")) layout.external_connections.push_back({
            string(value,"id"),endpoint(field(value,"interior")),string(value,"destinationMapId"),string(value,"destinationAnchorId")});
        const auto errors=validateBuildingLayout(layout);
        if (!errors.empty()) return {LayoutLoadStatus::Invalid,std::nullopt,errors.front()};
        return {LayoutLoadStatus::Loaded,std::move(layout),{}};
    } catch (const std::exception& error) { return {LayoutLoadStatus::Invalid,std::nullopt,error.what()}; }
}

std::string serializeBuildingLayout(const BuildingLayout& layout) {
    const auto errors=validateBuildingLayout(layout);
    if (!errors.empty()) throw std::runtime_error(errors.front());
    JsonValue::Array rooms,connections,external;
    for (const auto& room:layout.rooms) {
        JsonValue::Array doors;
        JsonValue::Array floor_overrides,floor_depth_overrides,wall_overrides;
        JsonValue::Array decorations,transitions;
        for (const auto& door:room.doors) doors.emplace_back(JsonValue::Object{
            {"id",JsonValue(door.id)},{"wall",JsonValue(std::string(wallName(door.wall)))},{"offsetCells",number(door.offset)}});
        for(const auto& item:room.surfaces.floor_overrides) floor_overrides.emplace_back(JsonValue::Object{
            {"column",number(item.cell.column)},{"row",number(item.cell.row)},{"palette",number(item.palette)}});
        for(const auto& item:room.surfaces.floor_depth_overrides) floor_depth_overrides.emplace_back(JsonValue::Object{
            {"column",number(item.cell.column)},{"row",number(item.cell.row)},{"depth",number(item.depth)}});
        for(const auto& item:room.surfaces.wall_overrides) wall_overrides.emplace_back(JsonValue::Object{
            {"wall",JsonValue(std::string(wallName(item.wall)))},{"segment",number(item.segment)},
            {"palette",number(item.palette)}});
        for(const auto& item:room.decorations) decorations.emplace_back(JsonValue::Object{
            {"id",JsonValue(item.id)},{"assetId",JsonValue(item.asset_id)},
            {"column",number(item.cell.column)},{"row",number(item.cell.row)},
            {"yawQuarterTurns",number(item.yaw_quarter_turns)}});
        for(const auto& item:room.transitions) transitions.emplace_back(JsonValue::Object{
            {"id",JsonValue(item.id)},{"kind",JsonValue(std::string(roomTransitionKindName(item.kind)))},
            {"lowerColumn",number(item.lower_cell.column)},{"lowerRow",number(item.lower_cell.row)},
            {"upperColumn",number(item.upper_cell.column)},{"upperRow",number(item.upper_cell.row)}});
        rooms.emplace_back(JsonValue::Object{{"id",JsonValue(room.id)},{"doors",JsonValue(std::move(doors))},
            {"decorations",JsonValue(std::move(decorations))},
            {"transitions",JsonValue(std::move(transitions))},
            {"surfaces",JsonValue(JsonValue::Object{
                {"floorPalette",number(room.surfaces.floor_palette)},
                {"wallPalette",number(room.surfaces.wall_palette)},
                {"floorOverrides",JsonValue(std::move(floor_overrides))},
                {"floorDepthOverrides",JsonValue(std::move(floor_depth_overrides))},
                {"wallOverrides",JsonValue(std::move(wall_overrides))}})},
            {"bounds",JsonValue(JsonValue::Object{{"column",number(room.bounds.column)},{"row",number(room.bounds.row)},
                {"widthCells",number(room.bounds.width)},{"depthCells",number(room.bounds.depth)}})}});
    }
    for (const auto& connection:layout.connections) connections.emplace_back(JsonValue::Object{
        {"id",JsonValue(connection.id)},{"first",endpointJson(connection.first)},{"second",endpointJson(connection.second)}});
    for (const auto& connection:layout.external_connections) external.emplace_back(JsonValue::Object{
        {"id",JsonValue(connection.id)},{"interior",endpointJson(connection.interior)},
        {"destinationMapId",JsonValue(connection.destination_map_id)},{"destinationAnchorId",JsonValue(connection.destination_anchor_id)}});
    return serializeJsonValue(JsonValue(JsonValue::Object{
        {"schema",JsonValue(std::string("pokemon-resort-aquarium-building"))},{"schemaVersion",number(5)},
        {"buildingId",JsonValue(layout.id)},{"revision",number(double(layout.revision))},
        {"rooms",JsonValue(std::move(rooms))},{"connections",JsonValue(std::move(connections))},
        {"externalConnections",JsonValue(std::move(external))}}));
}
} // namespace pr::gameplay::world3d::aquarium::rooms
