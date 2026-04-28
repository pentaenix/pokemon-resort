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
    src/core/InputRouter.cpp
    src/core/Json.cpp
    src/core/PokemonBall.cpp
    src/core/PokemonOrigin.cpp
    src/core/PokeSpriteAssets.cpp
    src/core/ConfigLoader.cpp
    src/core/SaveBridgeClient.cpp
    src/core/SaveLibrary.cpp
    src/core/SaveDataStore.cpp
    ${RESORT_STORAGE_SOURCES}
    src/ui/LoadingScreen.cpp
    src/ui/TitleScreen.cpp
    src/ui/TransferFlowCoordinator.cpp
    src/ui/title_screen/MainMenuController.cpp
    src/ui/title_screen/OptionsMenuController.cpp
    src/ui/title_screen/SectionScreenController.cpp
    src/ui/transfer_flow/TransferFlowController.cpp
    src/ui/transfer_flow/TransferSelectionBuilder.cpp
    src/ui/transfer_ticket/TransferTicketListController.cpp
    src/ui/title_screen/TitleScreenRender.cpp
    src/ui/TransferTicketScreen.cpp
    src/ui/TransferSystemScreen.cpp
    src/ui/transfer_system/TransferSystemScreenLifecycle.cpp
    src/ui/transfer_system/TransferSystemScreenCtor.cpp
    src/ui/transfer_system/TransferSystemScreenFocusNav.cpp
    src/ui/transfer_system/TransferSystemScreenNavigate2d.cpp
    src/ui/transfer_system/TransferSystemScreenUpdateSync.cpp
    src/ui/transfer_system/TransferSystemScreenViewportRefresh.cpp
    src/ui/transfer_system/TransferSystemScreenDropdownGame.cpp
    src/ui/transfer_system/TransferSystemScreenDropdownResort.cpp
    src/ui/transfer_system/TransferSystemScreenMiniPreview.cpp
    src/ui/transfer_system/TransferSystemScreenMenusUpdate.cpp
    src/ui/transfer_system/TransferSystemScreenBoxSpaceGestures.cpp
    src/ui/transfer_system/TransferSystemScreenBoxViewports.cpp
    src/ui/transfer_system/TransferSystemScreenPokemonMoveSingle.cpp
    src/ui/transfer_system/TransferSystemScreenPokemonMoveMulti.cpp
    src/ui/transfer_system/TransferSystemScreenPointerHitTest.cpp
    src/ui/transfer_system/TransferSystemScreenAdvancePressed.cpp
    src/ui/transfer_system/TransferSystemScreenBackPressed.cpp
    src/ui/transfer_system/TransferSystemScreenUnroutedEvents.cpp
    src/ui/transfer_system/TransferSystemScreenBoxRenameModal.cpp
    src/ui/transfer_system/TransferSystemScreenBoxRenameModalRender.cpp
    src/ui/transfer_system/TransferSystemScreenPointerPressed.cpp
    src/ui/transfer_system/TransferSystemScreenPointerMoved.cpp
    src/ui/transfer_system/TransferSystemScreenPointerReleased.cpp
    src/ui/transfer_system/TransferSystemScreenBoxSpacePointer.cpp
    src/ui/transfer_system/TransferSystemScreenActionMenuPointer.cpp
    src/ui/transfer_system/TransferSystemScreenBoxSpaceQuickDrop.cpp
    src/ui/transfer_system/TransferSystemScreenViewportModels.cpp
    src/ui/transfer_system/TransferSystemScreenBoxSpaceMode.cpp
    src/ui/transfer_system/TransferSystemScreenMultiMarquee.cpp
    src/ui/transfer_system/TransferSystemScreenDebugHooks.cpp
    src/ui/transfer_system/TransferSystemScreenBoxNavigation.cpp
    src/ui/transfer_system/TransferSystemScreenDropdownPointer.cpp
    src/ui/transfer_system/TransferSystemScreenActionMenuHelpers.cpp
    src/ui/transfer_system/TransferSystemScreenSmallQueries.cpp
    src/ui/transfer_system/TransferSystemScreenUiHelpers.cpp
    src/ui/transfer_system/GameTransferConfig.cpp
    src/ui/transfer_system/GameBoxBrowserController.cpp
    src/ui/transfer_system/MultiPokemonMoveController.cpp
    src/ui/transfer_system/move/HeldMoveController.cpp
    src/ui/transfer_system/ItemActionMenuController.cpp
    src/ui/transfer_system/PokemonActionMenuController.cpp
    src/ui/transfer_system/PokemonMoveController.cpp
    src/ui/transfer_system/TransferSystemFocusGraph.cpp
    src/ui/transfer_system/TransferInfoBannerPresenter.cpp
    src/ui/transfer_system/TransferInfoBannerRenderer.cpp
    src/ui/transfer_system/TransferSystemRendererMain.cpp
    src/ui/transfer_system/TransferSystemRendererTopBar.cpp
    src/ui/transfer_system/TransferSystemRendererMenus.cpp
    src/ui/transfer_system/TransferSystemRendererHeld.cpp
    src/ui/transfer_system/TransferSystemRendererDropdowns.cpp
    src/ui/transfer_system/TransferSystemUiStateController.cpp
    src/ui/transfer_system/detail/SdlScanlineDraw.cpp
    src/ui/transfer_system/detail/StringUtil.cpp
    src/ui/transfer_system/detail/SpriteSlugCandidates.cpp
    src/ui/transfer_system/detail/TextureLoad.cpp
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

