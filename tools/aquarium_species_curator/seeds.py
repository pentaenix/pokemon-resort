"""Stable candidate and first-pass approval seeds for the aquarium curator."""

from __future__ import annotations

# National Dex species whose base Gen 1-7 form has the Water type. Typing is
# only a way to build the review queue; it never grants runtime eligibility.
WATER_TYPE_DEX = frozenset(
    {
        7, 8, 9, 54, 55, 60, 61, 62, 72, 73, 79, 80, 86, 87, 90, 91,
        98, 99, 116, 117, 118, 119, 120, 121, 129, 130, 131, 134, 138,
        139, 140, 141, 158, 159, 160, 170, 171, 183, 184, 186, 194,
        195, 199, 211, 222, 223, 224, 226, 230, 245, 258, 259, 260,
        270, 271, 272, 278, 279, 283, 318, 319, 320, 321, 339, 340,
        341, 342, 349, 350, 363, 364, 365, 366, 367, 368, 369, 370,
        382, 393, 394, 395, 400, 418, 419, 422, 423, 456, 457, 458,
        484, 489, 490, 501, 502, 503, 515, 516, 535, 536, 537, 550,
        564, 565, 580, 581, 592, 593, 594, 647, 656, 657, 658, 688,
        689, 690, 692, 693, 721, 728, 729, 730, 746, 747, 748, 751,
        752, 767, 768, 771, 779, 788,
    }
)

# Ecological/art-direction exceptions explicitly requested for review. Entries
# remain visible even when their 3D model is not in the current Gen 1-7 pack.
EXTRA_CANDIDATES = (
    (147, "00", "Dratini"),
    (148, "00", "Dragonair"),
    (249, "00", "Lugia"),
    (270, "00", "Lotad"),
    (345, "00", "Lileep"),
    (346, "00", "Cradily"),
    (347, "00", "Anorith"),
    (348, "00", "Armaldo"),
    (602, "00", "Tynamo"),
    (603, "00", "Eelektrik"),
    (604, "00", "Eelektross"),
    (618, "00", "Stunfisk"),
    (686, "00", "Inkay"),
    (687, "00", "Malamar"),
    (691, "00", "Dragalge"),
    (781, "00", "Dhelmise"),
    (852, "00", "Clobbopus"),
    (853, "00", "Grapploct"),
    (864, "00", "Cursola"),
    (871, "00", "Pincurchin"),
    (904, "00", "Overqwil"),
    (211, "hisui", "Hisuian Qwilfish"),
    (222, "galar", "Galarian Corsola"),
)


INITIAL_APPROVALS = {
    "0087:00": {
        "animation": "slot4_00", "movement": "free-swimmer",
        "vertical": "open-water", "mask": ["11"], "preferredGroup": 2,
    },
    "0090:00": {
        "animation": "slot6_02", "movement": "bottom-swimmer",
        "vertical": "bottom", "direction": "backward", "mask": ["1"],
    },
    "0091:00": {
        "animation": "slot6_02", "idleAnimation": "slot6_00",
        "movement": "bottom-swimmer", "vertical": "bottom",
        "direction": "backward", "mask": ["11"],
    },
    "0120:00": {
        "animation": "slot4_00", "movement": "bottom-stationary",
        "vertical": "bottom", "mask": ["1"], "pitchDegrees": -90.0,
    },
    "0223:00": {
        "animation": "slot6_02", "movement": "schooling",
        "vertical": "open-water", "mask": ["1"], "minimumGroup": 2,
        "preferredGroup": 4,
    },
    "0366:00": {
        "animation": "slot4_00", "movement": "bottom-stationary",
        "vertical": "bottom", "mask": ["1"],
    },
    "0382:00": {
        "animation": "slot4_00", "movement": "large-cruiser",
        "vertical": "open-water", "mask": ["111", "111"],
    },
    "0746:00": {
        "animation": "slot6_02", "movement": "schooling",
        "vertical": "open-water", "mask": ["1"], "minimumGroup": 3,
        "preferredGroup": 6,
    },
    "0771:00": {
        "animation": "slot4_00", "movement": "bottom-crawler",
        "vertical": "bottom", "mask": ["1"],
    },
}
