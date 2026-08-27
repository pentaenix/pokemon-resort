#include "mapmaker/app/ProjectWorkspace.hpp"

#include "mapmaker/document/MapMetadataEditing.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>

namespace pr::mapmaker {
namespace {

class ProjectMutationCommand final : public EditorCommand {
public:
    ProjectMutationCommand(
        MapProjectDocument& document,
        std::string label,
        const std::function<void(MapProjectDocument&)>& mutation)
        : document_(&document), label_(std::move(label)), before_(document) {
        after_ = before_;
        mutation(after_);
    }

    const std::string& label() const override { return label_; }
    void apply() override { *document_ = after_; }
    void revert() override { *document_ = before_; }
    bool isNoop() const override { return before_.serialize() == after_.serialize(); }

private:
    MapProjectDocument* document_ = nullptr;
    std::string label_;
    MapProjectDocument before_;
    MapProjectDocument after_;
};

JsonValue emptyCells(int width, int height) {
    JsonValue::Array rows;
    rows.reserve(static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        JsonValue::Array row(static_cast<std::size_t>(width), JsonValue(nullptr));
        rows.emplace_back(std::move(row));
    }
    return JsonValue(std::move(rows));
}

JsonValue initialMetadata(const NewMapSpec& spec) {
    JsonValue::Object grid;
    grid.emplace("enabled", JsonValue(true));
    grid.emplace("tileSize", JsonValue(16.0));
    grid.emplace("width", JsonValue(static_cast<double>(spec.width)));
    grid.emplace("height", JsonValue(static_cast<double>(spec.height)));

    JsonValue::Object player;
    player.emplace("character", JsonValue("assets/characters/playable/haru.charbin"));
    JsonValue::Array spawn{
        JsonValue(static_cast<double>(std::max(0, spec.width / 2))),
        JsonValue(static_cast<double>(std::max(0, spec.height / 2)))};
    player.emplace("spawnTile", JsonValue(std::move(spawn)));
    player.emplace("spawnHeight", JsonValue(0.0));
    player.emplace("facing", JsonValue("south"));

    JsonValue::Object layer;
    layer.emplace("id", JsonValue("ground"));
    layer.emplace("name", JsonValue("Ground"));
    layer.emplace("visible", JsonValue(true));
    layer.emplace("cells", emptyCells(spec.width, spec.height));
    JsonValue::Array layers;
    layers.emplace_back(std::move(layer));
    JsonValue::Object tile_layers;
    tile_layers.emplace("version", JsonValue(1.0));
    tile_layers.emplace("activeLayer", JsonValue(0.0));
    tile_layers.emplace("layers", JsonValue(std::move(layers)));

    JsonValue::Object environment;
    if (spec.type == "interior") {
        environment.emplace("space", JsonValue("interior:" + spec.id));
        JsonValue::Array clear{
            JsonValue(0.0), JsonValue(0.0), JsonValue(0.0), JsonValue(255.0)};
        environment.emplace("clearColor", JsonValue(std::move(clear)));
        environment.emplace("renderOtherSpaces", JsonValue(false));
    }

    JsonValue::Object root;
    root.emplace("id", JsonValue(spec.id));
    root.emplace("name", JsonValue(spec.name.empty() ? spec.id : spec.name));
    root.emplace("type", JsonValue(spec.type));
    root.emplace("visual", JsonValue(JsonValue::Object{
        {"mesh", JsonValue("")},
        {"format", JsonValue("none")},
        {"material", JsonValue("")},
        {"textureDirectory", JsonValue("")},
        {"origin", JsonValue(JsonValue::Array{
            JsonValue(0.0), JsonValue(0.0), JsonValue(0.0)})},
        {"scale", JsonValue(1.0)}}));
    root.emplace("grid", JsonValue(std::move(grid)));
    root.emplace("player", JsonValue(std::move(player)));
    root.emplace("camera", JsonValue(JsonValue::Object{{"preset",
        JsonValue("gen4_platinum_default_exterior")}}));
    root.emplace("lighting", JsonValue(JsonValue::Object{{"preset",
        JsonValue("gen4_default_exterior")}, {"brightness", JsonValue(0.95)}}));
    root.emplace("collision", JsonValue(JsonValue::Object{{"enabled", JsonValue(true)}}));
    root.emplace("characters", JsonValue(JsonValue::Array{}));
    root.emplace("models", JsonValue(JsonValue::Array{}));
    root.emplace("tilePackage", JsonValue(JsonValue::Object{
        {"file", JsonValue("maptiles.rtpks")},
        {"packId", JsonValue("map_tiles")},
        {"name", JsonValue("Map Tiles")}}));
    root.emplace("tileLayers", JsonValue(std::move(tile_layers)));
    JsonValue::Array anchors;
    if (spec.type == "interior") {
        const int center_x = std::max(0, spec.width / 2);
        const int south_y = std::max(0, spec.height - 1);
        const auto append_anchor = [&](const char* id, int tile_x) {
            JsonValue::Object anchor;
            anchor.emplace("id", JsonValue(id));
            anchor.emplace("tile", JsonValue(JsonValue::Array{
                JsonValue(static_cast<double>(tile_x)),
                JsonValue(static_cast<double>(south_y))}));
            anchor.emplace("facing", JsonValue("north"));
            anchors.emplace_back(std::move(anchor));
        };
        append_anchor("entry_left", center_x - 1);
        append_anchor("entry", center_x);
        append_anchor("entry_right", center_x + 1);
    }
    root.emplace("anchors", JsonValue(std::move(anchors)));
    root.emplace("links", JsonValue(JsonValue::Array{}));
    root.emplace("doorTriggers", JsonValue(JsonValue::Array{}));
    if (spec.type == "interior") {
        const int center_x = std::max(0, spec.width / 2);
        JsonValue::Object room;
        room.emplace("enabled", JsonValue(true));
        room.emplace("wallHeightTiles", JsonValue(4.0));
        room.emplace("frontWallHeightTiles", JsonValue(0.35));
        room.emplace("trimHeightTiles", JsonValue(0.125));
        room.emplace("walkableInsetTiles", JsonValue(1.0));
        room.emplace("wallFaceOffsetTiles", JsonValue(0.5));
        room.emplace("entryExtensionDepthTiles", JsonValue(0.0));
        room.emplace("blackTopCap", JsonValue(true));
        room.emplace("topCapDepthTiles", JsonValue(0.125));
        JsonValue::Object interior;
        interior.emplace("shellModelId", JsonValue(""));
        interior.emplace("floorDatum", JsonValue(0.0));
        interior.emplace("gridOrigin", JsonValue(JsonValue::Array{
            JsonValue(0.0), JsonValue(0.0)}));
        JsonValue::Object opening;
        opening.emplace("edge", JsonValue("south"));
        opening.emplace("from", JsonValue(static_cast<double>(std::max(0, center_x - 1))));
        opening.emplace("to", JsonValue(static_cast<double>(
            std::min(std::max(0, spec.width - 1), center_x + 1))));
        interior.emplace("openings", JsonValue(JsonValue::Array{
            JsonValue(std::move(opening))}));
        interior.emplace("defaultRoom", JsonValue(std::move(room)));
        root.emplace("interior", JsonValue(std::move(interior)));
    }
    if (!environment.empty()) root.emplace("environment", JsonValue(std::move(environment)));
    return JsonValue(std::move(root));
}

std::string portableMapFileName(const std::string& id) {
    std::string result;
    result.reserve(id.size() + 6U);
    for (unsigned char character : id) {
        if (std::isalnum(character) || character == '_' || character == '-') {
            result.push_back(static_cast<char>(character));
        } else {
            result.push_back('_');
        }
    }
    if (result.empty()) result = "map";
    return result + ".owmap";
}

} // namespace

ProjectWorkspace ProjectWorkspace::open(
    const std::filesystem::path& pokemon_resort_root,
    const std::filesystem::path& project_path) {
    ProjectWorkspace workspace;
    workspace.pokemon_resort_root_ = pokemon_resort_root;
    workspace.project_path_ = project_path;
    workspace.project_ = MapProjectDocument::load(project_path);
    for (const MapSourceGroup& group : workspace.project_.sourceGroups()) {
        if (group.file.empty()) continue;
        OpenMapSource source;
        source.key = group.key;
        const MapProjectEntry* representative = group.entry_ids.empty()
            ? nullptr : workspace.project_.findMap(group.entry_ids.front());
        if (!representative) continue;
        source.path = workspace.mapPath(*representative);
        source.document = OwmapDocument::loadWithRecovery(source.path);
        source.commands.markClean();
        workspace.sources_.emplace(source.key, std::move(source));
    }
    const std::string preferred = workspace.project_.editor().active_map_id;
    workspace.active_map_id_ = workspace.project_.findMap(preferred)
        ? preferred
        : (workspace.project_.maps().empty() ? std::string{} : workspace.project_.maps().front().id);
    return workspace;
}

const MapProjectEntry* ProjectWorkspace::activeMapEntry() const {
    return project_.findMap(active_map_id_);
}

const OpenMapSource* ProjectWorkspace::sourceForMap(const std::string& map_id) const {
    const std::string key = project_.sourceKeyFor(map_id);
    const auto found = sources_.find(key);
    return found == sources_.end() ? nullptr : &found->second;
}

OpenMapSource* ProjectWorkspace::activeSource() {
    const std::string key = project_.sourceKeyFor(active_map_id_);
    const auto found = sources_.find(key);
    return found == sources_.end() ? nullptr : &found->second;
}

const OpenMapSource* ProjectWorkspace::activeSource() const {
    const std::string key = project_.sourceKeyFor(active_map_id_);
    const auto found = sources_.find(key);
    return found == sources_.end() ? nullptr : &found->second;
}

bool ProjectWorkspace::activateMap(const std::string& map_id) {
    if (!project_.findMap(map_id)) return false;
    active_map_id_ = map_id;
    return activeSource() != nullptr;
}

bool ProjectWorkspace::executeProjectMutation(
    std::string label,
    const std::function<void(MapProjectDocument&)>& mutation) {
    return project_commands_.execute(std::make_unique<ProjectMutationCommand>(
        project_, std::move(label), mutation));
}

bool ProjectWorkspace::moveMap(const std::string& map_id, int grid_x, int grid_y) {
    return executeProjectMutation("Move map", [=](MapProjectDocument& project) {
        (void)project.moveMap(map_id, grid_x, grid_y);
    });
}

std::pair<int, int> ProjectWorkspace::suggestedStandaloneMapPosition() const {
    if (project_.maps().empty()) return {0, 0};
    const int rightmost = std::max_element(
        project_.maps().begin(), project_.maps().end(),
        [](const MapProjectEntry& left, const MapProjectEntry& right) {
            return left.grid_x < right.grid_x;
        })->grid_x;
    int grid_y = 0;
    const int grid_x = rightmost + 2;
    while (std::any_of(project_.maps().begin(), project_.maps().end(),
        [=](const MapProjectEntry& entry) {
            return entry.grid_x == grid_x && entry.grid_y == grid_y;
        })) {
        ++grid_y;
    }
    return {grid_x, grid_y};
}

bool ProjectWorkspace::createMap(const NewMapSpec& requested, std::string* error) {
    try {
        NewMapSpec spec = requested;
        spec.width = std::clamp(spec.width, 1, static_cast<int>(OwmapDocument::kMaxDimension));
        spec.height = std::clamp(spec.height, 1, static_cast<int>(OwmapDocument::kMaxDimension));
        if (spec.id.empty()) throw std::runtime_error("Map ID is required");
        if (project_.findMap(spec.id)) throw std::runtime_error("Map ID already exists");
        const std::string file = portableMapFileName(spec.id);
        const std::filesystem::path path =
            pokemon_resort_root_ / "assets" / "overworld" / "maps" / file;
        if (std::filesystem::exists(path)) {
            throw std::runtime_error("Map file already exists: " + path.string());
        }
        OwmapDocument document = OwmapDocument::create(
            static_cast<std::uint16_t>(spec.width),
            static_cast<std::uint16_t>(spec.height),
            16.0F,
            initialMetadata(spec));
        MapProjectEntry entry;
        entry.id = spec.id;
        entry.name = spec.name.empty() ? spec.id : spec.name;
        entry.file = file;
        entry.grid_x = spec.grid_x;
        entry.grid_y = spec.grid_y;
        entry.linked = spec.linked && spec.type != "interior";
        if (!executeProjectMutation("Create map", [entry](MapProjectDocument& project) {
                (void)project.addMap(entry);
            })) {
            throw std::runtime_error("Could not add map to project");
        }
        OpenMapSource source;
        source.key = normalizedMapSourceKey(file);
        source.path = path;
        source.document = std::move(document);
        source.commands.markClean();
        source.newly_created = true;
        sources_.insert_or_assign(source.key, std::move(source));
        active_map_id_ = spec.id;
        if (error) error->clear();
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    }
}

bool ProjectWorkspace::createMapInstance(
    const std::string& id,
    const std::string& name,
    const std::string& source_map_id,
    int grid_x,
    int grid_y,
    std::string* error) {
    try {
        if (id.empty()) throw std::runtime_error("Map ID is required");
        if (project_.findMap(id)) throw std::runtime_error("Map ID already exists");
        const MapProjectEntry* source = project_.findMap(source_map_id);
        if (!source) throw std::runtime_error("Reusable source map does not exist");
        MapProjectEntry entry;
        entry.id = id;
        entry.name = name.empty() ? id : name;
        entry.file = source->file;
        entry.source_map_id = source_map_id;
        entry.grid_x = grid_x;
        entry.grid_y = grid_y;
        entry.linked = source->linked;
        if (!executeProjectMutation("Create reused map", [entry](MapProjectDocument& project) {
                (void)project.addMap(entry);
            })) {
            throw std::runtime_error("Could not add reused map to project");
        }
        active_map_id_ = id;
        if (error) error->clear();
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    }
}

bool ProjectWorkspace::undoProject() {
    const bool changed = project_commands_.undo();
    if (changed && !project_.findMap(active_map_id_) && !project_.maps().empty()) {
        active_map_id_ = project_.maps().front().id;
    }
    return changed;
}

bool ProjectWorkspace::redoProject() { return project_commands_.redo(); }

std::filesystem::path ProjectWorkspace::mapPath(const MapProjectEntry& entry) const {
    // Legacy web projects keep OWMAP and RTPKS files in the game asset roots.
    // Canonical game projects may keep maps next to their project JSON.
    const std::filesystem::path next_to_project = project_path_.parent_path() / entry.file;
    if (std::filesystem::is_regular_file(next_to_project)) return next_to_project;
    return pokemon_resort_root_ / "assets" / "overworld" / "maps" / entry.file;
}

std::vector<ValidationDiagnostic> ProjectWorkspace::validate() const {
    std::vector<MapValidationProjection> maps;
    const auto groups = project_.sourceGroups();
    maps.reserve(groups.size());
    for (const MapSourceGroup& group : groups) {
        const auto source = sources_.find(group.key);
        if (source != sources_.end()) {
            maps.push_back(projectValidation(source->second.document, group.key));
        }
    }
    return validateMapProject(project_, maps);
}

bool ProjectWorkspace::dirty() const {
    if (project_commands_.isDirty()) return true;
    for (const auto& [key, source] : sources_) {
        const bool referenced = std::any_of(project_.maps().begin(), project_.maps().end(),
            [&](const MapProjectEntry& entry) {
                return normalizedMapSourceKey(entry.file) == key;
            });
        if (!referenced) continue;
        if (source.dirty()) return true;
    }
    return false;
}

void ProjectWorkspace::saveActive() {
    OpenMapSource* source = activeSource();
    if (!source) throw std::runtime_error("No active map source to save");
    source->document.saveAtomic(source->path);
    source->commands.markClean();
    source->newly_created = false;
}

void ProjectWorkspace::saveAll() {
    for (auto& [key, source] : sources_) {
        (void)key;
        const bool referenced = std::any_of(project_.maps().begin(), project_.maps().end(),
            [&](const MapProjectEntry& entry) {
                return normalizedMapSourceKey(entry.file) == key;
            });
        if (!referenced || !source.dirty()) continue;
        source.document.saveAtomic(source.path);
        source.commands.markClean();
        source.newly_created = false;
    }
    if (project_commands_.isDirty()) {
        project_.saveAtomic(project_path_);
        project_commands_.markClean();
    }
}

std::vector<OpenMapSource*> ProjectWorkspace::sources() {
    std::vector<OpenMapSource*> result;
    result.reserve(sources_.size());
    for (auto& [key, source] : sources_) {
        (void)key;
        result.push_back(&source);
    }
    return result;
}

std::vector<const OpenMapSource*> ProjectWorkspace::sources() const {
    std::vector<const OpenMapSource*> result;
    result.reserve(sources_.size());
    for (const auto& [key, source] : sources_) {
        (void)key;
        result.push_back(&source);
    }
    return result;
}

} // namespace pr::mapmaker