add_executable(title_screen_menu_tests
    ../tests/native/title_screen/main_menu_controller_tests.cpp
    src/ui/title_screen/MainMenuController.cpp
)
target_include_directories(title_screen_menu_tests PRIVATE include)
add_test(NAME title_screen_menu_tests COMMAND title_screen_menu_tests)

add_executable(title_screen_options_tests
    ../tests/native/title_screen/options_menu_controller_tests.cpp
    src/ui/title_screen/OptionsMenuController.cpp
)
target_include_directories(title_screen_options_tests PRIVATE include)
add_test(NAME title_screen_options_tests COMMAND title_screen_options_tests)

add_executable(title_screen_section_tests
    ../tests/native/title_screen/section_screen_controller_tests.cpp
    src/ui/title_screen/SectionScreenController.cpp
)
target_include_directories(title_screen_section_tests PRIVATE include)
add_test(NAME title_screen_section_tests COMMAND title_screen_section_tests)

add_executable(transfer_ticket_list_tests
    ../tests/native/transfer_ticket/transfer_ticket_list_controller_tests.cpp
    src/ui/transfer_ticket/TransferTicketListController.cpp
)
target_include_directories(transfer_ticket_list_tests PRIVATE include)
target_link_libraries(transfer_ticket_list_tests PRIVATE SDL2::SDL2)
add_test(NAME transfer_ticket_list_tests COMMAND transfer_ticket_list_tests)

add_executable(transfer_selection_builder_tests
    ../tests/native/transfer_flow/transfer_selection_builder_tests.cpp
    src/core/PokemonOrigin.cpp
    src/ui/transfer_flow/TransferSelectionBuilder.cpp
)
target_include_directories(transfer_selection_builder_tests PRIVATE include)
add_test(NAME transfer_selection_builder_tests COMMAND transfer_selection_builder_tests)

add_executable(transfer_flow_controller_tests
    ../tests/native/transfer_flow/transfer_flow_controller_tests.cpp
    src/ui/transfer_flow/TransferFlowController.cpp
)
target_include_directories(transfer_flow_controller_tests PRIVATE include)
add_test(NAME transfer_flow_controller_tests COMMAND transfer_flow_controller_tests)

add_executable(game_transfer_config_tests
    ../tests/native/transfer_system/game_transfer_config_tests.cpp
    src/core/Json.cpp
    src/ui/transfer_system/GameTransferConfig.cpp
)
target_include_directories(game_transfer_config_tests PRIVATE include)
add_test(NAME game_transfer_config_tests COMMAND game_transfer_config_tests)

add_executable(transfer_info_banner_presenter_tests
    ../tests/native/transfer_system/transfer_info_banner_presenter_tests.cpp
    src/core/PokemonBall.cpp
    src/core/PokemonOrigin.cpp
    src/ui/transfer_system/TransferInfoBannerPresenter.cpp
)
target_include_directories(transfer_info_banner_presenter_tests PRIVATE include)
add_test(NAME transfer_info_banner_presenter_tests COMMAND transfer_info_banner_presenter_tests)

