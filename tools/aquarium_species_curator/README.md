# Aquarium Species Curator

This developer-only Python app reviews Pokémon models for player-built aquariums. It uses RAE's existing Three.js/PySide viewport, so its model materials and skeletal animation list come from the same exported GLB data used by Pokémon Resort. It is not linked, packaged, or invoked by the normal game build.

Run it from the Pokémon Resort repository:

```sh
tools/aquarium_species_curator/run
```

To seed or verify the catalogue without opening a window:

```sh
tools/aquarium_species_curator/run --initialize-only
```

After changing an approved model, animation, scale, or orientation, rebuild the
animation-sampled physical envelopes:

```sh
cmake --build build --target aquarium_species_envelope_dump
python3 tools/aquarium_species_curator/bake_physical_envelopes.py \
  --baker build/aquarium_species_envelope_dump
```

The native baker uses the same Attend loader, form visibility, skeletal poses,
and material visibility as the game. It samples the selected movement and rest
loops and applies the curated orientation. This is an offline developer step;
the normal game build and map-loading path never invoke it.

Generate conservative, reviewable profiles for every remaining candidate that
has a model:

```sh
tools/aquarium_species_curator/run --propose-remaining
```

This writes `proposed`, never `approved`, and preserves existing approved or
rejected reviews. Proposed entries appear in the normal Review queue and in the
dedicated Proposed filter. Missing-model candidates remain unreviewed.

The launcher uses `../rae/.venv` and `../rae` by default. Override them with `PKR_AQUARIUM_CURATOR_PYTHON` and `PKR_RAE_DIR` when needed. Compiled `.glbz` models are verified and expanded into the disposable `build/aquarium_species_curator_cache/` preview cache.

## Review queue

The initial queue contains every base Gen 1–7 Water-type species with an available Attend model, plus explicitly requested ecological exceptions. Typing only proposes candidates. It never approves a Pokémon for the game. Requested Gen 8 species remain in the queue as missing-model entries until compatible assets exist.

Candidates without models remain `unreviewed` under the **Missing model** filter
and are excluded from the normal Review queue until their assets arrive.

For each form, the curator records:

- approval, rejection, or unreviewed status;
- selected model animation;
- model pitch, yaw, scale multiplier, and waterline offset (pitch/yaw update the live preview);
- preferred vertical zone and surface/waterline behavior;
- movement profile;
- forward or backward locomotion direction;
- per-species rest animation and idle-only pitch override;
- freshwater, saltwater, and brackish compatibility;
- minimum and preferred group sizes;
- a paintable capacity-cell mask;
- curator notes or a rejection reason.

The **School**, **Bottom walker**, and **Stationary** presets fill the common
movement, vertical-zone, animation, and group fields in one selection. They do
not overwrite the species-specific capacity mask, orientation, or water kinds.
For example, Squirtle can start from Bottom walker, which selects `slot6_02`,
bottom placement, and the bottom-crawler movement profile.

`bottom-crawler` and `bottom-swimmer` are intermittent profiles: they use
catalogue-authored movement and rest windows instead of walking continuously.
Shellder and Cloyster are backward-moving bottom swimmers with `slot6_02` travel
and `slot6_00` rest; both override the shared timing with long stationary pauses
and brief relocation bursts. Pyukumuku similarly rests often but moves more
frequently than either shellfish.

`benthic-rest-swimmer` alternates a lower-water swimming excursion with a
navigation-validated return to the floor and a curated idle pose. Whiscash,
Huntail, Gorebyss, Relicanth, and Clawitzer currently use this profile. Its
timings, roaming height, and soft crowd-body scale live under
`movementProfiles`; individual entries may override them with
`behavior.activity`.

Swimming profiles prefer `slot6_00` as their rest pose whenever that clip is
available. Milotic moves with `slot6_02` and uses `slot6_00` at rest with an
idle-only 180-degree pitch correction for that clip's inverted root pose.

`surface-walker` constrains a Pokémon's feet to the water plane while keeping
its body above the surface. It uses the same move/rest rhythm; Suicune is the
first special case authored with this profile.

`timid-reef` is a mostly stationary bottom profile with randomized valid spawn
positions and short local movements separated by long rests. Per-species
`threatSpecies`, flee radius, distance, and speed fields allow ecological
avoidance without hard-coding predator names in the simulation. Corsola uses
this profile and flees nearby Mareanie or Toxapex.

`jelly-drift` keeps jellyfish near a small local anchor. It moves very slowly
sideways while alternating gentle rises and descents, without pitching the
model as though it were an ordinary forward-swimming fish. Tentacool,
Tentacruel, Frillish, and Jellicent use this profile.

Approve and Reject save immediately. Save / Skip retains an unreviewed draft. Closing or navigating also saves changed fields. Writes use a temporary file plus atomic promotion.

## Output contract

The authoritative output is:

```text
config/gameplay/world3d/aquarium_species.json
```

It uses schema `pokemon-resort-aquarium-species`, version `1`. The game-side
catalogue loader exposes only entries whose `review.status` is `approved` and
provides their presentation, vertical-zone, movement, social, habitat, and
capacity data to the player-selected `AquariumPopulationPolicy`. Movement
profiles also supply move/rest windows, floor-return behavior, roaming height,
and a crowd-body scale. The curator
remains responsible for verifying each selected model and animation before
approval.

The capacity mask is an abstract stocking budget, not a spawn position. The
top-level `capacityClearance` policy expands approved masks before runtime
packing. One-cell masks receive the normal clearance; masks at or above
`largeMaskMinimumCells` receive the larger comfort clearance.
`unexpandedSpecies` keeps deliberately tiny species and already-maximum
footprints at their authored size. Runtime
navigation derives actual swimming positions from the tank navigation volume.
Model availability, navigation clearance, usable water volume, depth, and floor
area remain separate commit-time validation gates. `physicalEnvelope` is
derived, versioned data. A stale source signature disables new stocking for
that entry until it is rebaked instead of trusting obsolete dimensions.
