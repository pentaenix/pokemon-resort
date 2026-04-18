# Pokemon Resort Architecture

## Current State

This repository currently contains one playable SDL2 application in [`pokemon-resort`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort) plus a top-level [`saves`](/Users/vanta/Desktop/title_screen_demo/saves) folder with external `.sav` files that are not part of the app runtime.

The app is a data-driven title-screen prototype with these implemented flows:

- splash logo fade in, hold, and fade out
- main logo reveal and white flash
- title hold with blinking `PRESS START`
- transition into the main menu
- main menu navigation for `RESORT`, `TRANSFER`, `TRADE`, and `OPTIONS`
- options menu with persisted user settings
- placeholder section screen flow for `RESORT` and `TRADE`
- loading screen while transfer saves are scanned and probed
- transfer save-ticket selection flow into a post-ticket transfer UI shell

At the moment, most gameplay-facing behavior still lives in a single scene controller: [`TitleScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TitleScreen.cpp).

## Runtime Architecture

### Entry and boot

- [`main.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/main.cpp) forwards an optional config path into `pr::runApplication`.
  It also supports `--clear-save-cache` for deleting the transfer probe cache and exiting.
- [`App.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/App.cpp) is the runtime coordinator. It:
  - finds the project root by locating `config/app.json` and `config/title_screen.json`
  - loads shared app config and title-screen config into strongly typed structs
  - initializes SDL, SDL_image, and SDL_ttf
  - creates the window and renderer
  - loads textures, fonts, and text surfaces
  - restores saved user settings if persistence is enabled
  - owns the main event/update/render loop
  - bridges scene output to audio playback and save writes

### Core modules

- [`ConfigLoader.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/ConfigLoader.cpp)
  Deserializes `app.json` into shared [`AppConfig`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/Types.hpp) and `title_screen.json` into [`TitleScreenConfig`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/Types.hpp). `App.cpp` applies the app-level window, input, and audio config over the title config before building the runtime.

- [`Types.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/Types.hpp)
  Defines the config schema and persisted settings schema. This file is effectively the contract between JSON authoring, runtime behavior, and save serialization.

- [`Assets.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/Assets.cpp)
  Resolves asset paths relative to the project root, loads textures, renders text into textures, and builds the alpha mask used for the logo shine effect.

- [`InputBindings.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/InputBindings.cpp)
  Maps human-readable binding names from JSON to SDL keycodes and is used by the app loop for keyboard navigation and activation. `App.cpp` owns the shared hold-to-repeat timing for vertical menu navigation so individual screens only receive discrete `onNavigate(delta)` actions.

- [`ScreenInput.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/ui/ScreenInput.hpp)
  Defines the small reusable input surface for screens. New pages should inherit from `ScreenInput` and override only the actions they support, such as `canNavigate()`, `onNavigate(delta)`, `onAdvancePressed()`, `onBackPressed()`, or pointer handlers. This keeps shared keyboard/controller repeat and pointer dispatch in `App.cpp` instead of duplicating it per screen.

- [`SaveDataStore.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/SaveDataStore.cpp)
  Loads from primary then backup save files, tolerates older flat save shapes, and writes atomically through a temporary file plus backup refresh.

- [`SaveBridgeClient.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/SaveBridgeClient.cpp)
  Launches the external .NET helper under [`tools/pkhex_bridge`](/Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge), captures stdout and stderr, and keeps the `PKHeX.Core` integration outside the native binary. It now prefers a bundled or published helper executable and falls back to development-time `dotnet run` only when needed.

- [`SaveLibrary.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/SaveLibrary.cpp)
  Scans the top-level workspace [`saves`](/Users/vanta/Desktop/title_screen_demo/saves) folder without recursion, records file metadata, and probes each candidate through the bridge during startup.

- [`Audio.mm`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/Audio.mm)
  Provides the audio controller used by `App.cpp` for looped menu music and button sound effects.

### UI / scene layer

- [`TitleScreen.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/ui/TitleScreen.hpp)
  Defines the `TitleState` enum and the public scene API used by the app loop.

- [`TitleScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TitleScreen.cpp)
  Contains the current state machine and almost all player-facing behavior:
  - timed state transitions
  - skip behavior for authoring and iteration
  - menu navigation and selection
  - options interactions and pending save requests
  - section-screen entry/exit
  - all scene rendering
  - button hit testing
  - logo shine generation and animation

- [`TransferTicketScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TransferTicketScreen.cpp)
  Owns the transfer save-ticket list, ticket rendering, rip animation, and selected-save handoff. It renders directly in the shared `1280x800` logical/design coordinate system; there is no internal 512-to-1280 translation layer. It receives already parsed transfer summaries from `App.cpp`; bridge probing, hashing, and cache reads stay inside `SaveLibrary`.