add_executable(transfer_system_ui_state_controller_tests
    ../tests/native/transfer_system/transfer_system_ui_state_controller_tests.cpp
    src/ui/transfer_system/TransferSystemUiStateController.cpp
)
target_include_directories(transfer_system_ui_state_controller_tests PRIVATE include)
add_test(NAME transfer_system_ui_state_controller_tests COMMAND transfer_system_ui_state_controller_tests)

add_executable(game_box_browser_controller_tests
    ../tests/native/transfer_system/game_box_browser_controller_tests.cpp
    src/ui/transfer_system/GameBoxBrowserController.cpp
)
target_include_directories(game_box_browser_controller_tests PRIVATE include)
add_test(NAME game_box_browser_controller_tests COMMAND game_box_browser_controller_tests)

add_executable(pokemon_action_menu_controller_tests
    ../tests/native/transfer_system/pokemon_action_menu_controller_tests.cpp
    src/ui/transfer_system/PokemonActionMenuController.cpp
)
target_include_directories(pokemon_action_menu_controller_tests PRIVATE include)
target_link_libraries(pokemon_action_menu_controller_tests PRIVATE SDL2::SDL2)
add_test(NAME pokemon_action_menu_controller_tests COMMAND pokemon_action_menu_controller_tests)

add_executable(multi_pokemon_move_controller_tests
    ../tests/native/transfer_system/multi_pokemon_move_controller_tests.cpp
    src/ui/transfer_system/MultiPokemonMoveController.cpp
)
target_include_directories(multi_pokemon_move_controller_tests PRIVATE include)
target_link_libraries(multi_pokemon_move_controller_tests PRIVATE SDL2::SDL2)
add_test(NAME multi_pokemon_move_controller_tests COMMAND multi_pokemon_move_controller_tests)

add_executable(transfer_system_focus_graph_tests
    ../tests/native/transfer_system/transfer_system_focus_graph_tests.cpp
    src/ui/transfer_system/TransferSystemFocusGraph.cpp
)
target_include_directories(transfer_system_focus_graph_tests PRIVATE include)
target_link_libraries(transfer_system_focus_graph_tests PRIVATE SDL2::SDL2)
add_test(NAME transfer_system_focus_graph_tests COMMAND transfer_system_focus_graph_tests)

add_executable(input_router_tests
    ../tests/native/core/input_router_tests.cpp
    src/core/InputBindings.cpp
    src/core/InputRouter.cpp
)
target_include_directories(input_router_tests PRIVATE include)
target_link_libraries(input_router_tests PRIVATE SDL2::SDL2)
add_test(NAME input_router_tests COMMAND input_router_tests)

add_executable(config_loader_tests
    ../tests/native/core/config_loader_tests.cpp
    src/core/ConfigLoader.cpp
    src/core/Json.cpp
)
target_include_directories(config_loader_tests PRIVATE include)
add_test(NAME config_loader_tests COMMAND config_loader_tests)

add_executable(pokesprite_assets_tests
    ../tests/native/core/pokesprite_assets_tests.cpp
    src/core/Json.cpp
    src/core/PokeSpriteAssets.cpp
)
target_include_directories(pokesprite_assets_tests PRIVATE include)
target_link_libraries(pokesprite_assets_tests PRIVATE SDL2::SDL2 SDL2_image::SDL2_image SDL2_ttf::SDL2_ttf)
add_test(NAME pokesprite_assets_tests COMMAND pokesprite_assets_tests)

add_executable(save_library_cache_tests
    ../tests/native/core/save_library_cache_tests.cpp
    src/core/Json.cpp
    src/core/SaveBridgeClient.cpp
    src/core/SaveLibrary.cpp
)
target_include_directories(save_library_cache_tests PRIVATE include)
add_test(NAME save_library_cache_tests COMMAND save_library_cache_tests)

