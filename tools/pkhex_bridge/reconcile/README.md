# Bridge Reconcile Components

`BridgeProjectReconcile` is a partial class split by legality concern. The public call flow still starts in `BridgeProject.cs`; these files only organize the final repair and overlay work that happens after PKHeX/OpenHome projection creates the target PKM.

## Where to Look

- `MoveReconciliation.cs`: move catalog names, placeholder move filling, PP repair, and hot-overlay move writes. Start here for invalid move mapping or empty moveset behavior.
- `ReviewOverlays.cs`: pre-save review JSON and hot mutable overlay JSON from Resort. Start here when Resort metadata is not being copied into the projected PKM.
- `TransferNormalization.cs`: cross-generation transfer fields such as language, EV caps, met date/location/level, synthetic SID16 for GB-origin Gen 3+ projections, Gen 5 Poke Transfer data, Gen 6 Bank-style data, geolocation, memory clearing, and Gen 6 FR/LG-era ability selection for GB-origin transfers. Start here for Gen 1/2 -> Gen 5/6 transfer legality issues that are not moves or PID correlation.
- `IdentityFinalization.cs`: PID, nature, gender, shiny, ability slot, target-generation identity policy, Method 1/Method H PID/IV repair, and final identity validation. Start here for `PID+ correlation`, PID/nature/gender mismatch, shiny mismatch, or Method 1 repair issues.
- `IdentityEncounterHelpers.cs`: helper code for encounter reflection, generated PID/IV copying, and legality-report PID correlation checks. Keep it policy-free; the policy decision belongs in `IdentityFinalization.cs`.
- `NicknameFinalization.cs`: final nickname state and Gen 1/2 species-default nickname coercion. Start here for `IsNicknamed`, evolution-name behavior, or Gen 1/2 default-name casing.
- `RibbonReconciliation.cs`: canonical ribbon catalog replay after projection. Start here for missing or downgraded ribbons.
- `CommonHelpers.cs`: shared reflection helpers, trainer-name normalization, ball/language helpers, and simple JSON readers. Keep domain-specific legality rules out of this file unless several components truly share them.

## Transfer Debugging Map

- Gen 1/2 -> Gen 3 legality: check `src/resort/services/MirrorProjectionService.cpp` for the Gen 3 origin-game mapper, `TransferNormalization.cs` for EV/EXP/language/met normalization, then `IdentityFinalization.cs` for `Gen3Encounter` PID/IV correlation.
- Gen 1/2 -> Gen 4 legality: check `src/resort/services/MirrorProjectionService.cpp` for Pal Park met/origin fields, `TransferNormalization.cs` for EV/EXP/language/met normalization, then `IdentityFinalization.cs` for `Gen4PalPark` PID/IV correlation.
- Gen 1/2 -> Gen 5 legality: check `TransferNormalization.cs` for Poke Transfer met fields and EV caps, then `IdentityFinalization.cs` for PID-derived fields.
- Gen 1/2 -> Gen 6 legality: check `TransferNormalization.cs` for Bank-style met fields, geolocation/memory, PID==EC, and Gen 3-era ability IDs; check `IdentityFinalization.cs` for remaining PID correlation.
- Nickname or OT regressions in Gen 1/2 saves: check `NicknameFinalization.cs` for bridge projection and OpenHome save writers for raw GB box/party storage. Gen 1/2 carts do not store a nickname bit.

## Regression Rules

The identity layer is intentionally policy-driven. Do not add broad `pk.Format` fixes when a bug belongs to a specific transfer story. Add or adjust an `IdentityProjectionPolicy` case, then keep the repair behind that policy.

Gen 3 and Gen 4 have separate origin/met stories. A Gen 3 target must use a Gen 3-compatible origin such as FireRed/LeafGreen; a Gen 4 target from older games should use Pal Park fields. Do not reuse the Gen 4 origin mapper for Gen 3 targets.

Gen 3 PID repair must not copy generated moves over source moves. Legacy moves from Gen 1/2 can be intentionally invalid in Gen 3; keep those moves and repair only identity/encounter fields that do not erase them. If PKHeX chooses an egg/gift path solely because of an intentionally preserved legacy move, treat the move legality separately from the identity policy.

Do not run exhaustive PID/IV brute-force searches for Gen 3 preserved-move conflicts. The generated encounter and Method H slot repairs are the bounded Gen 3 path; if those cannot satisfy PKHeX while moves are preserved, accept the remaining legality report rather than stalling transfers.

Before changing identity repair, run a focused test set that covers the affected generation and the known-good neighbors. For Gen 1/2 upward transfers, include Gen 3 encounter repair, Gen 4 Pal Park repair, Gen 5 Poke Transfer fields, and Gen 6 Bank-style fields in the same run so a local edge-case fix cannot silently break another generation.

Keep each `.cs` component under 500 lines. Documentation can exceed that limit when needed.
