from __future__ import annotations

import unittest

from tools.aquarium_species_curator.presets import preset_values


class PresetTests(unittest.TestCase):
    def test_bottom_walker_selects_walk_animation_and_bottom_profile(self):
        values = preset_values("Bottom walker", ["slot4_00", "slot6_02"])
        self.assertEqual(values["animation"], "slot6_02")
        self.assertEqual(values["movement"], "bottom-crawler")
        self.assertEqual(values["vertical"], "bottom")
        self.assertEqual(values["direction"], "forward")

    def test_preset_falls_back_to_first_available_animation(self):
        values = preset_values("School", ["unusual_swim"])
        self.assertEqual(values["animation"], "unusual_swim")


if __name__ == "__main__":
    unittest.main()
