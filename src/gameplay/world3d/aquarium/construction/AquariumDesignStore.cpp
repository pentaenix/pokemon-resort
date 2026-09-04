#include "gameplay/world3d/aquarium/construction/AquariumDesignStore.hpp"

#include <fstream>
#include <sstream>
#include <system_error>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace pr::gameplay::world3d::aquarium::construction {
namespace fs = std::filesystem;

namespace {

bool syncPath(const fs::path& path, bool directory, std::string* error) {
#if defined(_WIN32)
    (void)path;
    (void)directory;
    (void)error;
    return true;
#else
    int flags = O_RDONLY;
#if defined(O_DIRECTORY)
    if (directory) flags |= O_DIRECTORY;
#else
    (void)directory;
#endif
    const int descriptor = ::open(path.c_str(), flags);
    if (descriptor < 0) {
        if (error) *error = "Could not open aquarium save for durable sync";
        return false;
    }
    const bool okay = ::fsync(descriptor) == 0;
    ::close(descriptor);
    if (!okay && error) *error = "Could not durably sync aquarium save";
    return okay;
#endif
}

} // namespace

AquariumDesignStore::AquariumDesignStore(fs::path primary_path)
    : primary_path_(std::move(primary_path)) {}

fs::path AquariumDesignStore::backupPath() const {
    return fs::path(primary_path_.string() + ".bak");
}

fs::path AquariumDesignStore::previousPath() const {
    return fs::path(primary_path_.string() + ".previous");
}

fs::path AquariumDesignStore::temporaryPath() const {
    return fs::path(primary_path_.string() + ".tmp");
}

AquariumStoreLoadResult AquariumDesignStore::loadOne(const fs::path& path) const {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {AquariumStoreLoadStatus::Missing, std::nullopt, {}};
    std::ostringstream text;
    text << input.rdbuf();
    const AquariumDesignLoadResult parsed = parseAquariumDesign(text.str());
    if (parsed.status == AquariumDesignLoadStatus::Loaded && parsed.document) {
        return {AquariumStoreLoadStatus::Loaded, parsed.document, {}};
    }
    std::string diagnostic;
    for (const std::string& item : parsed.diagnostics) {
        if (!diagnostic.empty()) diagnostic += "; ";
        diagnostic += item;
    }
    return {
        parsed.status == AquariumDesignLoadStatus::NewerVersion
            ? AquariumStoreLoadStatus::NewerVersion
            : AquariumStoreLoadStatus::Invalid,
        std::nullopt,
        std::move(diagnostic),
    };
}

AquariumStoreLoadResult AquariumDesignStore::load() const {
    AquariumStoreLoadResult primary = loadOne(primary_path_);
    if (primary.status == AquariumStoreLoadStatus::Loaded ||
        primary.status == AquariumStoreLoadStatus::NewerVersion) {
        return primary;
    }
    AquariumStoreLoadResult backup = loadOne(backupPath());
    if (backup.status == AquariumStoreLoadStatus::NewerVersion) return backup;
    AquariumStoreLoadResult previous = loadOne(previousPath());
    if (previous.status == AquariumStoreLoadStatus::NewerVersion) return previous;
    AquariumStoreLoadResult temporary = loadOne(temporaryPath());
    if (temporary.status == AquariumStoreLoadStatus::NewerVersion) return temporary;

    if (backup.status == AquariumStoreLoadStatus::Loaded) {
        backup.status = AquariumStoreLoadStatus::RecoveredBackup;
        return backup;
    }
    if (previous.status == AquariumStoreLoadStatus::Loaded) {
        previous.status = AquariumStoreLoadStatus::RecoveredPrevious;
        return previous;
    }
    if (temporary.status == AquariumStoreLoadStatus::Loaded) {
        temporary.status = AquariumStoreLoadStatus::RecoveredTemporary;
        return temporary;
    }

    AquariumStoreLoadResult result = primary;
    const auto appendDiagnostic = [&result](const char* source, const AquariumStoreLoadResult& candidate) {
        if (candidate.diagnostic.empty()) return;
        if (!result.diagnostic.empty()) result.diagnostic += "; ";
        result.diagnostic += source;
        result.diagnostic += ": ";
        result.diagnostic += candidate.diagnostic;
    };
    appendDiagnostic("backup", backup);
    appendDiagnostic("previous", previous);
    appendDiagnostic("temporary", temporary);
    if (result.status == AquariumStoreLoadStatus::Missing &&
        (backup.status == AquariumStoreLoadStatus::Invalid ||
         previous.status == AquariumStoreLoadStatus::Invalid ||
         temporary.status == AquariumStoreLoadStatus::Invalid)) {
        result.status = AquariumStoreLoadStatus::Invalid;
    }
    return result;
}

