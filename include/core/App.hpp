#pragma once

namespace pr {

int runApplication(const char* argv0, const char* config_path_override);
int clearTransferSaveCache(const char* config_path_override);

} // namespace pr
