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
        if (version>1) return {LayoutLoadStatus::NewerVersion,std::nullopt,"Newer building schema; preserve without overwrite"};
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
        for (const auto& door:room.doors) doors.emplace_back(JsonValue::Object{
            {"id",JsonValue(door.id)},{"wall",JsonValue(std::string(wallName(door.wall)))},{"offsetCells",number(door.offset)}});
        rooms.emplace_back(JsonValue::Object{{"id",JsonValue(room.id)},{"doors",JsonValue(std::move(doors))},
            {"bounds",JsonValue(JsonValue::Object{{"column",number(room.bounds.column)},{"row",number(room.bounds.row)},
                {"widthCells",number(room.bounds.width)},{"depthCells",number(room.bounds.depth)}})}});
    }
    for (const auto& connection:layout.connections) connections.emplace_back(JsonValue::Object{
        {"id",JsonValue(connection.id)},{"first",endpointJson(connection.first)},{"second",endpointJson(connection.second)}});
    for (const auto& connection:layout.external_connections) external.emplace_back(JsonValue::Object{
        {"id",JsonValue(connection.id)},{"interior",endpointJson(connection.interior)},
        {"destinationMapId",JsonValue(connection.destination_map_id)},{"destinationAnchorId",JsonValue(connection.destination_anchor_id)}});
    return serializeJsonValue(JsonValue(JsonValue::Object{
        {"schema",JsonValue(std::string("pokemon-resort-aquarium-building"))},{"schemaVersion",number(1)},
        {"buildingId",JsonValue(layout.id)},{"revision",number(double(layout.revision))},
        {"rooms",JsonValue(std::move(rooms))},{"connections",JsonValue(std::move(connections))},
        {"externalConnections",JsonValue(std::move(external))}}));
}
} // namespace pr::gameplay::world3d::aquarium::rooms
