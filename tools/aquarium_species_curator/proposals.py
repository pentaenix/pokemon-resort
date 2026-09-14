"""Conservative first-pass aquarium profiles awaiting human approval."""

from __future__ import annotations

from copy import deepcopy

from .catalog import Candidate, default_entry

BOTTOM_CRAWLERS = {
    98, 99, 138, 139, 140, 141, 158, 159, 160, 183, 184, 186, 194,
    195, 199, 224, 258, 259, 260, 341, 342, 348, 393,
    394, 395, 400, 422, 423, 501, 502, 503, 515, 516, 536, 537, 564,
    565, 647, 656, 657, 658, 721, 747, 751, 752, 767, 768,
}
BOTTOM_SWIMMERS = {347}
BENTHIC_REST_SWIMMERS = {340, 367, 368, 369, 693}
BOTTOM_STATIONARY = {222, 748}
BOTTOM_BURROWERS = {618}
ANCHORED = {345, 346, 688, 689, 781}
SURFACE_FLOATERS = {270, 271, 272, 278, 279, 580, 581}
SURFACE_WALKERS = {245, 283}
HOVERERS = {116, 117, 121, 211, 686, 687, 690, 691}
JELLY_DRIFTERS = {72, 73, 592, 593}
SCHOOLERS = {118, 119, 129, 170, 171, 318, 339, 349, 370, 456, 457, 535, 550, 602, 779}
LARGE_CRUISERS = {130, 131, 148, 226, 249, 321, 350, 365, 484, 604, 721, 730, 788}
SWIMMING_PROFILES = {
    "free-swimmer", "schooling", "escort", "hover", "jelly-drift", "surface-floater",
    "large-cruiser", "bottom-swimmer", "benthic-rest-swimmer",
}

SIDEWAYS_TRAVEL = {98, 99}
BACKWARD_TRAVEL = {341, 342, 692, 693}
FLAP_SWIMMERS = {249, 278, 279, 580, 581}
IDLE_SWIMMERS = {131, 320, 321, 347, 363, 364, 365}
FRESHWATER = {
    118, 119, 129, 130, 134, 147, 148, 158, 159, 160, 183, 184, 186,
    194, 195, 199, 245, 258, 259, 260, 270, 271, 272, 283, 339, 340,
    349, 350, 393, 394, 395, 400, 418, 419, 501, 502, 503, 515, 516,
    535, 536, 537, 550, 564, 565, 580, 581, 602, 603, 604, 647, 656,
    657, 658, 751, 752,
}
BRACKISH_TOO = {134, 194, 195, 270, 271, 272, 339, 340, 418, 419, 422, 423, 550, 618}
MEDIUM = {
    99, 117, 119, 121, 134, 139, 141, 160, 171, 184, 195, 199, 224,
    230, 245, 249, 259, 271, 272, 279, 319, 320, 340, 342, 346, 348,
    350, 364, 367, 368, 369, 394, 400, 419, 423, 457, 458, 490, 502,
    516, 537, 565, 581, 593, 604, 647, 657, 687, 689, 691, 693, 721,
    729, 748, 752, 768, 781, 788,
}
LARGE_MASKS = {
    130: ["111"], 131: ["111", "111"], 148: ["111"],
    226: ["111", "111"], 249: ["111", "111"],
    321: ["11111", "11111"], 350: ["111"], 365: ["111", "111"],
    484: ["111", "111"], 604: ["111"], 721: ["111", "111"],
    730: ["111", "111"],
}


def _first_animation(available: list[str], preferred: tuple[str, ...]) -> str:
    return next((name for name in preferred if name in available), available[0] if available else "")


def _profile(dex: int) -> tuple[str, str, str]:
    if dex in SURFACE_WALKERS:
        return "surface-walker", "surface", "stands-on-surface"
    if dex in BOTTOM_BURROWERS:
        return "bottom-burrower", "bottom", "submerged"
    if dex in ANCHORED:
        return "anchored", "bottom", "submerged"
    if dex in BOTTOM_STATIONARY:
        return "bottom-stationary", "bottom", "submerged"
    if dex in BOTTOM_SWIMMERS:
        return "bottom-swimmer", "bottom", "submerged"
    if dex in BENTHIC_REST_SWIMMERS:
        return "benthic-rest-swimmer", "lower-water", "submerged"
    if dex in BOTTOM_CRAWLERS:
        return "bottom-crawler", "bottom", "submerged"
    if dex in SURFACE_FLOATERS:
        surface = "top-protrudes" if dex in {270, 271, 272, 278, 279, 580, 581} else "touches-surface"
        return "surface-floater", "surface", surface
    if dex in JELLY_DRIFTERS:
        return "jelly-drift", "open-water", "submerged"
    if dex in HOVERERS:
        return "hover", "open-water", "submerged"
    if dex in SCHOOLERS:
        return "schooling", "open-water", "submerged"
    if dex in LARGE_CRUISERS:
        return "large-cruiser", "open-water", "submerged"
    return "free-swimmer", "open-water", "submerged"


def proposal_entry(candidate: Candidate, available_animations: list[str]) -> dict:
    entry = deepcopy(default_entry(candidate))
    movement, vertical, surface = _profile(candidate.dex)
    preferred = ("slot6_02", "slot5_00", "slot4_00")
    if candidate.dex == 129:  # Magikarp's authored swimming pose.
        preferred = ("slot5_00", "slot6_02", "slot4_00")
    elif candidate.dex in FLAP_SWIMMERS:
        preferred = ("slot6_01", "slot6_02", "slot4_00")
    elif candidate.dex in IDLE_SWIMMERS or movement in {"anchored", "bottom-stationary", "bottom-burrower"}:
        preferred = ("slot4_00", "slot5_00", "slot6_02")

    direction = "forward"
    if candidate.dex in SIDEWAYS_TRAVEL:
        direction = "sideways"
    elif candidate.dex in BACKWARD_TRAVEL:
        direction = "backward"
    minimum_group = 3 if movement == "schooling" else 1
    preferred_group = 6 if movement == "schooling" else 1
    idle_preference = ("slot6_00", "slot4_00") if movement in SWIMMING_PROFILES else ("slot4_00", "slot6_00")
    water_kinds = ["freshwater"] if candidate.dex in FRESHWATER else ["saltwater"]
    if candidate.dex in BRACKISH_TOO:
        water_kinds.append("brackish")

    entry["review"] = {
        "status": "proposed",
        "rejectionReason": "",
        "notes": "Automated first-pass habitat proposal; requires curator approval.",
    }
    entry["presentation"]["animation"] = _first_animation(available_animations, preferred)
    entry["habitat"] = {
        "verticalZone": vertical,
        "surfaceBehavior": surface,
        "waterKinds": water_kinds,
    }
    entry["behavior"] = {
        "movementProfile": movement,
        "travelDirection": direction,
        "idleAnimation": _first_animation(available_animations, idle_preference),
        "idlePitchDegrees": 180.0 if candidate.dex == 350 else 0.0,
        "minimumGroup": minimum_group,
        "preferredGroup": preferred_group,
    }
    entry["capacity"] = {"mask": LARGE_MASKS.get(candidate.dex, ["11"] if candidate.dex in MEDIUM else ["1"])}
    return entry
