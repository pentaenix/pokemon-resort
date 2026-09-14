"""Bulk-create reviewable proposals without auto-approving any species."""

from __future__ import annotations

import argparse
from pathlib import Path

from .catalog import CatalogStore, build_candidates
from .glb_asset import animation_names, prepare_preview_asset
from .proposals import proposal_entry


def main() -> int:
    parser = argparse.ArgumentParser(description="Propose profiles for unreviewed aquarium species")
    project_root = Path(__file__).resolve().parents[2]
    parser.add_argument("--project-root", type=Path, default=project_root)
    parser.add_argument("--catalog", type=Path)
    args = parser.parse_args()
    root = args.project_root.resolve()
    catalog_path = (args.catalog or root / "config/gameplay/world3d/aquarium_species.json").resolve()
    store = CatalogStore(catalog_path)
    store.load()
    cache = root / "build/aquarium_species_curator_cache"
    proposals = []
    for candidate in build_candidates(root):
        if not candidate.has_model or store.status_for(candidate) != "unreviewed":
            continue
        preview = prepare_preview_asset(root / candidate.model_path, cache)
        proposals.append(proposal_entry(candidate, animation_names(preview)))
    changed = store.upsert_many(proposals)
    print(f"Aquarium curator: wrote {changed} proposals at catalogue revision {store.document['catalogRevision']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