add_executable(input_config_integration_tests
    ../tests/native/core/input_config_integration_tests.cpp
    src/core/ConfigLoader.cpp
    src/core/InputBindings.cpp
    src/core/InputRouter.cpp
    src/core/Json.cpp
)
target_include_directories(input_config_integration_tests PRIVATE include)
target_link_libraries(input_config_integration_tests PRIVATE SDL2::SDL2)
add_test(NAME input_config_integration_tests COMMAND input_config_integration_tests)

add_test(
    NAME title_screen_headless_smoke
    COMMAND /bin/zsh ${CMAKE_SOURCE_DIR}/../tests/e2e/native/run_title_screen_headless_smoke.sh
)

add_executable(title_screen_flow_harness_tests
    ../tests/e2e/native/title_screen_flow_harness_tests.cpp
    src/core/Assets.cpp
    src/core/ConfigLoader.cpp
    src/core/Font.cpp
    src/core/InputBindings.cpp
    src/core/InputRouter.cpp
    src/core/Json.cpp
    src/ui/TitleScreen.cpp
    src/ui/title_screen/MainMenuController.cpp
    src/ui/title_screen/OptionsMenuController.cpp
    src/ui/title_screen/SectionScreenController.cpp
    src/ui/title_screen/TitleScreenRender.cpp
)
target_compile_definitions(title_screen_flow_harness_tests PRIVATE PR_ENABLE_TEST_HOOKS)
target_include_directories(title_screen_flow_harness_tests PRIVATE include)
target_link_libraries(title_screen_flow_harness_tests PRIVATE SDL2::SDL2 SDL2_image::SDL2_image SDL2_ttf::SDL2_ttf)
add_test(NAME title_screen_flow_harness_tests COMMAND title_screen_flow_harness_tests)

