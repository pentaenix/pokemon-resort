set(PR_MAP_MAKER_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(PR_MAP_MAKER_INCLUDE_DIR ${PR_MAP_MAKER_ROOT}/include)
set(PR_MAP_MAKER_MINIZ_INCLUDE_DIR
    ${bgfx_cmake_SOURCE_DIR}/bimg/3rdparty/tinyexr/deps/miniz)

find_package(Threads REQUIRED)

add_library(pokemon_resort_map_maker_core STATIC
    ${CMAKE_SOURCE_DIR}/src/core/config/Json.cpp
    ${CMAKE_SOURCE_DIR}/src/core/crypto/Sha256.cpp
    src/app/AutosaveRecovery.cpp
    src/app/FrameMetrics.cpp
    src/app/ProjectWorkspace.cpp
    src/assets/EditorAssetCatalog.cpp
    src/commands/CommandStack.cpp
    src/commands/TerrainCommands.cpp
    src/document/DocumentCommands.cpp
    src/document/DoorMetadataEditing.cpp
    src/document/MapMetadataEditing.cpp
    src/document/OwmapDocument.cpp
    src/logging/StructuredLogger.cpp
    src/project/MapProjectDocument.cpp
    src/project/ProjectDiscovery.cpp
    src/selection/SelectionModel.cpp
    src/session/EditorSession.cpp
    src/validation/ProjectValidator.cpp
)
target_include_directories(pokemon_resort_map_maker_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${PR_MAP_MAKER_INCLUDE_DIR}
    PRIVATE
        ${PR_MAP_MAKER_MINIZ_INCLUDE_DIR}
)
target_compile_features(pokemon_resort_map_maker_core PUBLIC cxx_std_20)
target_link_libraries(pokemon_resort_map_maker_core
    PUBLIC
        bimg
        Threads::Threads
)

# This adapter deliberately reuses the shipping overworld loader, camera, and
# renderer. Keeping it separate lets document tests remain fast and headless.
# The shipping renderer calls back into GLB/data helpers while the OWMAP loader
# calls into the renderer-owned RTPKS loader. An object library keeps that
# existing two-way implementation seam link-order independent on macOS.
add_library(pokemon_resort_map_maker_runtime OBJECT
    src/interaction/WorldPicker.cpp
    src/preview/ExactWorldPreview.cpp
    ${CMAKE_SOURCE_DIR}/src/gameplay/world3d/camera/Gen4CameraPreset.cpp
    ${CMAKE_SOURCE_DIR}/src/gameplay/world3d/camera/Gen4FollowCamera.cpp
    ${CMAKE_SOURCE_DIR}/src/gameplay/world3d/data/GlbModelAnimation.cpp
    ${CMAKE_SOURCE_DIR}/src/gameplay/world3d/data/GlbModelLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/gameplay/world3d/data/JsonOverworldLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/gameplay/world3d/data/OwmapOverworldLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/gameplay/world3d/data/SceneMetadataParser.cpp
    ${CMAKE_SOURCE_DIR}/src/core/assets/Font.cpp
    ${CMAKE_SOURCE_DIR}/src/gameplay/world3d/dialogue/OverworldTextboxConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/gameplay/world3d/scripts/OverworldScript.cpp
    ${CMAKE_SOURCE_DIR}/src/gameplay/world3d/terrain/ActorTerrainBinding.cpp
    ${CMAKE_SOURCE_DIR}/src/gameplay/world3d/terrain/GridStepMotor.cpp
)
target_include_directories(pokemon_resort_map_maker_runtime
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${PR_MAP_MAKER_INCLUDE_DIR}
    PRIVATE
        ${PR_MAP_MAKER_MINIZ_INCLUDE_DIR}
)
target_compile_features(pokemon_resort_map_maker_runtime PUBLIC cxx_std_20)
target_link_libraries(pokemon_resort_map_maker_runtime
    PUBLIC
        pokemon_resort_map_maker_core
        world3d_bgfx_renderer
)

# bgfx ships the exact Dear ImGui version and renderer bridge used here. Pinning
# the editor to those bundled sources avoids a second ImGui ABI in the process.
set(PR_MAP_MAKER_DEAR_IMGUI_DIR
    ${bgfx_cmake_SOURCE_DIR}/bgfx/3rdparty/dear-imgui)
set(PR_MAP_MAKER_BGFX_COMMON_DIR
    ${bgfx_cmake_SOURCE_DIR}/bgfx/examples/common)
add_library(pokemon_resort_map_maker_imgui STATIC
    ${PR_MAP_MAKER_DEAR_IMGUI_DIR}/imgui.cpp
    ${PR_MAP_MAKER_DEAR_IMGUI_DIR}/imgui_draw.cpp
    ${PR_MAP_MAKER_DEAR_IMGUI_DIR}/imgui_tables.cpp
    ${PR_MAP_MAKER_DEAR_IMGUI_DIR}/imgui_widgets.cpp
    ${PR_MAP_MAKER_BGFX_COMMON_DIR}/imgui/imgui.cpp
)
target_include_directories(pokemon_resort_map_maker_imgui
    PUBLIC
        ${PR_MAP_MAKER_DEAR_IMGUI_DIR}/..
        ${PR_MAP_MAKER_BGFX_COMMON_DIR}
)
target_compile_features(pokemon_resort_map_maker_imgui PUBLIC cxx_std_20)
target_link_libraries(pokemon_resort_map_maker_imgui PUBLIC bgfx bx bimg)

add_library(pokemon_resort_map_maker_ui STATIC
    src/app/SdlImGuiInputBridge.cpp
    src/assets/BgfxThumbnailCache.cpp
    src/ui/EditorShell.cpp
    src/ui/EditorShellDoor.cpp
    src/ui/EditorShellTravel.cpp
)
target_include_directories(pokemon_resort_map_maker_ui
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${PR_MAP_MAKER_INCLUDE_DIR}
)
target_compile_features(pokemon_resort_map_maker_ui PUBLIC cxx_std_20)
target_link_libraries(pokemon_resort_map_maker_ui
    PUBLIC
        pokemon_resort_map_maker_core
        pokemon_resort_map_maker_imgui
        SDL2::SDL2
        SDL2_image::SDL2_image
)

set(PR_MAP_MAKER_APPLICATION_SOURCES)
foreach(PR_MAP_MAKER_APPLICATION_SOURCE
        src/main.cpp
        src/app/MapMakerApp.cpp
        src/app/EditorController.cpp
        src/app/EditorControllerPersistence.cpp
        src/app/EditorControllerTravel.cpp
        src/app/EditorControllerUi.cpp)
    if(EXISTS ${PR_MAP_MAKER_ROOT}/${PR_MAP_MAKER_APPLICATION_SOURCE})
        list(APPEND PR_MAP_MAKER_APPLICATION_SOURCES
            ${PR_MAP_MAKER_APPLICATION_SOURCE})
    endif()
endforeach()

if(EXISTS ${PR_MAP_MAKER_ROOT}/src/main.cpp)
    add_executable(pokemon_resort_map_maker
        ${PR_MAP_MAKER_APPLICATION_SOURCES})
    target_include_directories(pokemon_resort_map_maker PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${PR_MAP_MAKER_INCLUDE_DIR})
    target_compile_features(pokemon_resort_map_maker PRIVATE cxx_std_20)
    target_compile_definitions(pokemon_resort_map_maker PRIVATE
        PR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    set_target_properties(pokemon_resort_map_maker PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    target_link_libraries(pokemon_resort_map_maker PRIVATE
        pokemon_resort_map_maker_runtime
        pokemon_resort_map_maker_ui
        SDL2::SDL2
        SDL2_image::SDL2_image
        SDL2_ttf::SDL2_ttf)
    if(TARGET SDL2::SDL2main)
        target_link_libraries(pokemon_resort_map_maker PRIVATE SDL2::SDL2main)
    endif()

    add_dependencies(pokemon_resort_map_maker world3d_bgfx_shaders)
    add_custom_command(
        TARGET pokemon_resort_map_maker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${WORLD3D_BGFX_SHADER_OUTPUT_DIR}
                $<TARGET_FILE_DIR:pokemon_resort_map_maker>/shaders
        COMMENT "Copying exact-world preview shaders for the native map maker"
    )
endif()

option(PR_MAP_MAKER_ENABLE_SANITIZERS
    "Build native map-maker tests with address/undefined sanitizers" OFF)

if(PR_MAP_MAKER_ENABLE_SANITIZERS AND
   CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    foreach(PR_MAP_MAKER_SANITIZED_LIBRARY
            pokemon_resort_map_maker_core
            pokemon_resort_map_maker_runtime)
        target_compile_options(${PR_MAP_MAKER_SANITIZED_LIBRARY} PRIVATE
            -fno-omit-frame-pointer -fsanitize=address,undefined)
    endforeach()
endif()

function(pr_add_map_maker_test TARGET_NAME SOURCE_FILE)
    add_executable(${TARGET_NAME} ${CMAKE_SOURCE_DIR}/${SOURCE_FILE})
    target_compile_features(${TARGET_NAME} PRIVATE cxx_std_20)
    target_compile_definitions(${TARGET_NAME} PRIVATE
        PR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    if(ARGN)
        target_link_libraries(${TARGET_NAME} PRIVATE ${ARGN})
    else()
        target_link_libraries(${TARGET_NAME} PRIVATE
            pokemon_resort_map_maker_core)
    endif()
    if(PR_MAP_MAKER_ENABLE_SANITIZERS AND
       CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${TARGET_NAME} PRIVATE
            -fno-omit-frame-pointer -fsanitize=address,undefined)
        target_link_options(${TARGET_NAME} PRIVATE
            -fno-omit-frame-pointer -fsanitize=address,undefined)
    endif()
    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
    set_tests_properties(${TARGET_NAME} PROPERTIES
        LABELS map_maker
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endfunction()

pr_add_map_maker_test(autosave_recovery_tests
    tests/native/mapmaker/autosave_recovery_tests.cpp)
pr_add_map_maker_test(command_stack_tests
    tests/native/mapmaker/command_stack_tests.cpp)
pr_add_map_maker_test(document_commands_tests
    tests/native/mapmaker/document_commands_tests.cpp)
pr_add_map_maker_test(editor_asset_catalog_tests
    tests/native/mapmaker/editor_asset_catalog_tests.cpp)
pr_add_map_maker_test(frame_metrics_tests
    tests/native/mapmaker/frame_metrics_tests.cpp)
pr_add_map_maker_test(map_metadata_editing_tests
    tests/native/mapmaker/map_metadata_editing_tests.cpp)
pr_add_map_maker_test(map_project_document_tests
    tests/native/mapmaker/map_project_document_tests.cpp)
pr_add_map_maker_test(owmap_document_tests
    tests/native/mapmaker/owmap_document_tests.cpp)
pr_add_map_maker_test(selection_validation_tests
    tests/native/mapmaker/selection_validation_tests.cpp)
pr_add_map_maker_test(structured_logger_tests
    tests/native/mapmaker/structured_logger_tests.cpp)

pr_add_map_maker_test(world_picker_tests
    tests/native/mapmaker/world_picker_tests.cpp
    pokemon_resort_map_maker_runtime)