AquariumStoreLoadResult AquariumDesignStore::loadBackup() const {
    return loadOne(backupPath());
}

bool AquariumDesignStore::saveTransactionally(
    const AquariumDesignDocument& document,
    std::string* error) const {
    const std::vector<std::string> diagnostics = validateAquariumDesign(document);
    if (!diagnostics.empty()) {
        if (error) *error = diagnostics.front();
        return false;
    }
    for (const fs::path& candidate :
         {primary_path_, backupPath(), previousPath(), temporaryPath()}) {
        if (loadOne(candidate).status == AquariumStoreLoadStatus::NewerVersion) {
            if (error) *error = "A newer aquarium save artifact must be preserved";
            return false;
        }
    }
    std::error_code ec;
    fs::create_directories(primary_path_.parent_path(), ec);
    if (ec) {
        if (error) *error = "Could not create aquarium save directory: " + ec.message();
        return false;
    }
    const fs::path temporary = temporaryPath();
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            if (error) *error = "Could not open aquarium temporary save";
            return false;
        }
        output << serializeAquariumDesignCanonical(document);
        output.flush();
        if (!output) {
            if (error) *error = "Could not flush aquarium temporary save";
            return false;
        }
    }
    if (!syncPath(temporary, false, error)) {
        fs::remove(temporary, ec);
        return false;
    }
    const AquariumStoreLoadResult verified = loadOne(temporary);
    if (verified.status != AquariumStoreLoadStatus::Loaded || !verified.document ||
        verified.document->revision != document.revision) {
        fs::remove(temporary, ec);
        if (error) *error = "Aquarium temporary save failed read-back validation";
        return false;
    }
    const fs::path backup = backupPath();
    const AquariumStoreLoadResult current_primary = loadOne(primary_path_);
    if (current_primary.status == AquariumStoreLoadStatus::Loaded) {
        fs::copy_file(primary_path_, backup, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            fs::remove(temporary, ec);
            if (error) *error = "Could not update aquarium backup: " + ec.message();
            return false;
        }
    } else if (loadOne(backup).status != AquariumStoreLoadStatus::Loaded) {
        fs::copy_file(temporary, backup, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            fs::remove(temporary, ec);
            if (error) *error = "Could not establish aquarium recovery backup: " + ec.message();
            return false;
        }
    }
    if (!syncPath(backup, false, error)) {
        fs::remove(temporary, ec);
        return false;
    }
    fs::rename(temporary, primary_path_, ec);
    if (ec) {
        // Windows does not replace an existing destination. Preserve the primary,
        // then use a reversible two-rename promotion.
        const fs::path displaced = previousPath();
        std::error_code fallback_ec;
        fs::remove(displaced, fallback_ec);
        fallback_ec.clear();
        fs::rename(primary_path_, displaced, fallback_ec);
        if (fallback_ec) {
            if (error) *error = "Could not promote aquarium save: " + fallback_ec.message();
            return false;
        }
        fs::rename(temporary, primary_path_, fallback_ec);
        if (fallback_ec) {
            std::error_code restore_ec;
            fs::rename(displaced, primary_path_, restore_ec);
            if (error) *error = "Could not promote aquarium save: " + fallback_ec.message();
            return false;
        }
        fs::remove(displaced, fallback_ec);
    }
    const AquariumStoreLoadResult promoted = loadOne(primary_path_);
    if (promoted.status != AquariumStoreLoadStatus::Loaded || !promoted.document ||
        promoted.document->revision != document.revision) {
        if (error) *error = "Promoted aquarium save failed validation";
        return false;
    }
    std::string directory_sync_error;
    (void)syncPath(primary_path_.parent_path(), true, &directory_sync_error);
    if (error) error->clear();
    return true;
}

} // namespace pr::gameplay::world3d::aquarium::construction
