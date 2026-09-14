from __future__ import annotations

from pathlib import Path
import unittest

from tools.aquarium_species_curator.catalog import build_candidates, default_entry
from tools.aquarium_species_curator.glb_asset import animation_names, prepare_preview_asset
from tools.aquarium_species_curator.seeds import INITIAL_APPROVALS


class ProjectCatalogueContractTests(unittest.TestCase):
    def test_seeded_approvals_have_models_and_real_animations(self):
        project_root = Path(__file__).resolve().parents[3]
        candidates = {candidate.id: candidate for candidate in build_candidates(project_root)}
        self.assertTrue(INITIAL_APPROVALS.keys() <= candidates.keys())
        for candidate_id in INITIAL_APPROVALS:
            candidate = candidates[candidate_id]
            self.assertTrue(candidate.has_model, candidate_id)
            preview = prepare_preview_asset(
                project_root / candidate.model_path,
                project_root / "build/aquarium_species_curator_test_cache",
            )
            selected = default_entry(candidate)["presentation"]["animation"]
            available = animation_names(preview)
            self.assertIn(selected, available, candidate_id)
            idle = default_entry(candidate)["behavior"].get("idleAnimation")
            if idle:
                self.assertIn(idle, available, candidate_id)

    def test_requested_future_species_remain_visible_without_models(self):
        project_root = Path(__file__).resolve().parents[3]
        candidates = {candidate.id: candidate for candidate in build_candidates(project_root)}
        self.assertIn("0871:00", candidates)  # Pincurchin
        self.assertFalse(candidates["0871:00"].has_model)
        self.assertIn("0904:00", candidates)  # Overqwil

    def test_shellder_is_seeded_as_backward_bottom_swimmer(self):
        project_root = Path(__file__).resolve().parents[3]
        candidate = {item.id: item for item in build_candidates(project_root)}["0090:00"]
        entry = default_entry(candidate)
        self.assertEqual(entry["review"]["status"], "approved")
        self.assertEqual(entry["presentation"]["animation"], "slot6_02")
        self.assertEqual(entry["behavior"]["movementProfile"], "bottom-swimmer")
        self.assertEqual(entry["behavior"]["travelDirection"], "backward")

    def test_cloyster_uses_its_locomotion_idle_as_rest(self):
        project_root = Path(__file__).resolve().parents[3]
        candidate = {item.id: item for item in build_candidates(project_root)}["0091:00"]
        entry = default_entry(candidate)
        self.assertEqual(entry["review"]["status"], "approved")
        self.assertEqual(entry["presentation"]["animation"], "slot6_02")
        self.assertEqual(entry["behavior"]["idleAnimation"], "slot6_00")


if __name__ == "__main__":
    unittest.main()
