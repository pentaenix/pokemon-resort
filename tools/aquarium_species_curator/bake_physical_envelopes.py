"""Merge native, animation-sampled physical envelopes into the catalogue."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baker", type=Path, required=True)
    parser.add_argument("--catalog", type=Path)
    args = parser.parse_args()
    project_root = Path(__file__).resolve().parents[2]
    catalog_path = (args.catalog or project_root / "config/gameplay/world3d/aquarium_species.json").resolve()
    completed = subprocess.run(
        [str(args.baker.resolve()), str(project_root), str(catalog_path)],
        check=True,
        capture_output=True,
        text=True,
    )
    baked = json.loads(completed.stdout)
    by_id = {str(item["id"]): item for item in baked["envelopes"]}
    document = json.loads(catalog_path.read_text(encoding="utf-8"))
    changed = 0
    for entry in document["entries"]:
        if entry.get("review", {}).get("status") != "approved":
            continue
        envelope = by_id.get(str(entry["id"]))
        if envelope is None:
            raise RuntimeError(f"Native baker omitted approved entry {entry['id']}")
        envelope = {key: value for key, value in envelope.items() if key != "id"}
        if entry.get("physicalEnvelope") != envelope:
            entry["physicalEnvelope"] = envelope
            changed += 1
    if not changed:
        print("Aquarium physical envelopes already current")
        return 0
    document["physicalEnvelopeVersion"] = 1
    document["catalogRevision"] = int(document.get("catalogRevision", 0)) + 1
    payload = json.dumps(document, indent=2, ensure_ascii=False) + "\n"
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=catalog_path.name + ".", suffix=".tmp", dir=catalog_path.parent
    )
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            output.write(payload)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary_name, catalog_path)
    finally:
        if os.path.exists(temporary_name):
            os.unlink(temporary_name)
    print(f"Baked {changed} approved physical envelopes at revision {document['catalogRevision']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
