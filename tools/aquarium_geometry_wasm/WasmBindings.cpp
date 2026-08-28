#include "AquariumGeometryJsonAdapter.hpp"

#include <emscripten/bind.h>

EMSCRIPTEN_BINDINGS(aquarium_geometry_kernel) {
    emscripten::function(
        "buildAquariumDocument",
        &pr::aquarium::geometry::wasm::buildAquariumDocumentJson);
}
