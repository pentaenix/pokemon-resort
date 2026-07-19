cmake_minimum_required(VERSION 3.20)
project(title_screen_data_driven LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS ON CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_BIN2C OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_TEXTURE OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_SHADER ON CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    bgfx_cmake
    GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
    GIT_TAG 4ba3843dd96f39252cb4e995f28070fdfd12a3d3
    GIT_SHALLOW TRUE
    GIT_SUBMODULES bgfx bimg bx
)
FetchContent_MakeAvailable(bgfx_cmake)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(SDL2_ttf REQUIRED)
find_package(SQLite3)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)

set(WORLD3D_BGFX_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay/world3d/rendering/bgfx/shaders)
set(WORLD3D_BGFX_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/world3d-shaders)
bgfx_compile_shaders(
    TYPE VERTEX
    SHADERS ${WORLD3D_BGFX_SHADER_SOURCE_DIR}/vs_world.sc ${WORLD3D_BGFX_SHADER_SOURCE_DIR}/vs_world_camera_sphere.sc ${WORLD3D_BGFX_SHADER_SOURCE_DIR}/vs_billboard.sc
    VARYING_DEF ${WORLD3D_BGFX_SHADER_SOURCE_DIR}/varying.def.sc
    OUTPUT_DIR ${WORLD3D_BGFX_SHADER_OUTPUT_DIR}
    INCLUDE_DIRS ${bgfx_cmake_SOURCE_DIR}/bgfx/src
    OUT_FILES_VAR WORLD3D_BGFX_VERTEX_SHADERS
)
bgfx_compile_shaders(
    TYPE FRAGMENT
    SHADERS ${WORLD3D_BGFX_SHADER_SOURCE_DIR}/fs_textured_cutout.sc ${WORLD3D_BGFX_SHADER_SOURCE_DIR}/fs_pokemon_eye.sc ${WORLD3D_BGFX_SHADER_SOURCE_DIR}/fs_eye_sclera_mask.sc
    VARYING_DEF ${WORLD3D_BGFX_SHADER_SOURCE_DIR}/varying.def.sc
    OUTPUT_DIR ${WORLD3D_BGFX_SHADER_OUTPUT_DIR}
    INCLUDE_DIRS ${bgfx_cmake_SOURCE_DIR}/bgfx/src
    OUT_FILES_VAR WORLD3D_BGFX_FRAGMENT_SHADERS
)
add_custom_target(world3d_bgfx_shaders DEPENDS ${WORLD3D_BGFX_VERTEX_SHADERS} ${WORLD3D_BGFX_FRAGMENT_SHADERS})

set(RESORT_STORAGE_SOURCES
    src/core/crypto/Sha256.cpp
    src/resort/domain/ResortRibbonCatalogMerge.cpp
    src/resort/domain/ResortPokemonRecord.cpp
    src/resort/domain/Ids.cpp
    src/resort/domain/PokemonMergeFieldPolicy.cpp
    src/resort/openhome/OpenHomePokemonPayload.cpp
    src/resort/openhome/OpenHomeMovementBridge.cpp
    src/resort/openhome/OpenHomeStorageBridge.cpp
    src/resort/transfer/LegacyPkhexTransferCore.cpp
    src/resort/transfer/OpenHomeTransferCore.cpp
    src/resort/diagnostics/ResortTransferLog.cpp
    src/resort/persistence/SqliteConnection.cpp
    src/resort/persistence/Migrations.cpp
    src/resort/persistence/PokemonRepository.cpp
    src/resort/persistence/BoxRepository.cpp
    src/resort/persistence/SnapshotRepository.cpp
    src/resort/persistence/HistoryRepository.cpp
    src/resort/persistence/MirrorSessionRepository.cpp
    src/resort/persistence/OpenHomeLinkRepository.cpp
    src/resort/persistence/PidTransportRegistryRepository.cpp
    src/resort/services/BoxViewService.cpp
    src/resort/services/MirrorSessionService.cpp
    src/resort/services/PokemonMatcher.cpp
    src/resort/services/PokemonMergeService.cpp
    src/resort/services/PokemonImportService.cpp
    src/resort/services/PokemonExportService.cpp
    src/resort/services/MirrorProjectionService.cpp
    src/resort/services/MirrorReturnAnalysis.cpp
    src/resort/services/PokemonResortService.cpp
    src/resort/services/BridgeImportService.cpp
    src/resort/integration/BridgeImportAdapter.cpp
    src/resort/integration/Gen12DvBytes.cpp
)

