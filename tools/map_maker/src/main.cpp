#include "mapmaker/app/MapMakerApp.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void printHelp() {
    std::cout
        << "Pokemon Resort Map Maker\n\n"
        << "Usage: pokemon_resort_map_maker [options]\n"
        << "  --project <path>       Open an explicit map_project.json\n"
        << "  --map <id>             Focus a project map on startup\n"
        << "  --renderer <name>      Override bgfx backend (metal, vulkan, opengl)\n"
        << "  --validate-project     Validate all unique map sources without a window\n"
        << "  --smoke-test           Run twelve real native frames and exit\n"
        << "  --help                  Show this help\n";
}

std::string argumentValue(int& index, int argc, char** argv, const std::string& option) {
    if (index + 1 >= argc) throw std::runtime_error(option + " requires a value");
    return argv[++index];
}

} // namespace

int main(int argc, char** argv) {
    pr::mapmaker::MapMakerOptions options;
#ifdef PR_SOURCE_DIR
    options.resort_root = std::filesystem::path(PR_SOURCE_DIR);
#else
    options.resort_root = std::filesystem::current_path();
#endif
    try {
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--project") {
                options.project_path = argumentValue(index, argc, argv, argument);
            } else if (argument == "--map") {
                options.initial_map_id = argumentValue(index, argc, argv, argument);
            } else if (argument == "--renderer") {
                options.renderer = argumentValue(index, argc, argv, argument);
            } else if (argument == "--validate-project") {
                options.validate_project = true;
            } else if (argument == "--smoke-test") {
                options.smoke_test = true;
            } else if (argument == "--help" || argument == "-h") {
                printHelp();
                return 0;
            } else {
                throw std::runtime_error("Unknown option: " + argument);
            }
        }
        return pr::mapmaker::runMapMaker(options);
    } catch (const std::exception& exception) {
        std::cerr << "pokemon_resort_map_maker: " << exception.what() << '\n';
        printHelp();
        return 1;
    }
}
