#include "resort/diagnostics/ResortTransferLog.hpp"

#include <fstream>

namespace pr::resort {

namespace {

std::filesystem::path transferLogPathForDb(const std::filesystem::path& profile_database_file) {
    return profile_database_file.parent_path() /
           (profile_database_file.stem().string() + ".transfer.jsonl");
}

} // namespace

void appendResortTransferJsonl(
    const std::filesystem::path& profile_database_file,
    const std::string& json_object_one_line) {
    if (json_object_one_line.find('\n') != std::string::npos) {
        return;
    }
    std::ofstream f(transferLogPathForDb(profile_database_file), std::ios::app);
    if (!f) {
        return;
    }
    f << json_object_one_line << '\n';
}

} // namespace pr::resort