add_library(world3d_bgfx_renderer STATIC
    src/core/crypto/Sha256.cpp
    src/gameplay/attend/rendering/AttendBgfxRenderer.cpp
    src/gameplay/attend/rendering/AttendBgfxRendererAnimation.cpp
    src/gameplay/attend/rendering/AttendBgfxRendererCamera.cpp
    src/gameplay/attend/rendering/AttendBgfxRendererCommon.cpp
    src/gameplay/attend/rendering/AttendBgfxRendererCore.cpp
    src/gameplay/attend/rendering/AttendBgfxRendererEye.cpp
    src/gameplay/attend/rendering/AttendBgfxRendererPokemonBuild.cpp
    src/gameplay/attend/rendering/AttendBgfxRendererRender.cpp
    src/gameplay/attend/rendering/AttendBgfxRendererScene.cpp
    src/gameplay/attend/rendering/AttendBgfxRendererSubmit.cpp
    src/gameplay/attend/rendering/AttendBgfxRendererCornerButton.cpp
    src/gameplay/attend/rendering/AttendBgfxRendererProfilePlate.cpp
    src/gameplay/attend/rendering/AttendBgfxRendererUi.cpp
    src/gameplay/attend/rendering/AttendPokemonAsset.cpp
    src/gameplay/attend/rendering/AttendEyeSocketMask.cpp
    src/gameplay/attend/rendering/AttendPokemonMaterialPolicy.cpp
    src/gameplay/attend/rendering/AttendPokemonModel.cpp
    src/gameplay/world3d/rendering/bgfx/BgfxBackend.cpp
    src/gameplay/world3d/rendering/bgfx/BillboardBgfxDrawer.cpp
    src/gameplay/world3d/rendering/bgfx/OverworldBgfxRenderer.cpp
    src/gameplay/world3d/rendering/bgfx/RampTerrainRender.cpp
    src/gameplay/world3d/data/RtpksTilePackageLoader.cpp
    src/gameplay/world3d/rendering/BillboardPlacement.cpp
    src/gameplay/world3d/rendering/PixelScale.cpp
    src/gameplay/world3d/rendering/WorldBillboardCompositor.cpp
    src/gameplay/world3d/rendering/CharacterTextureCache.cpp
    src/gameplay/world3d/rendering/SpriteShadowDecal.cpp
    src/gameplay/world3d/terrain/TerrainSurface.cpp
    src/ui/overlay/OverlaySliceLayout.cpp
)
target_include_directories(world3d_bgfx_renderer PUBLIC include)
target_include_directories(world3d_bgfx_renderer PRIVATE ${bgfx_cmake_SOURCE_DIR}/bimg/3rdparty/tinyexr/deps/miniz)
target_compile_features(world3d_bgfx_renderer PRIVATE cxx_std_20)
target_link_libraries(world3d_bgfx_renderer PUBLIC bgfx bx bimg SDL2::SDL2 SDL2_image::SDL2_image SDL2_ttf::SDL2_ttf PkgConfig::ZSTD)

