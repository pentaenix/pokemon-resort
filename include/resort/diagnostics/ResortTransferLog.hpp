#pragma once

#include <filesystem>
#include <string>

namespace pr::resort {

/// Appends one JSON object per line to `<db_stem>.transfer.jsonl` beside the Resort profile database.
/// Invalidates silently if the path is not writable (diagnostics only).
void appendResortTransferJsonl(
    const std::filesystem::path& profile_database_file,
    const std::string& json_object_one_line);

} // namespace pr::resort