- [`LoadingScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/LoadingScreen.cpp)
  Owns the black loading screen shown while transfer save probing runs in the background. It reads [`loading_screen.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/loading_screen.json), picks random ball PNGs from the configured loading asset directory, and swaps balls after each animation lap.

- [`TransferSystemScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TransferSystemScreen.cpp)
  Owns the post-ticket transfer UI shell. For now it only renders the same configurable moving background used by the transfer ticket page; future transfer UI elements should get their own dedicated config file rather than growing `transfer_select_save.json`.

## Portability Goals

This project should stay easy to move across operating systems over time. The preferred shape is:

- shared gameplay and UI logic in portable C++ where possible
- OS-specific behavior isolated behind small adapter modules
- data and authoring in JSON or files rather than hard-coded platform branches
- build and packaging differences handled in the build system or platform entry points, not scattered through scene code

When adding or changing behavior, assume future support for Linux, Windows, and Android is a real goal unless the change is explicitly platform-only.

### What Should Stay Shared

- scene state and transitions
- menu flow and interaction rules
- layout and timing values that can be expressed in config
- save schema and persistence rules
- asset lookup policy, as long as the paths can be resolved through a platform-aware helper

### What Should Be Isolated

- audio backends
- app lifecycle and window creation details
- filesystem roots and save locations
- controller, keyboard, and touch input translation
- external helper processes or native bridges
- packaging-specific logic, such as bundle/resource lookup

### Working Rule For New Code

If a change can be written as an intent, config value, or scene event, prefer that over a direct OS call inside UI code. If a change genuinely needs platform APIs, keep the boundary thin and document it in this file.

## State Machine

The title screen currently moves through these runtime states:

1. `SplashFadeIn`
2. `SplashHold`
3. `SplashFadeOut`
4. `MainLogoOnBlack`
5. `WhiteFlash`
6. `TitleHold`
7. `WaitingForStart`
8. `StartTransition`
9. `MainMenuIntro`
10. `MainMenuIdle`
11. `MainMenuToSection`
12. `MainMenuSectionFade`
13. `OptionsIntro`
14. `OptionsIdle`
15. `OptionsOutro`
16. `SectionScreen`

This means the current architecture is scene-centric rather than screen-per-flow. The `TitleScreen` object acts as both controller and renderer for the entire front-end experience.

## Data Flow

### Startup pipeline

1. [`main.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/main.cpp) calls `runApplication`.
2. [`App.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/App.cpp) finds the root and loads [`config/app.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/app.json) plus [`config/title_screen.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/title_screen.json).
3. [`ConfigLoader.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/ConfigLoader.cpp) maps JSON into [`Types.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/Types.hpp) structs.
4. SDL subsystems are initialized and the renderer logical size is configured.
5. [`Assets.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/Assets.cpp) loads textures and text assets.
6. [`SaveDataStore.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/SaveDataStore.cpp) restores persisted options into `TitleScreen`.
7. The app loop starts. When the player opens TRANSFER, `App.cpp` switches to `LoadingScreen`, runs `SaveLibrary::refreshForTransferPage()` on a background task, then passes `transferPageRecords()` summaries into the ticket UI on the main thread.

### Frame pipeline

1. `App.cpp` polls SDL events.
2. Keyboard, mouse, and controller events are translated into `ScreenInput` actions for the active screen. Vertical menu navigation uses a short app-level hold delay before repeating, instead of relying on OS key-repeat.
3. The active screen updates. For transfer entry, `LoadingScreen::update(dt)` animates while the save-probe task runs.
4. `App.cpp` reads scene side effects:
   - whether menu music should be playing
   - current music and SFX volume
   - whether a button SFX should fire
   - whether user settings should be persisted
5. The active screen renders.
6. SDL presents the frame.

### Persistence pipeline

1. Options state changes inside `TitleScreen`.
2. `TitleScreen` raises a save request flag.
3. `App.cpp` converts current scene settings into `SaveData`.
4. [`SaveDataStore.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/SaveDataStore.cpp) writes the primary save and refreshes the backup.

## Configuration Surface

Shared application settings live in [`app.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/app.json). It currently controls:

- physical dev window size
- renderer logical size / virtual resolution
- design coordinate size
- app window title
- shared input bindings
- shared audio asset paths and default volumes

The current target/design resolution is `1280x800`. The development window is intentionally half-size at `640x400`; keep `virtual_width`, `virtual_height`, `design_width`, and `design_height` at `1280x800` unless the target device resolution itself changes. Only change `window.width` and `window.height` when resizing the desktop preview window.

The main title-screen authoring file is [`title_screen.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/title_screen.json). It currently controls:

