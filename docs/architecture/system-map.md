# System Map (Domain-First)

## Domains
- `engine`: reusable runtime systems (rendering/input/audio/camera core/chunk runtime).
- `gameplay`: game rules and 3D world behavior (player/NPC/dialogue/events/quests/followers).
- `transfer`: save movement, OpenHome identity, transfer orchestration.
- `ui`: presentation and screen-level adapters.
- `data`: authored configs and content packs.

## Dependency Direction
- `engine` -> no dependency on `gameplay`, `transfer`, `ui`.
- `gameplay` -> may depend on `engine` and `transfer/contracts` only.
- `transfer` -> owns save adapters and persistence; exports app-facing contracts.
- `ui` -> may compose `engine`, `gameplay`, and `transfer/contracts`.

## Contracts
- `transfer/contracts/TransferContracts.hpp`
  - `TransferService`
  - `PartySnapshotProvider`
- `gameplay/contracts/WorldEventBus.hpp`
  - `WorldEventBus`

## Migration Intent
This structure is introduced as groundwork. Existing production code remains functional while incremental migrations move responsibilities into domain-aligned folders.
