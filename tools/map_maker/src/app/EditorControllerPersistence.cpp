#include "mapmaker/app/EditorController.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace pr::mapmaker {

std::filesystem::path EditorController::previewPath() const {
    const OpenMapSource* source = workspace_->activeSource();
    const std::string name = source ? source->key : "active";
    std::string safe = name;
    std::replace_if(safe.begin(), safe.end(), [](char value) {
        return !std::isalnum(static_cast<unsigned char>(value)) && value != '-' && value != '_';
    }, '_');
    return resort_root_ / "build" / "map-maker-state" / "preview" / (safe + ".owmap");
}

bool EditorController::reloadPreview() {
    if (!preview_reload_pending_) return true;
    OpenMapSource* source = workspace_->activeSource();
    if (!source) return false;
    try {
        const auto path = previewPath();
        std::filesystem::create_directories(path.parent_path());
        const auto bytes = source->document.serialize();
        const auto temporary = path.string() + ".tmp";
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) throw std::runtime_error("Could not open preview temporary file");
        stream.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        stream.close();
        (void)OwmapDocument::load(temporary);
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::rename(temporary, path);
        if (!preview_->loadMap(path)) throw std::runtime_error(preview_->lastError());
        preview_source_key_ = source->key;
        preview_reload_pending_ = false;
        return true;
    } catch (const std::exception& exception) {
        status_ = "Preview reload failed: " + std::string(exception.what());
        log(LogLevel::Error, "preview", status_);
        preview_reload_pending_ = false;
        return false;
    }
}

void EditorController::tickRecovery() {
    for (OpenMapSource* source : workspace_->sources()) {
        std::string error;
        if (!recovery_->writeIfDue(
                source->key, source->document, source->dirty(), &error) &&
            !error.empty()) {
            log(LogLevel::Warning, "recovery", source->key + ": " + error);
        }
    }
}

void EditorController::log(
    LogLevel level, const std::string& category, const std::string& message) {
    (void)logger_->log(level, category, message);
}

} // namespace pr::mapmaker
