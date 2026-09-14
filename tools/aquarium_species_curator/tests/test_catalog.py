from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from tools.aquarium_species_curator.catalog import (
    Candidate, CatalogStore, MOVEMENT_PROFILE_DEFAULTS, default_entry,
)


class CatalogStoreTests(unittest.TestCase):
    def test_bottom_movement_profiles_rest_in_idle(self):
        for name in ("bottom-crawler", "bottom-swimmer", "surface-walker"):
            profile = MOVEMENT_PROFILE_DEFAULTS[name]
            self.assertEqual(profile["locomotionMode"], "move-rest")
            self.assertEqual(profile["idleAnimation"], "slot4_00")

        benthic = MOVEMENT_PROFILE_DEFAULTS["benthic-rest-swimmer"]
        self.assertEqual(benthic["locomotionMode"], "bottom-rest-swim")
        self.assertTrue(benthic["restAtBottom"])
        self.assertGreater(benthic["roamingHeightMeters"], 0.0)

    def test_round_trip_preserves_reviewed_entry_and_revision(self):
        candidate = Candidate("0087:00", 87, "00", "Dewgong", "dewgong", "model.glbz", ("water-type-gen1-7",))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "catalog.json"
            store = CatalogStore(path)
            entry = default_entry(candidate)
            entry["review"]["status"] = "approved"
            self.assertTrue(store.upsert(entry))
            self.assertFalse(store.upsert(entry))
            loaded = CatalogStore(path)
            loaded.load()
            self.assertEqual(loaded.entry_for(candidate), entry)
            self.assertEqual(json.loads(path.read_text())["catalogRevision"], 1)

    def test_seed_merges_new_profile_without_overwriting_existing_tuning(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "catalog.json"
            path.write_text(json.dumps({
                "schema": "pokemon-resort-aquarium-species",
                "schemaVersion": 1,
                "catalogRevision": 4,
                "movementProfiles": {"bottom-crawler": {"custom": True}},
                "entries": [],
            }))
            store = CatalogStore(path)
            store.load()
            self.assertTrue(store.seed_initial_approvals([]))
            self.assertEqual(store.document["movementProfiles"]["bottom-crawler"], {"custom": True})
            self.assertIn("surface-walker", store.document["movementProfiles"])


if __name__ == "__main__":
    unittest.main()
