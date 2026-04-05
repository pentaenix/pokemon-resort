#include "core/App.hpp"

int main(int argc, char** argv) {
    const char* config_override = argc > 1 ? argv[1] : nullptr;
    return pr::runApplication(argv[0], config_override);
}
