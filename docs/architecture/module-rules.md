# Module Rules

## Hard Rules
1. `engine` cannot import `gameplay`, `transfer`, or `resort` modules.
2. `gameplay` cannot import `resort/*` or `transfer` internals.
3. `gameplay` may only consume `transfer/contracts/*` from transfer.
4. `transfer/contracts/*` cannot import `gameplay/*` or `ui/*`.

## Enforcement
- Script: `scripts/check_module_boundaries.sh`
- CTest target: `architecture_module_rules_check`

## Change Management
Cross-domain boundary changes require an ADR in `docs/architecture/adrs/` and updates to `docs/architecture/system-map.md`.
