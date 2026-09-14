from __future__ import annotations

import unittest

from tools.aquarium_species_curator.catalog import Candidate
from tools.aquarium_species_curator.proposals import proposal_entry


def candidate(dex_number: int, name: str) -> Candidate:
    return Candidate(f"{dex_number:04d}:00", dex_number, "00", name, name.casefold(), "model.glbz", ("water-type-gen1-7",))


class ProposalTests(unittest.TestCase):
    def test_krabby_walks_sideways_on_bottom(self):
        entry = proposal_entry(candidate(98, "Krabby"), ["slot4_00", "slot6_02"])
        self.assertEqual(entry["review"]["status"], "proposed")
        self.assertEqual(entry["presentation"]["animation"], "slot6_02")
        self.assertEqual(entry["behavior"]["movementProfile"], "bottom-crawler")
        self.assertEqual(entry["behavior"]["travelDirection"], "sideways")

    def test_magikarp_prefers_slot_five_swim(self):
        entry = proposal_entry(candidate(129, "Magikarp"), ["slot4_00", "slot5_00", "slot6_02"])
        self.assertEqual(entry["presentation"]["animation"], "slot5_00")
        self.assertEqual(entry["behavior"]["movementProfile"], "schooling")

    def test_swimmers_prefer_slot_six_idle_when_available(self):
        entry = proposal_entry(candidate(118, "Goldeen"), ["slot4_00", "slot6_00", "slot6_02"])
        self.assertEqual(entry["behavior"]["idleAnimation"], "slot6_00")

        walker = proposal_entry(candidate(98, "Krabby"), ["slot4_00", "slot6_00", "slot6_02"])
        self.assertEqual(walker["behavior"]["idleAnimation"], "slot4_00")

    def test_milotic_has_an_idle_only_orientation_correction(self):
        entry = proposal_entry(candidate(350, "Milotic"), ["slot4_00", "slot6_00", "slot6_02"])
        self.assertEqual(entry["presentation"]["animation"], "slot6_02")
        self.assertEqual(entry["behavior"]["idleAnimation"], "slot6_00")
        self.assertEqual(entry["behavior"]["idlePitchDegrees"], 180.0)

    def test_lotad_protrudes_from_surface(self):
        entry = proposal_entry(candidate(270, "Lotad"), ["slot4_00", "slot6_02"])
        self.assertEqual(entry["habitat"]["verticalZone"], "surface")
        self.assertEqual(entry["habitat"]["surfaceBehavior"], "top-protrudes")

    def test_lombre_and_ludicolo_share_lotad_surface_behavior(self):
        for dex_number, name in ((271, "Lombre"), (272, "Ludicolo")):
            entry = proposal_entry(candidate(dex_number, name), ["slot4_00", "slot4_04"])
            self.assertEqual(entry["behavior"]["movementProfile"], "surface-floater")
            self.assertEqual(entry["habitat"]["verticalZone"], "surface")
            self.assertEqual(entry["habitat"]["surfaceBehavior"], "top-protrudes")

    def test_surskit_walks_on_the_surface(self):
        entry = proposal_entry(candidate(283, "Surskit"), ["slot4_00", "slot6_02"])
        self.assertEqual(entry["behavior"]["movementProfile"], "surface-walker")
        self.assertEqual(entry["habitat"]["surfaceBehavior"], "stands-on-surface")

    def test_anorith_swims_near_the_bottom_while_armaldo_walks(self):
        anorith = proposal_entry(candidate(347, "Anorith"), ["slot4_00", "slot6_02"])
        self.assertEqual(anorith["presentation"]["animation"], "slot4_00")
        self.assertEqual(anorith["behavior"]["movementProfile"], "bottom-swimmer")
        self.assertEqual(anorith["habitat"]["verticalZone"], "bottom")

        armaldo = proposal_entry(candidate(348, "Armaldo"), ["slot4_00", "slot6_02"])
        self.assertEqual(armaldo["presentation"]["animation"], "slot6_02")
        self.assertEqual(armaldo["behavior"]["movementProfile"], "bottom-crawler")
        self.assertEqual(armaldo["habitat"]["verticalZone"], "bottom")

    def test_stunfisk_is_a_bottom_burrower(self):
        entry = proposal_entry(candidate(618, "Stunfisk"), ["slot4_00", "slot6_02"])
        self.assertEqual(entry["behavior"]["movementProfile"], "bottom-burrower")
        self.assertEqual(entry["presentation"]["animation"], "slot4_00")

    def test_huntail_uses_benthic_rest_excursions(self):
        entry = proposal_entry(candidate(367, "Huntail"), ["slot4_00", "slot6_00", "slot6_02"])
        self.assertEqual(entry["behavior"]["movementProfile"], "benthic-rest-swimmer")
        self.assertEqual(entry["behavior"]["idleAnimation"], "slot6_00")
        self.assertEqual(entry["habitat"]["verticalZone"], "lower-water")

    def test_suicune_walks_above_the_water_surface(self):
        entry = proposal_entry(candidate(245, "Suicune"), ["slot4_00", "slot6_02"])
        self.assertEqual(entry["presentation"]["animation"], "slot6_02")
        self.assertEqual(entry["behavior"]["movementProfile"], "surface-walker")
        self.assertEqual(entry["habitat"]["verticalZone"], "surface")
        self.assertEqual(entry["habitat"]["surfaceBehavior"], "stands-on-surface")


if __name__ == "__main__":
    unittest.main()
