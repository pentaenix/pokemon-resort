#pragma once

#include "mapmaker/commands/CommandStack.hpp"
#include "mapmaker/document/OwmapDocument.hpp"
#include "mapmaker/project/MapProjectDocument.hpp"
#include "mapmaker/validation/ProjectValidator.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pr::mapmaker {

struct OpenMapSource {
    std::string key;
    std::filesystem::path path;
    OwmapDocument document;
    CommandStack commands;
    bool newly_created = false;
    bool dirty() const { return newly_created || commands.isDirty(); }
};

struct NewMapSpec {
    std::string id;
    std::string name;
    std::string type = "exterior";
    int width = 32;
    int height = 32;
    int grid_x = 0;
    int grid_y = 0;
    bool linked = true;
};

class ProjectWorkspace {
public:
    static ProjectWorkspace open(
        const std::filesystem::path& pokemon_resort_root,
        const std::filesystem::path& project_path);

    const MapProjectDocument& project() const { return project_; }
    const std::filesystem::path& projectPath() const { return project_path_; }
    const std::string& activeMapId() const { return active_map_id_; }
    const MapProjectEntry* activeMapEntry() const;
    const OpenMapSource* sourceForMap(const std::string& map_id) const;
    OpenMapSource* activeSource();
    const OpenMapSource* activeSource() const;
    bool activateMap(const std::string& map_id);
    bool moveMap(const std::string& map_id, int grid_x, int grid_y);
    std::pair<int, int> suggestedStandaloneMapPosition() const;
    bool createMap(const NewMapSpec& spec, std::string* error = nullptr);
    bool createMapInstance(
        const std::string& id,
        const std::string& name,
        const std::string& source_map_id,
        int grid_x,
        int grid_y,
        std::string* error = nullptr);
    bool undoProject();
    bool redoProject();
    bool canUndoProject() const { return project_commands_.canUndo(); }
    bool canRedoProject() const { return project_commands_.canRedo(); }
    bool projectDirty() const { return project_commands_.isDirty(); }

    std::filesystem::path mapPath(const MapProjectEntry& entry) const;
    std::vector<ValidationDiagnostic> validate() const;
    bool dirty() const;
    void saveActive();
    void saveAll();

    std::vector<OpenMapSource*> sources();
    std::vector<const OpenMapSource*> sources() const;

private:
    bool executeProjectMutation(
        std::string label,
        const std::function<void(MapProjectDocument&)>& mutation);

    std::filesystem::path pokemon_resort_root_;
    std::filesystem::path project_path_;
    MapProjectDocument project_;
    std::string active_map_id_;
    std::unordered_map<std::string, OpenMapSource> sources_;
    CommandStack project_commands_;
};

} // namespace pr::mapmaker
