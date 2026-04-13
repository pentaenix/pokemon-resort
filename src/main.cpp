#include "core/App.hpp"

#include <string>

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--clear-save-cache") {
        const char* config_override = argc > 2 ? argv[2] : nullptr;
        return pr::clearTransferSaveCache(config_override);
    }

    const char* config_override = argc > 1 ? argv[1] : nullptr;
    return pr::runApplication(argv[0], config_override);
}
