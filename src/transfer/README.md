# Transfer Domain

Owns save movement, OpenHome identity operations, and transfer application orchestration.

Submodules:
- `application/`: use-case orchestration (`TransferService` implementations).
- `domain/`: transfer-specific domain rules.
- `adapters/`: bridge and external tool adapters.
- `persistence/`: transfer data stores.
- `interfaces/`: gateway interfaces inside transfer.

Gameplay systems should depend on `include/transfer/contracts/*` only.
