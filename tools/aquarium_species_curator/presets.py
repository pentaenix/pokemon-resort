"""Small behavior presets for fast aquarium species review."""

from __future__ import annotations

PRESETS = {
    "School": {
        "movement": "schooling",
        "vertical": "open-water",
        "surface": "submerged",
        "direction": "forward",
        "animations": ("slot6_02", "slot4_00"),
        "idleAnimation": "slot4_00",
        "minimumGroup": 3,
        "preferredGroup": 6,
    },
    "Bottom walker": {
        "movement": "bottom-crawler",
        "vertical": "bottom",
        "surface": "submerged",
        "direction": "forward",
        "animations": ("slot6_02", "slot4_00"),
        "idleAnimation": "slot4_00",
        "minimumGroup": 1,
        "preferredGroup": 1,
    },
    "Stationary": {
        "movement": "bottom-stationary",
        "vertical": "bottom",
        "surface": "submerged",
        "direction": "forward",
        "animations": ("slot4_00", "slot6_00"),
        "idleAnimation": "slot4_00",
        "minimumGroup": 1,
        "preferredGroup": 1,
    },
}


def preset_values(name: str, available_animations: list[str]) -> dict | None:
    preset = PRESETS.get(name)
    if not preset:
        return None
    values = dict(preset)
    values["animation"] = next(
        (candidate for candidate in preset["animations"] if candidate in available_animations),
        available_animations[0] if available_animations else "",
    )
    return values
