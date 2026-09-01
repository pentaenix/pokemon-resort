add_library(aquarium_geometry STATIC
    src/CellRegions.cpp
    src/Footprint.cpp
    src/GeometryBuilder.cpp
    src/Kernel.cpp
    src/Tunnel.cpp
    src/TunnelGeometry.cpp
)

target_include_directories(aquarium_geometry
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(aquarium_geometry PUBLIC cxx_std_17)

add_executable(aquarium_geometry_tests
    tests/KernelTests.cpp
)
target_link_libraries(aquarium_geometry_tests PRIVATE aquarium_geometry)
target_compile_definitions(aquarium_geometry_tests PRIVATE
    AQK_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/goldens"
)
add_test(NAME aquarium_geometry_tests COMMAND aquarium_geometry_tests)
add_test(
    NAME aquarium_geometry_boundary_check
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_boundaries.sh
)

add_executable(aquarium_geometry_benchmark
    tests/Benchmark.cpp
)
target_link_libraries(aquarium_geometry_benchmark PRIVATE aquarium_geometry)
