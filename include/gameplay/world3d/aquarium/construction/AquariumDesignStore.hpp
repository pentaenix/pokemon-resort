#pragma once

#include "gameplay/world3d/aquarium/construction/AquariumDesign.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace pr::gameplay::world3d::aquarium::construction {

enum class AquariumStoreLoadStatus {
    Missing,
    Loaded,
    RecoveredBackup,
    RecoveredPrevious,
    RecoveredTemporary,
    NewerVersion,
    Invalid,
};

struct AquariumStoreLoadResult {
    AquariumStoreLoadStatus status = AquariumStoreLoadStatus::Missing;
    std::optional<AquariumDesignDocument> document;
    std::string diagnostic;
};

class AquariumDesignStore {
public:
    explicit AquariumDesignStore(std::filesystem::path primary_path);

    const std::filesystem::path& primaryPath() const { return primary_path_; }
    std::filesystem::path backupPath() const;
    std::filesystem::path previousPath() const;
    std::filesystem::path temporaryPath() const;
    AquariumStoreLoadResult load() const;
    AquariumStoreLoadResult loadBackup() const;
    bool saveTransactionally(const AquariumDesignDocument& document, std::string* error = nullptr) const;

private:
    AquariumStoreLoadResult loadOne(const std::filesystem::path& path) const;
    std::filesystem::path primary_path_;
};

} // namespace pr::gameplay::world3d::aquarium::construction
