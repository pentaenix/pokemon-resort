# Bridge Reconcile Components

`BridgeProjectReconcile` is a partial class split by legality concern. The public call flow still starts in `BridgeProject.cs`; these files only organize the final repair and overlay work that happens after PKHeX/OpenHome projection creates the target PKM.

## Where to Look

- `MoveReconciliation.cs`: move catalog names, placeholder move filling, PP repair, and hot-overlay move writes. Start here for invalid move mapping or empty moveset behavior.
- `ReviewOverlays.cs`: pre-save review JSON and hot mutable overlay JSON from Resort. Start here when Resort metadata is not being copied into the projected PKM.
- `TransferNormalization.cs`: cross-generation transfer fields such as language, EV caps, met date/location/level, Gen 5 Poke Transfer data, Gen 6 Bank-style data, geolocation, memory clearing, and Gen 6 FR/LG-era ability selection for GB-origin transfers. Start here for Gen 1/2 -> Gen 5/6 transfer legality issues that are not moves or PID correlation.
- `IdentityFinalization.cs`: PID, nature, gender, shiny, ability slot, Method 1 PID/IV repair, and final identity validation. Start here for `PID+ correlation`, PID/nature/gender mismatch, shiny mismatch, or Method 1 repair issues.
- `NicknameFinalization.cs`: final nickname state and Gen 1/2 species-default nickname coercion. Start here for `IsNicknamed`, evolution-name behavior, or Gen 1/2 default-name casing.
- `RibbonReconciliation.cs`: canonical ribbon catalog replay after projection. Start here for missing or downgraded ribbons.
- `CommonHelpers.cs`: shared reflection helpers, trainer-name normalization, ball/language helpers, and simple JSON readers. Keep domain-specific legality rules out of this file unless several components truly share them.

## Transfer Debugging Map

- Gen 1/2 -> Gen 3/4 legality: check `TransferNormalization.cs` for EV/EXP normalization, then `IdentityFinalization.cs` for PID/IV correlation.
- Gen 1/2 -> Gen 5 legality: check `TransferNormalization.cs` for Poke Transfer met fields and EV caps, then `IdentityFinalization.cs` for PID-derived fields.
- Gen 1/2 -> Gen 6 legality: check `TransferNormalization.cs` for Bank-style met fields, geolocation/memory, PID==EC, and Gen 3-era ability IDs; check `IdentityFinalization.cs` for remaining PID correlation.
- Nickname or OT regressions in Gen 1/2 saves: check `NicknameFinalization.cs` for bridge projection and OpenHome save writers for raw GB box/party storage. Gen 1/2 carts do not store a nickname bit.

Keep each `.cs` component under 500 lines. Documentation can exceed that limit when needed.
