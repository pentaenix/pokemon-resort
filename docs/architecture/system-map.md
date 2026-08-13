# System Map (Domain-First)

## Domains
- `engine`: reusable runtime systems (rendering/input/audio/camera core/chunk runtime).
- `gameplay`: game rules and 3D world behavior (player/NPC/dialogue/events/quests/followers).
- `gameplay/attend`: Pokemon interaction-scene config and rendering helpers; currently used by the `RESORT -> TEST ATTEND` debug scene. `config/gameplay/pokemon_attend.json` is a manifest that links concern-specific debug, interaction, provider, and environment files under `config/gameplay/pokemon_attend/` so temporary scene selections, shared interaction tuning, Pokemon-provider behavior, and Alola-map presentation stay separate. The attend Pokemon renderer consumes provider/exporter policy metadata such as RAE `renderClass`, sampler wrap, eye-sheet UVs, and mesh draw order behind this module boundary.
- `transfer`: save movement, OpenHome identity, transfer orchestration.
- `ui`: presentation and screen-level adapters, including reusable logical-coordinate overlays under `ui/overlay`.
- `data`: authored configs and content packs.
- `tools/map_maker`: standalone map-authoring application. Its pure document/project/command/selection/validation core owns edits; its preview adapter consumes the game world loader and renderer without becoming a runtime dependency.

## Dependency Direction
- `engine` -> no dependency on `gameplay`, `transfer`, `ui`.
- `gameplay` -> may depend on `engine` and `transfer/contracts` only.
- future `gameplay/test_attend` behavior should follow the `gameplay` dependency rule and must not import `resort` or transfer internals.
- `transfer` -> owns save adapters and persistence; exports app-facing contracts.
- `ui` -> may compose `engine`, `gameplay`, and `transfer/contracts`.
- `ui/overlay` -> shared SDL overlay primitives only; consuming screens own semantic actions such as weather, tutorials, or overworld controls.
- `tools/map_maker` -> may consume `core/config` JSON and public `gameplay/world3d` data, camera, terrain, and rendering seams. Production `engine`, `gameplay`, `transfer`, `ui`, and `resort` modules must not consume `mapmaker/*`.

## Contracts
- `transfer/contracts/TransferContracts.hpp`
  - `TransferService`
  - `PartySnapshotProvider`
- `gameplay/contracts/WorldEventBus.hpp`
  - `WorldEventBus`
- `gameplay/world3d/rendering/bgfx/OverworldBgfxRenderer::renderEmbeddedViewport`
  - submits exact world passes and returns a non-owning renderer texture; the tool host owns final composition and the single `bgfx::frame()` call

## Migration Intent
This structure is introduced as groundwork. Existing production code remains functional while incremental migrations move responsibilities into domain-aligned folders.
