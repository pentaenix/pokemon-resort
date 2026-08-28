#include "AquariumGeometryJsonAdapter.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: aquarium_geometry_golden_dump <design.json>\n";
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
        std::cerr << "could not open aquarium design: " << argv[1] << '\n';
        return 2;
    }
    std::ostringstream text;
    text << input.rdbuf();
    try {
        std::cout << pr::aquarium::geometry::wasm::buildAquariumDocumentJson(text.str()) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "aquarium_geometry_golden_dump: " << error.what() << '\n';
        return 1;
    }
}
