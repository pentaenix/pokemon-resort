# Architecture Decision Records (ADRs)

Use ADRs for decisions that change:
- domain boundaries
- dependency rules
- transfer/gameplay integration contracts
- persistence ownership

File naming:
- `NNNN-short-title.md` (e.g., `0001-transfer-contract-boundary.md`)

Accepted decisions:

- [`0001-native-map-maker.md`](0001-native-map-maker.md): standalone SDL2/bgfx/ImGui editor with a lossless OWMAP core and exact runtime-renderer preview seam.
- [`0002-shared-aquarium-geometry-kernel.md`](0002-shared-aquarium-geometry-kernel.md): one deterministic native/WASM kernel owns player-aquarium geometry, collision, and navigation derivation.