- asset paths
- timing values for intro phases and transitions
- prompt text, color, and font size
- layout positions
- transition endpoints and speed scales
- menu labels, animation, and selection behavior
- save naming and app identity for `SDL_GetPrefPath`
- skip flags used to shorten iteration loops
- logo shine tuning

If a behavior is timing/layout/content related, JSON is the first place to look. If a behavior changes control flow or rendering rules, it is probably still hard-coded in `TitleScreen.cpp`.

Menu labels are data-driven in `title_screen.json`, but menu actions are currently routed by named indices in [`TitleScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TitleScreen.cpp). Adding a new top-level item requires updating both the `menu.items` array and the corresponding selection routing / placeholder section mapping in `TitleScreen`.

The transfer save-ticket page is authored in [`transfer_select_save.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/transfer_select_save.json). Its screen/list coordinates are native `1280x800` logical pixels. Ticket text offsets are local to the ticket art, so they can stay relative to `main_left.png` / `main_right.png` even when full-screen background or banner assets change. `TransferSystemScreen` currently reuses only the `transfer_screen.background_animation` settings and background asset from this file; the actual transfer UI should use its own JSON.

## Build and Platform Notes

- Build system: [`CMakeLists.txt`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/CMakeLists.txt)
- Primary target: `title_screen_demo`
- Language mode: C++17 plus Objective-C++ for audio today; treat that as an implementation detail, not a requirement for future platforms
- Current platform assumptions: macOS with `SDL2`, `SDL2_image`, and `SDL2_ttf`; `AVFoundation` and `Foundation` are linked on Apple
- Save parsing bridge: external .NET helper under [`tools/pkhex_bridge`](/Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge), intended to reference `PKHeX.Core`
- Export path: publish the bridge as a self-contained macOS helper and bundle it next to the native executable or inside app resources

For cross-platform work, avoid introducing new macOS-only dependencies in core modules unless there is a clear adapter layer or a fallback path for the other target platforms.

## Architectural Strengths

- Clear separation between app loop, config loading, assets, input, persistence, and scene logic
- Strong typed config boundary through [`Types.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/Types.hpp)
- Data-driven tuning for many visual and interaction parameters
- Save writes are defensive and backup-aware

## Current Architectural Constraints

- [`TitleScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TitleScreen.cpp) is the main bottleneck. It owns state machine logic, rendering, hit testing, options logic, and section placeholder behavior.
- There is only one concrete scene class, so the app loop cannot yet switch among independent screens.
- There are no automated tests in the repository yet.
- Audio is coordinated externally by `App.cpp` through scene flags rather than through a more explicit event/effect system.
- `PKHeX.Core` is not linked into the native app directly; save probing currently depends on an external helper process and local .NET SDK/runtime availability.

## Recommended Next Refactors

### Near-term

- Split `TitleScreen` into smaller collaborators such as:
  - intro/title sequence controller
  - main menu controller
  - options menu controller
  - section screen controller
- Continue moving new pages behind the lightweight `ScreenInput` surface so `App.cpp` can coordinate input generically instead of adding per-page input branches.
- Move menu action results into explicit intents or events instead of several boolean consume methods.
- Add thin platform/service interfaces for audio, save-path resolution, and helper-process spawning so those concerns can be swapped without touching scene code.
- Prefer config-driven layout and asset selection over hard-coded coordinates or platform checks inside render logic.

### Mid-term

- Add tests for:
  - config loading defaults and overrides
  - save load fallback behavior
  - save atomic-write error handling
  - input binding parsing
- Separate render concerns from state transitions if the UI grows beyond the current title/menu scope.
- Add platform-specific build notes for Windows, Linux, and Android once those targets become active, including any required runtime assets or packaging steps.

## LLM Handoff Notes

If another model is asked to work in this repo, the best initial reading order is:

1. [`pokemon-resort/docs/ARCHITECTURE.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/ARCHITECTURE.md)
2. [`pokemon-resort/config/title_screen.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/title_screen.json)
3. [`pokemon-resort/include/core/Types.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/Types.hpp)
4. [`pokemon-resort/src/core/App.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/App.cpp)
5. [`pokemon-resort/src/ui/TitleScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TitleScreen.cpp)

That sequence gives the fastest path to understanding current behavior, extension points, and where complexity currently lives.

## Guidance For Future Agents

When making changes here, keep these habits in mind:

- State whether the source of truth is JSON config, runtime code, or persisted save data before editing behavior.
- Prefer adding a boundary rather than teaching `App.cpp` or `TitleScreen.cpp` about a new operating system.
- If a feature is likely to vary by platform, start with a shared interface and a single implementation, then add the alternate backend only when needed.
- Keep docs updated when module boundaries or portability assumptions change, especially if a new dependency makes the project harder to move between operating systems.
- Be wary of one-off platform checks in scene logic; they tend to age badly and make the app harder to port later.