add_executable(transfer_system_flow_harness_tests
    ../tests/e2e/native/transfer_system_flow_harness_tests.cpp
    src/core/Assets.cpp
    src/core/ConfigLoader.cpp
    src/core/Font.cpp
    src/core/Json.cpp
    src/core/PokemonBall.cpp
    src/core/PokemonOrigin.cpp
    src/core/PokeSpriteAssets.cpp
    src/core/SaveBridgeClient.cpp
    ${RESORT_STORAGE_SOURCES}
    src/ui/TransferSystemScreen.cpp
    src/ui/transfer_system/TransferSystemScreenLifecycle.cpp
    src/ui/transfer_system/TransferSystemScreenCtor.cpp
    src/ui/transfer_system/TransferSystemScreenFocusNav.cpp
    src/ui/transfer_system/TransferSystemScreenNavigate2d.cpp
    src/ui/transfer_system/TransferSystemScreenUpdateSync.cpp
    src/ui/transfer_system/TransferSystemScreenViewportRefresh.cpp
    src/ui/transfer_system/TransferSystemScreenDropdownGame.cpp
    src/ui/transfer_system/TransferSystemScreenDropdownResort.cpp
    src/ui/transfer_system/TransferSystemScreenMiniPreview.cpp
    src/ui/transfer_system/TransferSystemScreenMenusUpdate.cpp
    src/ui/transfer_system/TransferSystemScreenBoxSpaceGestures.cpp
    src/ui/transfer_system/TransferSystemScreenBoxViewports.cpp
    src/ui/transfer_system/TransferSystemScreenPokemonMoveSingle.cpp
    src/ui/transfer_system/TransferSystemScreenPokemonMoveMulti.cpp
    src/ui/transfer_system/TransferSystemScreenPointerHitTest.cpp
    src/ui/transfer_system/TransferSystemScreenAdvancePressed.cpp
    src/ui/transfer_system/TransferSystemScreenBackPressed.cpp
    src/ui/transfer_system/TransferSystemScreenUnroutedEvents.cpp
    src/ui/transfer_system/TransferSystemScreenBoxRenameModal.cpp
    src/ui/transfer_system/TransferSystemScreenBoxRenameModalRender.cpp
    src/ui/transfer_system/TransferSystemScreenPointerPressed.cpp
    src/ui/transfer_system/TransferSystemScreenPointerMoved.cpp
    src/ui/transfer_system/TransferSystemScreenPointerReleased.cpp
    src/ui/transfer_system/TransferSystemScreenBoxSpacePointer.cpp
    src/ui/transfer_system/TransferSystemScreenActionMenuPointer.cpp
    src/ui/transfer_system/TransferSystemScreenBoxSpaceQuickDrop.cpp
    src/ui/transfer_system/TransferSystemScreenViewportModels.cpp
    src/ui/transfer_system/TransferSystemScreenBoxSpaceMode.cpp
    src/ui/transfer_system/TransferSystemScreenMultiMarquee.cpp
    src/ui/transfer_system/TransferSystemScreenDebugHooks.cpp
    src/ui/transfer_system/TransferSystemScreenBoxNavigation.cpp
    src/ui/transfer_system/TransferSystemScreenDropdownPointer.cpp
    src/ui/transfer_system/TransferSystemScreenActionMenuHelpers.cpp
    src/ui/transfer_system/TransferSystemScreenSmallQueries.cpp
    src/ui/transfer_system/TransferSystemScreenUiHelpers.cpp
    src/ui/transfer_system/GameTransferConfig.cpp
    src/ui/transfer_system/GameBoxBrowserController.cpp
    src/ui/transfer_system/MultiPokemonMoveController.cpp
    src/ui/transfer_system/move/HeldMoveController.cpp
    src/ui/transfer_system/ItemActionMenuController.cpp
    src/ui/transfer_system/PokemonActionMenuController.cpp
    src/ui/transfer_system/PokemonMoveController.cpp
    src/ui/transfer_system/TransferSystemFocusGraph.cpp
    src/ui/transfer_system/TransferInfoBannerPresenter.cpp
    src/ui/transfer_system/TransferInfoBannerRenderer.cpp
    src/ui/transfer_system/TransferSystemRendererMain.cpp
    src/ui/transfer_system/TransferSystemRendererTopBar.cpp
    src/ui/transfer_system/TransferSystemRendererMenus.cpp
    src/ui/transfer_system/TransferSystemRendererHeld.cpp
    src/ui/transfer_system/TransferSystemRendererDropdowns.cpp
    src/ui/transfer_system/TransferSystemUiStateController.cpp
    src/ui/transfer_system/detail/SdlScanlineDraw.cpp
    src/ui/transfer_system/detail/StringUtil.cpp
    src/ui/transfer_system/detail/SpriteSlugCandidates.cpp
    src/ui/transfer_system/detail/TextureLoad.cpp
    src/ui/BoxViewport.cpp
    src/ui/FocusManager.cpp
)
target_compile_definitions(transfer_system_flow_harness_tests PRIVATE PR_ENABLE_TEST_HOOKS)
target_include_directories(transfer_system_flow_harness_tests PRIVATE include)
target_link_libraries(transfer_system_flow_harness_tests PRIVATE SDL2::SDL2 SDL2_image::SDL2_image SDL2_ttf::SDL2_ttf)
if(SQLite3_FOUND)
    target_link_libraries(transfer_system_flow_harness_tests PRIVATE SQLite3::SQLite3)
else()
    target_link_libraries(transfer_system_flow_harness_tests PRIVATE sqlite3)
endif()
add_test(NAME transfer_system_flow_harness_tests COMMAND transfer_system_flow_harness_tests)

add_executable(transfer_ticket_unicode_harness_tests
    ../tests/e2e/native/transfer_ticket_unicode_harness_tests.cpp
    src/core/Assets.cpp
    src/core/ConfigLoader.cpp
    src/core/Font.cpp
    src/core/Json.cpp
    src/core/PokeSpriteAssets.cpp
    src/ui/TransferTicketScreen.cpp
    src/ui/transfer_ticket/TransferTicketListController.cpp
)
target_compile_definitions(transfer_ticket_unicode_harness_tests PRIVATE PR_ENABLE_TEST_HOOKS)
target_include_directories(transfer_ticket_unicode_harness_tests PRIVATE include)
target_link_libraries(transfer_ticket_unicode_harness_tests PRIVATE SDL2::SDL2 SDL2_image::SDL2_image SDL2_ttf::SDL2_ttf)
add_test(NAME transfer_ticket_unicode_harness_tests COMMAND transfer_ticket_unicode_harness_tests)

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
