# System Map (Domain-First)

## Domains
- `engine`: reusable runtime systems (rendering/input/audio/camera core/chunk runtime).
- `gameplay`: game rules and 3D world behavior (player/NPC/dialogue/events/quests/followers).
- `gameplay/attend`: Pokemon interaction-scene config and rendering helpers; currently used by the `RESORT -> TEST ATTEND` preview. Scene composition, Pokemon provider defaults, the grass-field environment floor, and edge-proof sky/light presets are kept in separate `config/gameplay/pokemon_attend` files so Pokemon model-source swaps do not rewrite the whole scene. The attend Pokemon renderer consumes provider/exporter policy metadata such as RAE `renderClass`, sampler wrap, eye-sheet UVs, and mesh draw order behind this module boundary.
- `transfer`: save movement, OpenHome identity, transfer orchestration.
- `ui`: presentation and screen-level adapters, including reusable logical-coordinate overlays under `ui/overlay`.
- `data`: authored configs and content packs.

## Dependency Direction
- `engine` -> no dependency on `gameplay`, `transfer`, `ui`.
- `gameplay` -> may depend on `engine` and `transfer/contracts` only.
- future `gameplay/test_attend` behavior should follow the `gameplay` dependency rule and must not import `resort` or transfer internals.
- `transfer` -> owns save adapters and persistence; exports app-facing contracts.
- `ui` -> may compose `engine`, `gameplay`, and `transfer/contracts`.
- `ui/overlay` -> shared SDL overlay primitives only; consuming screens own semantic actions such as weather, tutorials, or overworld controls.

## Contracts
- `transfer/contracts/TransferContracts.hpp`
  - `TransferService`
  - `PartySnapshotProvider`
- `gameplay/contracts/WorldEventBus.hpp`
  - `WorldEventBus`

## Migration Intent
This structure is introduced as groundwork. Existing production code remains functional while incremental migrations move responsibilities into domain-aligned folders.