add_executable(title_screen_demo
    src/main.cpp
    src/core/App.cpp
    src/core/app/audio/AppAudioDirector.cpp
    src/core/app/audio/PokemonCryPlayer.cpp
    src/core/app/frame/AppFrameRequests.cpp
    src/core/app/loading/AppLoadingCoordinator.cpp
    src/core/app/AppPaths.cpp
    src/core/app/screen/AppScreenCoordinator.cpp
    src/core/app/screen/AppScreenCoordinatorAttend.cpp
    src/core/app/screen/AppScreenCoordinatorOverworld3D.cpp
    src/core/app/screen/AppScreenCoordinatorLoading.cpp
    src/core/app/screen/AppScreenCoordinatorTitle.cpp
    src/core/app/screen/AppScreenCoordinatorTransfer.cpp
    src/core/app/transition/AppTransitionController.cpp
    src/core/audio/Audio.mm
    src/core/assets/Assets.cpp
    src/core/assets/Font.cpp
    src/core/input/InputBindings.cpp
    src/core/input/InputRouter.cpp
    src/core/config/Json.cpp
    src/core/domain/PokemonBall.cpp
    src/core/domain/PokemonOrigin.cpp
    src/core/assets/PokemonCryAssets.cpp
    src/core/assets/PokeSpriteAssets.cpp
    src/core/config/ConfigLoader.cpp
    src/core/bridge/SaveBridgeClient.cpp
    src/transfer/application/NullTransferService.cpp
    src/transfer/application/OpenHomeMovementOrchestrator.cpp
    src/transfer/application/TransferCommitOrchestrator.cpp
    src/gameplay/world3d/events/InMemoryWorldEventBus.cpp
    src/gameplay/world3d/camera/Gen4CameraPreset.cpp
    src/gameplay/world3d/camera/Gen4FollowCamera.cpp
    src/gameplay/world3d/characters/SpriteSheetAnimator.cpp
    src/gameplay/world3d/characters/CharacterController.cpp
    src/gameplay/world3d/characters/CharacterTerrainQuery.cpp
    src/gameplay/world3d/characters/GridActorMotor.cpp
    src/gameplay/world3d/terrain/ActorTerrainBinding.cpp
    src/gameplay/world3d/terrain/GridStepMotor.cpp
    src/gameplay/world3d/data/SceneMetadataParser.cpp
    src/gameplay/world3d/data/JsonOverworldLoader.cpp
    src/gameplay/world3d/data/OwmapOverworldLoader.cpp
    src/gameplay/world3d/data/GlbModelLoader.cpp
    src/gameplay/world3d/data/GlbModelAnimation.cpp
    src/gameplay/world3d/characters/CharacterMovementConfig.cpp
    src/gameplay/world3d/rendering/GlbModelRenderer.cpp
    src/gameplay/world3d/effects/LandingDustSystem.cpp
    src/gameplay/world3d/followers/FollowerConfig.cpp
    src/gameplay/world3d/followers/FollowerController.cpp
    src/gameplay/world3d/followers/FollowerIdleExitTarget.cpp
    src/gameplay/world3d/followers/NatureIdleConfig.cpp
    src/gameplay/world3d/followers/NatureIdlePlanner.cpp
    src/gameplay/world3d/npc/ResortPokemonSpawnConfig.cpp
    src/gameplay/world3d/npc/NpcActorDriver.cpp
    src/gameplay/world3d/scripts/OverworldScript.cpp
    src/gameplay/world3d/dialogue/OverworldTextboxConfig.cpp
    src/gameplay/world3d/dialogue/OverworldTextboxController.cpp
    src/gameplay/world3d/dialogue/OverworldTextboxRenderer.cpp
    src/gameplay/world3d/interactions/InteractionSequence.cpp
    src/gameplay/world3d/interactions/InteractionText.cpp
    src/gameplay/world3d/rendering/BillboardSpriteRenderer.cpp
    src/gameplay/world3d/rendering/FallbackTerrainRenderer.cpp
    src/gameplay/world3d/rendering/OverworldMapRenderer.cpp
    src/gameplay/attend/AttendSceneConfig.cpp
    src/gameplay/attend/PokemonModelCatalog.cpp
    src/core/save/SaveLibrary.cpp
    src/core/save/SaveDataStore.cpp
    src/core/app/persistence/UserSettingsPersistence.cpp
    src/core/save/TransferBoxEditsStore.cpp
    src/core/bridge/BridgeImportMerge.cpp
    ${RESORT_STORAGE_SOURCES}
    src/ui/loading/PokeballLoadingScreen.cpp
    src/ui/loading/LoadingScreenFactory.cpp
    src/ui/loading/ResortTransferLoadingConfig.cpp
    src/ui/loading/ResortTransferLoadingRenderer.cpp
    src/ui/loading/ResortTransferLoadingScreen.cpp
    src/ui/loading/ResortTransferLoadingScreenMotion.cpp
    src/ui/TitleScreen.cpp
    src/ui/AttendTestScreen.cpp
    src/ui/attend/AttendOverlay.cpp
    src/ui/overlay/OverlayCanvas.cpp
    src/ui/transitions/ScreenTransition.cpp
    src/ui/Overworld3DTestScreen.cpp
    src/ui/TransferFlowCoordinator.cpp
    src/ui/title_screen/MainMenuController.cpp
    src/ui/title_screen/OptionsMenuController.cpp
    src/ui/title_screen/ResortMenuController.cpp
    src/ui/title_screen/SectionScreenController.cpp
    src/ui/transfer_flow/TransferFlowController.cpp
    src/ui/transfer_flow/TransferSelectionBuilder.cpp
    src/ui/transfer_ticket/TransferTicketListController.cpp
    src/ui/transfer_ticket/TransferTicketWaveBanner.cpp
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
    src/ui/transfer_system/TransferSystemScreenAdvanceHeldBox.cpp
    src/ui/transfer_system/TransferSystemScreenAdvancePressed.cpp
    src/ui/transfer_system/TransferSystemScreenBackPressed.cpp
    src/ui/transfer_system/TransferSystemScreenUnroutedEvents.cpp
    src/ui/transfer_system/TransferSystemScreenBoxRenameModal.cpp
    src/ui/transfer_system/TransferSystemScreenBoxRenameModalRender.cpp
    src/ui/transfer_system/TransferSystemScreenExitSaveModal.cpp
    src/ui/transfer_system/TransferSystemScreenExitSaveModalRender.cpp
    src/ui/transfer_system/TransferSystemScreenPointerPressed.cpp
    src/ui/transfer_system/TransferSystemScreenPointerMoved.cpp
    src/ui/transfer_system/TransferSystemScreenPointerReleased.cpp
    src/ui/transfer_system/TransferSystemScreenBoxSpacePointer.cpp
    src/ui/transfer_system/TransferSystemScreenActionMenuPointer.cpp
    src/ui/transfer_system/TransferSystemScreenBoxSpaceQuickDrop.cpp
    src/ui/transfer_system/TransferSystemScreenViewportModels.cpp
    src/ui/transfer_system/TransferSystemScreenTransferMarkers.cpp
    src/ui/transfer_system/markers/TransferMarkerState.cpp
    src/ui/transfer_system/TransferSystemScreenBoxSpaceMode.cpp
    src/ui/transfer_system/TransferSystemScreenMultiMarquee.cpp
    src/ui/transfer_system/TransferSystemScreenDebugHooks.cpp
    src/ui/transfer_system/TransferSystemScreenBoxNavigation.cpp
    src/ui/transfer_system/TransferSystemScreenDropdownPointer.cpp
    src/ui/transfer_system/TransferSystemScreenActionMenuHelpers.cpp
    src/ui/transfer_system/TransferSystemScreenHeldItemBridge.cpp
    src/ui/transfer_system/TransferSystemScreenSmallQueries.cpp
    src/ui/transfer_system/TransferSystemScreenUiHelpers.cpp
    src/ui/transfer_system/GameTransferConfig.cpp
    src/ui/transfer_system/summary/PokemonSummaryConfig.cpp
    src/ui/transfer_system/summary/PokemonSummaryContent.cpp
    src/ui/transfer_system/TransferSaveConfig.cpp
    src/ui/transfer_system/summary/TransferSystemSummaryPanel.cpp
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
add_dependencies(title_screen_demo world3d_bgfx_shaders)
add_custom_command(
    TARGET title_screen_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${WORLD3D_BGFX_SHADER_OUTPUT_DIR}
            $<TARGET_FILE_DIR:title_screen_demo>/shaders
)

target_link_libraries(title_screen_demo PRIVATE world3d_bgfx_renderer SDL2::SDL2 SDL2::SDL2main SDL2_image::SDL2_image SDL2_ttf::SDL2_ttf)
if(SQLite3_FOUND)
    target_link_libraries(title_screen_demo PRIVATE SQLite3::SQLite3)
else()
    target_link_libraries(title_screen_demo PRIVATE sqlite3)
endif()

if(APPLE)
    find_path(VORBIS_INCLUDE_DIR vorbis/vorbisfile.h PATHS /opt/homebrew /usr/local PATH_SUFFIXES include)
    find_library(VORBISFILE_LIBRARY vorbisfile PATHS /opt/homebrew /usr/local PATH_SUFFIXES lib)
    find_library(VORBIS_LIBRARY vorbis PATHS /opt/homebrew /usr/local PATH_SUFFIXES lib)
    find_library(OGG_LIBRARY ogg PATHS /opt/homebrew /usr/local PATH_SUFFIXES lib)
    if(VORBIS_INCLUDE_DIR AND VORBISFILE_LIBRARY AND VORBIS_LIBRARY AND OGG_LIBRARY)
        target_include_directories(title_screen_demo PRIVATE ${VORBIS_INCLUDE_DIR})
        target_link_libraries(title_screen_demo PRIVATE ${VORBISFILE_LIBRARY} ${VORBIS_LIBRARY} ${OGG_LIBRARY})
        target_compile_definitions(title_screen_demo PRIVATE PR_ENABLE_VORBIS_AUDIO=1)
    endif()
    target_link_libraries(title_screen_demo PRIVATE "-framework AVFoundation" "-framework Foundation")
endif()

enable_testing()

add_executable(resort_storage_tests
    tests/native/resort/storage_import_tests.cpp
    src/core/config/Json.cpp
    src/core/bridge/SaveBridgeClient.cpp
    ${RESORT_STORAGE_SOURCES}
)
target_include_directories(resort_storage_tests PRIVATE include)
if(SQLite3_FOUND)
    target_link_libraries(resort_storage_tests PRIVATE SQLite3::SQLite3)
else()
    target_link_libraries(resort_storage_tests PRIVATE sqlite3)
endif()
add_test(NAME resort_storage_tests COMMAND resort_storage_tests)

add_executable(openhome_bridge_tests
    tests/native/resort/openhome_bridge_tests.cpp
    src/core/config/Json.cpp
    src/resort/openhome/OpenHomePokemonPayload.cpp
    src/resort/openhome/OpenHomeMovementBridge.cpp
    src/resort/openhome/OpenHomeStorageBridge.cpp
)
target_include_directories(openhome_bridge_tests PRIVATE include)
add_test(NAME openhome_bridge_tests COMMAND openhome_bridge_tests)

add_executable(transfer_marker_resolve_tests
    tests/native/transfer_marker_resolve_tests.cpp
)
target_include_directories(transfer_marker_resolve_tests PRIVATE include)
add_test(NAME transfer_marker_resolve_tests COMMAND transfer_marker_resolve_tests)

add_executable(transfer_contracts_tests
    tests/native/architecture/transfer_contracts_tests.cpp
    src/transfer/application/NullTransferService.cpp
)
target_include_directories(transfer_contracts_tests PRIVATE include)
add_test(NAME transfer_contracts_tests COMMAND transfer_contracts_tests)

add_executable(world_event_bus_tests
    tests/native/architecture/world_event_bus_tests.cpp
    src/gameplay/world3d/events/InMemoryWorldEventBus.cpp
)
target_include_directories(world_event_bus_tests PRIVATE include)
add_test(NAME world_event_bus_tests COMMAND world_event_bus_tests)

add_test(
    NAME architecture_module_rules_check
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check_module_boundaries.sh
)

add_test(
    NAME architecture_docs_freshness_check
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check_docs_freshness.sh
)

add_executable(mutable_merge_policy_tests
    tests/native/resort/mutable_merge_policy_tests.cpp
    src/resort/services/MirrorReturnAnalysis.cpp
    src/resort/services/PokemonMergeService.cpp
    src/resort/domain/ResortRibbonCatalogMerge.cpp
    src/resort/domain/PokemonMergeFieldPolicy.cpp
    src/resort/integration/Gen12DvBytes.cpp
    src/core/config/Json.cpp
    src/core/crypto/Sha256.cpp
)
target_include_directories(mutable_merge_policy_tests PRIVATE include)
add_test(NAME mutable_merge_policy_tests COMMAND mutable_merge_policy_tests)

add_executable(app_transition_controller_tests
    tests/native/core/app_transition_controller_tests.cpp
    src/core/app/transition/AppTransitionController.cpp
)
target_include_directories(app_transition_controller_tests PRIVATE include)
add_test(NAME app_transition_controller_tests COMMAND app_transition_controller_tests)

add_executable(app_frame_requests_tests
    tests/native/core/app_frame_requests_tests.cpp
    src/core/app/frame/AppFrameRequests.cpp
)
target_include_directories(app_frame_requests_tests PRIVATE include)
add_test(NAME app_frame_requests_tests COMMAND app_frame_requests_tests)

add_executable(title_screen_menu_tests
    tests/native/title_screen/main_menu_controller_tests.cpp
    src/ui/title_screen/MainMenuController.cpp
)
target_include_directories(title_screen_menu_tests PRIVATE include)
add_test(NAME title_screen_menu_tests COMMAND title_screen_menu_tests)

add_executable(title_screen_options_tests
    tests/native/title_screen/options_menu_controller_tests.cpp
    src/ui/title_screen/OptionsMenuController.cpp
)
target_include_directories(title_screen_options_tests PRIVATE include)
add_test(NAME title_screen_options_tests COMMAND title_screen_options_tests)

add_executable(title_screen_section_tests
    tests/native/title_screen/section_screen_controller_tests.cpp
    src/ui/title_screen/SectionScreenController.cpp
)
target_include_directories(title_screen_section_tests PRIVATE include)
add_test(NAME title_screen_section_tests COMMAND title_screen_section_tests)

add_executable(resort_menu_controller_tests
    tests/native/title_screen/resort_menu_controller_tests.cpp
    src/ui/title_screen/ResortMenuController.cpp
)
target_include_directories(resort_menu_controller_tests PRIVATE include)
add_test(NAME resort_menu_controller_tests COMMAND resort_menu_controller_tests)

add_executable(attend_scene_config_tests
    tests/native/attend/attend_scene_config_tests.cpp
    src/gameplay/attend/AttendSceneConfig.cpp
    src/gameplay/attend/PokemonModelCatalog.cpp
    src/core/config/Json.cpp
)
target_include_directories(attend_scene_config_tests PRIVATE include)
add_test(NAME attend_scene_config_tests COMMAND attend_scene_config_tests)

add_executable(attend_pokemon_model_tests
    tests/native/attend/attend_pokemon_model_tests.cpp
    src/core/crypto/Sha256.cpp
    src/gameplay/attend/rendering/AttendPokemonAsset.cpp
    src/gameplay/attend/rendering/AttendPokemonMaterialPolicy.cpp
    src/gameplay/attend/rendering/AttendPokemonModel.cpp
    src/core/config/Json.cpp
)
target_include_directories(attend_pokemon_model_tests PRIVATE include)
target_link_libraries(attend_pokemon_model_tests PRIVATE SDL2::SDL2 SDL2_image::SDL2_image PkgConfig::ZSTD)
add_test(NAME attend_pokemon_model_tests COMMAND attend_pokemon_model_tests)

add_executable(attend_eye_socket_mask_tests
    tests/native/attend/attend_eye_socket_mask_tests.cpp
    src/gameplay/attend/rendering/AttendEyeSocketMask.cpp
)
target_include_directories(attend_eye_socket_mask_tests PRIVATE include)
add_test(NAME attend_eye_socket_mask_tests COMMAND attend_eye_socket_mask_tests)

add_executable(attend_pokemon_glbz_tests
    tests/native/attend/attend_pokemon_glbz_tests.cpp
    src/core/crypto/Sha256.cpp
    src/gameplay/attend/rendering/AttendPokemonAsset.cpp
    src/gameplay/attend/rendering/AttendPokemonModel.cpp
    src/core/config/Json.cpp
)
target_include_directories(attend_pokemon_glbz_tests PRIVATE include)
target_link_libraries(attend_pokemon_glbz_tests PRIVATE SDL2::SDL2 SDL2_image::SDL2_image PkgConfig::ZSTD)
add_test(NAME attend_pokemon_glbz_tests COMMAND attend_pokemon_glbz_tests)

add_executable(overlay_canvas_tests
    tests/native/ui/overlay_canvas_tests.cpp
    src/ui/overlay/OverlayCanvas.cpp
    src/core/assets/Assets.cpp
    src/core/assets/Font.cpp
)
target_include_directories(overlay_canvas_tests PRIVATE include)
target_link_libraries(overlay_canvas_tests PRIVATE SDL2::SDL2 SDL2_image::SDL2_image SDL2_ttf::SDL2_ttf)
add_test(NAME overlay_canvas_tests COMMAND overlay_canvas_tests)

add_executable(resort_loading_transition_screen_tests
    tests/native/resort_loading/resort_loading_transition_screen_tests.cpp
    src/core/assets/Assets.cpp
    src/core/assets/Font.cpp
    src/core/config/Json.cpp
    src/ui/loading/PokeballLoadingScreen.cpp
    src/ui/loading/LoadingScreenFactory.cpp
    src/ui/loading/ResortTransferLoadingConfig.cpp
    src/ui/loading/ResortTransferLoadingRenderer.cpp
    src/ui/loading/ResortTransferLoadingScreen.cpp
    src/ui/loading/ResortTransferLoadingScreenMotion.cpp
)
target_compile_definitions(resort_loading_transition_screen_tests PRIVATE PR_ENABLE_TEST_HOOKS)
target_include_directories(resort_loading_transition_screen_tests PRIVATE include)
target_link_libraries(resort_loading_transition_screen_tests PRIVATE SDL2::SDL2 SDL2_image::SDL2_image SDL2_ttf::SDL2_ttf)
add_test(NAME resort_loading_transition_screen_tests COMMAND resort_loading_transition_screen_tests)

add_executable(transfer_ticket_list_tests
    tests/native/transfer_ticket/transfer_ticket_list_controller_tests.cpp
    src/ui/transfer_ticket/TransferTicketListController.cpp
)
target_include_directories(transfer_ticket_list_tests PRIVATE include)
target_link_libraries(transfer_ticket_list_tests PRIVATE SDL2::SDL2)
add_test(NAME transfer_ticket_list_tests COMMAND transfer_ticket_list_tests)

add_executable(transfer_selection_builder_tests
    tests/native/transfer_flow/transfer_selection_builder_tests.cpp
    src/core/domain/PokemonOrigin.cpp
    src/ui/transfer_flow/TransferSelectionBuilder.cpp
)
target_include_directories(transfer_selection_builder_tests PRIVATE include)
add_test(NAME transfer_selection_builder_tests COMMAND transfer_selection_builder_tests)

add_executable(transfer_flow_controller_tests
    tests/native/transfer_flow/transfer_flow_controller_tests.cpp
    src/ui/transfer_flow/TransferFlowController.cpp
)
target_include_directories(transfer_flow_controller_tests PRIVATE include)
add_test(NAME transfer_flow_controller_tests COMMAND transfer_flow_controller_tests)

add_executable(game_transfer_config_tests
    tests/native/transfer_system/game_transfer_config_tests.cpp
    src/core/config/Json.cpp
    src/ui/transfer_system/GameTransferConfig.cpp
)
target_include_directories(game_transfer_config_tests PRIVATE include)
add_test(NAME game_transfer_config_tests COMMAND game_transfer_config_tests)

add_executable(pokemon_summary_config_tests
    tests/native/transfer_system/pokemon_summary_config_tests.cpp
    src/core/config/Json.cpp
    src/ui/transfer_system/summary/PokemonSummaryConfig.cpp
)
target_include_directories(pokemon_summary_config_tests PRIVATE include)
add_test(NAME pokemon_summary_config_tests COMMAND pokemon_summary_config_tests)

add_executable(transfer_info_banner_presenter_tests
    tests/native/transfer_system/transfer_info_banner_presenter_tests.cpp
    src/core/domain/PokemonBall.cpp
    src/core/domain/PokemonOrigin.cpp
    src/ui/transfer_system/TransferInfoBannerPresenter.cpp
)
target_include_directories(transfer_info_banner_presenter_tests PRIVATE include)
add_test(NAME transfer_info_banner_presenter_tests COMMAND transfer_info_banner_presenter_tests)

add_executable(transfer_system_ui_state_controller_tests
    tests/native/transfer_system/transfer_system_ui_state_controller_tests.cpp
    src/ui/transfer_system/TransferSystemUiStateController.cpp
)
target_include_directories(transfer_system_ui_state_controller_tests PRIVATE include)
add_test(NAME transfer_system_ui_state_controller_tests COMMAND transfer_system_ui_state_controller_tests)

add_executable(game_box_browser_controller_tests
    tests/native/transfer_system/game_box_browser_controller_tests.cpp
    src/ui/transfer_system/GameBoxBrowserController.cpp
)
target_include_directories(game_box_browser_controller_tests PRIVATE include)
add_test(NAME game_box_browser_controller_tests COMMAND game_box_browser_controller_tests)

add_executable(pokemon_action_menu_controller_tests
    tests/native/transfer_system/pokemon_action_menu_controller_tests.cpp
    src/ui/transfer_system/PokemonActionMenuController.cpp
)
target_include_directories(pokemon_action_menu_controller_tests PRIVATE include)
target_link_libraries(pokemon_action_menu_controller_tests PRIVATE SDL2::SDL2)
add_test(NAME pokemon_action_menu_controller_tests COMMAND pokemon_action_menu_controller_tests)

add_executable(multi_pokemon_move_controller_tests
    tests/native/transfer_system/multi_pokemon_move_controller_tests.cpp
    src/ui/transfer_system/MultiPokemonMoveController.cpp
)
target_include_directories(multi_pokemon_move_controller_tests PRIVATE include)
target_link_libraries(multi_pokemon_move_controller_tests PRIVATE SDL2::SDL2)
add_test(NAME multi_pokemon_move_controller_tests COMMAND multi_pokemon_move_controller_tests)

add_executable(transfer_system_focus_graph_tests
    tests/native/transfer_system/transfer_system_focus_graph_tests.cpp
    src/ui/transfer_system/TransferSystemFocusGraph.cpp
)
target_include_directories(transfer_system_focus_graph_tests PRIVATE include)
target_link_libraries(transfer_system_focus_graph_tests PRIVATE SDL2::SDL2)
add_test(NAME transfer_system_focus_graph_tests COMMAND transfer_system_focus_graph_tests)

add_executable(input_router_tests
    tests/native/core/input_router_tests.cpp
    src/core/input/InputBindings.cpp
    src/core/input/InputRouter.cpp
)
target_include_directories(input_router_tests PRIVATE include)
target_link_libraries(input_router_tests PRIVATE SDL2::SDL2)
add_test(NAME input_router_tests COMMAND input_router_tests)

add_executable(config_loader_tests
    tests/native/core/config_loader_tests.cpp
    src/core/config/ConfigLoader.cpp
    src/core/config/Json.cpp
)
target_include_directories(config_loader_tests PRIVATE include)
add_test(NAME config_loader_tests COMMAND config_loader_tests)

add_executable(owmap_overworld_loader_tests
    tests/native/world3d/owmap_overworld_loader_tests.cpp
    src/core/config/Json.cpp
    src/gameplay/world3d/data/SceneMetadataParser.cpp
    src/gameplay/world3d/data/JsonOverworldLoader.cpp
    src/gameplay/world3d/data/OwmapOverworldLoader.cpp
    src/gameplay/world3d/characters/CharacterController.cpp
    src/gameplay/world3d/characters/CharacterTerrainQuery.cpp
    src/gameplay/world3d/characters/GridActorMotor.cpp
    src/gameplay/world3d/terrain/ActorTerrainBinding.cpp
    src/gameplay/world3d/terrain/GridStepMotor.cpp
    src/gameplay/world3d/terrain/TerrainSurface.cpp
    src/gameplay/world3d/followers/FollowerConfig.cpp
    src/gameplay/world3d/rendering/BillboardPlacement.cpp
    src/gameplay/world3d/rendering/PixelScale.cpp
    src/gameplay/world3d/rendering/WorldBillboardCompositor.cpp
    src/gameplay/world3d/camera/Gen4CameraPreset.cpp
    src/gameplay/world3d/camera/Gen4FollowCamera.cpp
)
target_include_directories(owmap_overworld_loader_tests PRIVATE include)
target_link_libraries(owmap_overworld_loader_tests PRIVATE SDL2::SDL2)
add_test(NAME owmap_overworld_loader_tests COMMAND owmap_overworld_loader_tests)

add_executable(glb_model_animation_tests
    tests/native/world3d/glb_model_animation_tests.cpp
    src/core/config/Json.cpp
    src/gameplay/world3d/data/GlbModelLoader.cpp
    src/gameplay/world3d/data/GlbModelAnimation.cpp
)
target_include_directories(glb_model_animation_tests PRIVATE include)
add_test(NAME glb_model_animation_tests COMMAND glb_model_animation_tests)

add_executable(rtpks_ocean_tile_tests
    tests/native/world3d/rtpks_ocean_tile_tests.cpp
    src/core/config/Json.cpp
)
target_include_directories(rtpks_ocean_tile_tests PRIVATE include)
target_compile_definitions(rtpks_ocean_tile_tests PRIVATE PR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(rtpks_ocean_tile_tests PRIVATE world3d_bgfx_renderer)
add_test(NAME rtpks_ocean_tile_tests COMMAND rtpks_ocean_tile_tests)

add_executable(overworld_textbox_tests
    tests/native/world3d/overworld_textbox_tests.cpp
    src/core/config/Json.cpp
    src/core/assets/Font.cpp
    src/gameplay/world3d/dialogue/OverworldTextboxConfig.cpp
    src/gameplay/world3d/dialogue/OverworldTextboxController.cpp
    src/gameplay/world3d/dialogue/OverworldTextboxRenderer.cpp
    src/ui/overlay/OverlaySliceLayout.cpp
)
target_include_directories(overworld_textbox_tests PRIVATE include)
target_link_libraries(overworld_textbox_tests PRIVATE SDL2::SDL2 SDL2_image::SDL2_image SDL2_ttf::SDL2_ttf)
add_test(NAME overworld_textbox_tests COMMAND overworld_textbox_tests)

add_executable(interaction_system_tests
    tests/native/world3d/interaction_system_tests.cpp
    src/core/config/Json.cpp
    src/gameplay/world3d/characters/SpriteSheetAnimator.cpp
    src/gameplay/world3d/interactions/InteractionSequence.cpp
    src/gameplay/world3d/interactions/InteractionText.cpp
    src/gameplay/world3d/npc/ResortPokemonSpawnConfig.cpp
    src/gameplay/world3d/scripts/OverworldScript.cpp
    src/ui/transitions/ScreenTransition.cpp
)
target_include_directories(interaction_system_tests PRIVATE include)
target_link_libraries(interaction_system_tests PRIVATE SDL2::SDL2)
add_test(NAME interaction_system_tests COMMAND interaction_system_tests)

add_executable(transfer_box_edits_store_tests
    tests/native/core/transfer_box_edits_store_tests.cpp
    src/core/save/TransferBoxEditsStore.cpp
    src/core/config/Json.cpp
)
target_include_directories(transfer_box_edits_store_tests PRIVATE include)
add_test(NAME transfer_box_edits_store_tests COMMAND transfer_box_edits_store_tests)

add_executable(bridge_import_merge_tests
    tests/native/core/bridge_import_merge_tests.cpp
    src/core/bridge/BridgeImportMerge.cpp
    src/core/config/Json.cpp
)
target_include_directories(bridge_import_merge_tests PRIVATE include)
add_test(NAME bridge_import_merge_tests COMMAND bridge_import_merge_tests)

add_executable(pcslot_imported_pokemon_builder_tests
    tests/native/resort/pcslot_imported_pokemon_builder_tests.cpp
    src/resort/integration/BridgeImportAdapter.cpp
    src/core/config/Json.cpp
)
target_include_directories(pcslot_imported_pokemon_builder_tests PRIVATE include)
add_test(NAME pcslot_imported_pokemon_builder_tests COMMAND pcslot_imported_pokemon_builder_tests)

add_executable(save_bridge_client_tests
    tests/native/core/save_bridge_client_tests.cpp
    src/core/bridge/SaveBridgeClient.cpp
    src/core/config/Json.cpp
)
target_include_directories(save_bridge_client_tests PRIVATE include)
add_test(NAME save_bridge_client_tests COMMAND save_bridge_client_tests)

add_executable(pokesprite_assets_tests
    tests/native/core/pokesprite_assets_tests.cpp
    src/core/config/Json.cpp
    src/core/assets/PokeSpriteAssets.cpp
)
target_include_directories(pokesprite_assets_tests PRIVATE include)
target_link_libraries(pokesprite_assets_tests PRIVATE SDL2::SDL2 SDL2_image::SDL2_image SDL2_ttf::SDL2_ttf)
add_test(NAME pokesprite_assets_tests COMMAND pokesprite_assets_tests)

add_executable(pokemon_cry_assets_tests
    tests/native/core/pokemon_cry_assets_tests.cpp
    src/core/app/audio/PokemonCryPlayer.cpp
    src/core/app/frame/AppFrameRequests.cpp
    src/core/assets/PokemonCryAssets.cpp
)
target_include_directories(pokemon_cry_assets_tests PRIVATE include)
add_test(NAME pokemon_cry_assets_tests COMMAND pokemon_cry_assets_tests)

add_executable(save_library_cache_tests
    tests/native/core/save_library_cache_tests.cpp
    src/core/config/Json.cpp
    src/core/bridge/SaveBridgeClient.cpp
    src/core/save/SaveLibrary.cpp
)
target_include_directories(save_library_cache_tests PRIVATE include)
add_test(NAME save_library_cache_tests COMMAND save_library_cache_tests)

add_executable(input_config_integration_tests
    tests/native/core/input_config_integration_tests.cpp
    src/core/config/ConfigLoader.cpp
    src/core/input/InputBindings.cpp
    src/core/input/InputRouter.cpp
    src/core/config/Json.cpp
)
target_include_directories(input_config_integration_tests PRIVATE include)
target_link_libraries(input_config_integration_tests PRIVATE SDL2::SDL2)
add_test(NAME input_config_integration_tests COMMAND input_config_integration_tests)

add_test(
    NAME title_screen_headless_smoke
    COMMAND /bin/zsh ${CMAKE_SOURCE_DIR}/tests/e2e/native/run_title_screen_headless_smoke.sh
)

add_executable(title_screen_flow_harness_tests
    tests/e2e/native/title_screen_flow_harness_tests.cpp
    src/core/assets/Assets.cpp
    src/core/config/ConfigLoader.cpp
    src/core/assets/Font.cpp
    src/core/input/InputBindings.cpp
    src/core/input/InputRouter.cpp
    src/core/config/Json.cpp
    src/ui/TitleScreen.cpp
    src/ui/title_screen/MainMenuController.cpp
    src/ui/title_screen/OptionsMenuController.cpp
    src/ui/title_screen/ResortMenuController.cpp
    src/ui/title_screen/SectionScreenController.cpp
    src/ui/title_screen/TitleScreenRender.cpp
)
target_compile_definitions(title_screen_flow_harness_tests PRIVATE PR_ENABLE_TEST_HOOKS)
target_include_directories(title_screen_flow_harness_tests PRIVATE include)
target_link_libraries(title_screen_flow_harness_tests PRIVATE SDL2::SDL2 SDL2_image::SDL2_image SDL2_ttf::SDL2_ttf)
add_test(NAME title_screen_flow_harness_tests COMMAND title_screen_flow_harness_tests)

add_executable(transfer_system_flow_harness_tests
    tests/e2e/native/transfer_system_flow_harness_tests.cpp
    src/transfer/application/OpenHomeMovementOrchestrator.cpp
    src/transfer/application/TransferCommitOrchestrator.cpp
    src/core/assets/Assets.cpp
    src/core/config/ConfigLoader.cpp
    src/core/assets/Font.cpp
    src/core/config/Json.cpp
    src/core/domain/PokemonBall.cpp
    src/core/domain/PokemonOrigin.cpp
    src/core/assets/PokeSpriteAssets.cpp
    src/core/bridge/SaveBridgeClient.cpp
    src/core/save/TransferBoxEditsStore.cpp
    src/core/bridge/BridgeImportMerge.cpp
    src/transfer/application/TransferCommitOrchestrator.cpp
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
    src/ui/transfer_system/TransferSystemScreenAdvanceHeldBox.cpp
    src/ui/transfer_system/TransferSystemScreenAdvancePressed.cpp
    src/ui/transfer_system/TransferSystemScreenBackPressed.cpp
    src/ui/transfer_system/TransferSystemScreenUnroutedEvents.cpp
    src/ui/transfer_system/TransferSystemScreenBoxRenameModal.cpp
    src/ui/transfer_system/TransferSystemScreenBoxRenameModalRender.cpp
    src/ui/transfer_system/TransferSystemScreenExitSaveModal.cpp
    src/ui/transfer_system/TransferSystemScreenExitSaveModalRender.cpp
    src/ui/transfer_system/TransferSystemScreenPointerPressed.cpp
    src/ui/transfer_system/TransferSystemScreenPointerMoved.cpp
    src/ui/transfer_system/TransferSystemScreenPointerReleased.cpp
    src/ui/transfer_system/TransferSystemScreenBoxSpacePointer.cpp
    src/ui/transfer_system/TransferSystemScreenActionMenuPointer.cpp
    src/ui/transfer_system/TransferSystemScreenBoxSpaceQuickDrop.cpp
    src/ui/transfer_system/TransferSystemScreenViewportModels.cpp
    src/ui/transfer_system/TransferSystemScreenTransferMarkers.cpp
    src/ui/transfer_system/markers/TransferMarkerState.cpp
    src/ui/transfer_system/TransferSystemScreenBoxSpaceMode.cpp
    src/ui/transfer_system/TransferSystemScreenMultiMarquee.cpp
    src/ui/transfer_system/TransferSystemScreenDebugHooks.cpp
    src/ui/transfer_system/TransferSystemScreenBoxNavigation.cpp
    src/ui/transfer_system/TransferSystemScreenDropdownPointer.cpp
    src/ui/transfer_system/TransferSystemScreenActionMenuHelpers.cpp
    src/ui/transfer_system/TransferSystemScreenHeldItemBridge.cpp
    src/ui/transfer_system/TransferSystemScreenSmallQueries.cpp
    src/ui/transfer_system/TransferSystemScreenUiHelpers.cpp
    src/ui/transfer_system/GameTransferConfig.cpp
    src/ui/transfer_system/summary/PokemonSummaryConfig.cpp
    src/ui/transfer_system/summary/PokemonSummaryContent.cpp
    src/ui/transfer_system/TransferSaveConfig.cpp
    src/ui/transfer_system/summary/TransferSystemSummaryPanel.cpp
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
    tests/e2e/native/transfer_ticket_unicode_harness_tests.cpp
    src/core/assets/Assets.cpp
    src/core/config/ConfigLoader.cpp
    src/core/assets/Font.cpp
    src/core/config/Json.cpp
    src/core/assets/PokeSpriteAssets.cpp
    src/ui/TransferTicketScreen.cpp
    src/ui/transfer_ticket/TransferTicketListController.cpp
    src/ui/transfer_ticket/TransferTicketWaveBanner.cpp
)
target_compile_definitions(transfer_ticket_unicode_harness_tests PRIVATE PR_ENABLE_TEST_HOOKS)
target_include_directories(transfer_ticket_unicode_harness_tests PRIVATE include)
target_link_libraries(transfer_ticket_unicode_harness_tests PRIVATE SDL2::SDL2 SDL2_image::SDL2_image SDL2_ttf::SDL2_ttf)
add_test(NAME transfer_ticket_unicode_harness_tests COMMAND transfer_ticket_unicode_harness_tests)

add_executable(resort_backend_tool
    tools/resort_backend_tool.cpp
    src/core/config/Json.cpp
    src/core/bridge/SaveBridgeClient.cpp
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
        ${CMAKE_SOURCE_DIR}/tools/pkhex_bridge/PKHeXBridge.csproj
        -c Release
        -r osx-arm64
        --self-contained true
        -o ${CMAKE_BINARY_DIR}/pkhex_bridge/osx-arm64
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Publishing self-contained PKHeX bridge for macOS"
)
