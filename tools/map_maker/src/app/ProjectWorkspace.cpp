#include "mapmaker/app/ProjectWorkspace.hpp"

#include "mapmaker/document/MapMetadataEditing.hpp"

#include <stdexcept>
#include <utility>

namespace pr::mapmaker {

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

std::filesystem::path ProjectWorkspace::mapPath(const MapProjectEntry& entry) const {
    // Legacy web projects keep OWMAP and RTPKS files in the game asset roots.
    // Canonical game projects may keep maps next to their project JSON.
    const std::filesystem::path next_to_project = project_path_.parent_path() / entry.file;
    if (std::filesystem::is_regular_file(next_to_project)) return next_to_project;
    return pokemon_resort_root_ / "assets" / "overworld" / "maps" / entry.file;
}

std::vector<ValidationDiagnostic> ProjectWorkspace::validate() const {
    std::vector<MapValidationProjection> maps;
    maps.reserve(sources_.size());
    for (const auto& [key, source] : sources_) {
        maps.push_back(projectValidation(source.document, key));
    }
    return validateMapProject(project_, maps);
}

bool ProjectWorkspace::dirty() const {
    for (const auto& [key, source] : sources_) {
        (void)key;
        if (source.commands.isDirty()) return true;
    }
    return false;
}

void ProjectWorkspace::saveActive() {
    OpenMapSource* source = activeSource();
    if (!source) throw std::runtime_error("No active map source to save");
    source->document.saveAtomic(source->path);
    source->commands.markClean();
}

void ProjectWorkspace::saveAll() {
    for (auto& [key, source] : sources_) {
        (void)key;
        if (!source.commands.isDirty()) continue;
        source.document.saveAtomic(source.path);
        source.commands.markClean();
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
