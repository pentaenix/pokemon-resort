cmake_minimum_required(VERSION 3.20)
project(title_screen_data_driven LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(SDL2_ttf REQUIRED)
find_package(SQLite3)

set(RESORT_STORAGE_SOURCES
    src/resort/domain/Ids.cpp
    src/resort/persistence/SqliteConnection.cpp
    src/resort/persistence/Migrations.cpp
    src/resort/persistence/PokemonRepository.cpp
    src/resort/persistence/BoxRepository.cpp
    src/resort/persistence/SnapshotRepository.cpp
    src/resort/persistence/HistoryRepository.cpp
    src/resort/persistence/MirrorSessionRepository.cpp
    src/resort/services/BoxViewService.cpp
    src/resort/services/MirrorSessionService.cpp
    src/resort/services/PokemonMatcher.cpp
    src/resort/services/PokemonMergeService.cpp
    src/resort/services/PokemonImportService.cpp
    src/resort/services/PokemonExportService.cpp
    src/resort/services/PokemonResortService.cpp
    src/resort/services/BridgeImportService.cpp
    src/resort/integration/BridgeImportAdapter.cpp
)

add_executable(title_screen_demo
    src/main.cpp
    src/core/App.cpp
    src/core/Audio.mm
    src/core/Assets.cpp
    src/core/Font.cpp
    src/core/InputBindings.cpp
    src/core/Json.cpp
    src/core/PokeSpriteAssets.cpp
    src/core/ConfigLoader.cpp
    src/core/SaveBridgeClient.cpp
    src/core/SaveLibrary.cpp
    src/core/SaveDataStore.cpp
    ${RESORT_STORAGE_SOURCES}
    src/ui/LoadingScreen.cpp
    src/ui/TitleScreen.cpp
    src/ui/TransferTicketScreen.cpp
    src/ui/TransferSystemScreen.cpp
    src/ui/BoxViewport.cpp
    src/ui/FocusManager.cpp
)

target_include_directories(title_screen_demo PRIVATE include)

target_link_libraries(title_screen_demo PRIVATE SDL2::SDL2 SDL2::SDL2main SDL2_image::SDL2_image SDL2_ttf::SDL2_ttf)

if(SQLite3_FOUND)
    target_link_libraries(title_screen_demo PRIVATE SQLite3::SQLite3)
else()
    target_link_libraries(title_screen_demo PRIVATE sqlite3)
endif()

if(APPLE)
    target_link_libraries(title_screen_demo PRIVATE "-framework AVFoundation" "-framework Foundation")
endif()

enable_testing()

add_executable(resort_storage_tests
    ../tests/native/resort/storage_import_tests.cpp
    src/core/Json.cpp
    src/core/SaveBridgeClient.cpp
    ${RESORT_STORAGE_SOURCES}
)
target_include_directories(resort_storage_tests PRIVATE include)
if(SQLite3_FOUND)
    target_link_libraries(resort_storage_tests PRIVATE SQLite3::SQLite3)
else()
    target_link_libraries(resort_storage_tests PRIVATE sqlite3)
endif()
add_test(NAME resort_storage_tests COMMAND resort_storage_tests)

add_executable(resort_backend_tool
    tools/resort_backend_tool.cpp
    src/core/Json.cpp
    src/core/SaveBridgeClient.cpp
    ${RESORT_STORAGE_SOURCES}
)
target_include_directories(resort_backend_tool PRIVATE include)
if(SQLite3_FOUND)
    target_link_libraries(resort_backend_tool PRIVATE SQLite3::SQLite3)
else()
    target_link_libraries(resort_backend_tool PRIVATE sqlite3)
endif()

add_custom_target(pkhex_bridge_publish
    COMMAND dotnet publish
        ${CMAKE_SOURCE_DIR}/../tools/pkhex_bridge/PKHeXBridge.csproj
        -c Release
        -r osx-arm64
        --self-contained true
        -o ${CMAKE_BINARY_DIR}/pkhex_bridge/osx-arm64
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Publishing self-contained PKHeX bridge for macOS"
)
