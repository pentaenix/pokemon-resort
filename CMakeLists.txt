cmake_minimum_required(VERSION 3.20)
project(title_screen_data_driven LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(SDL2_ttf REQUIRED)

add_executable(title_screen_demo
    src/main.cpp
    src/core/App.cpp
    src/core/Audio.mm
    src/core/Assets.cpp
    src/core/Font.cpp
    src/core/InputBindings.cpp
    src/core/Json.cpp
    src/core/ConfigLoader.cpp
    src/core/SaveBridgeClient.cpp
    src/core/SaveLibrary.cpp
    src/core/SaveDataStore.cpp
    src/ui/TitleScreen.cpp
    src/ui/TransferTicketSandboxScreen.cpp
)

target_include_directories(title_screen_demo PRIVATE include)

target_link_libraries(title_screen_demo PRIVATE SDL2::SDL2 SDL2::SDL2main SDL2_image::SDL2_image SDL2_ttf::SDL2_ttf)

if(APPLE)
    target_link_libraries(title_screen_demo PRIVATE "-framework AVFoundation" "-framework Foundation")
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
