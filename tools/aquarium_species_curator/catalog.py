"""Candidate discovery and transactional aquarium species catalogue storage."""

from __future__ import annotations

from dataclasses import dataclass
import json
import os
from pathlib import Path
import re
import tempfile
from typing import Any

from .seeds import EXTRA_CANDIDATES, INITIAL_APPROVALS, WATER_TYPE_DEX

SCHEMA = "pokemon-resort-aquarium-species"
SCHEMA_VERSION = 1
MOVEMENT_PROFILE_DEFAULTS = {
    "jelly-drift": {
        "locomotionMode": "vertical-drift",
        "horizontalSpeedMetersPerSecond": 0.11,
        "verticalTravelMeters": 0.38,
        "horizontalTravelMeters": 0.20,
        "crowdBodyScale": 0.52,
    },
    "bottom-crawler": {
        "locomotionMode": "move-rest",
        "idleAnimation": "slot4_00",
        "moveSeconds": {"minimum": 2.5, "maximum": 6.0},
        "restSeconds": {"minimum": 3.0, "maximum": 8.0},
        "restAtBottom": True,
        "crowdBodyScale": 0.62,
    },
    "bottom-swimmer": {
        "locomotionMode": "move-rest",
        "idleAnimation": "slot4_00",
        "moveSeconds": {"minimum": 2.5, "maximum": 6.0},
        "restSeconds": {"minimum": 2.0, "maximum": 5.0},
        "roamingHeightMeters": 0.55,
        "restAtBottom": True,
        "crowdBodyScale": 0.60,
    },
    "benthic-rest-swimmer": {
        "locomotionMode": "bottom-rest-swim",
        "idleAnimation": "slot6_00",
        "moveSeconds": {"minimum": 8.0, "maximum": 15.0},
        "restSeconds": {"minimum": 4.0, "maximum": 10.0},
        "roamingHeightMeters": 0.9,
        "restAtBottom": True,
        "crowdBodyScale": 0.64,
    },
    "surface-walker": {
        "locomotionMode": "move-rest",
        "idleAnimation": "slot4_00",
        "moveSeconds": {"minimum": 3.0, "maximum": 7.0},
        "restSeconds": {"minimum": 1.0, "maximum": 3.0},
        "crowdBodyScale": 0.65,
    },
    "timid-reef": {
        "locomotionMode": "timid-local",
        "idleAnimation": "slot4_00",
        "idleSeconds": {"minimum": 8.0, "maximum": 18.0},
        "localMoveDistanceMeters": 0.28,
        "crowdBodyScale": 0.60,
    },
}
MODEL_RE = re.compile(r"^pm(?P<dex>\d{4})_(?P<form>\d{2})_(?P<name>.+)\.(?:glb|glbz)$", re.I)


@dataclass(frozen=True)
class Candidate:
    id: str
    dex: int
    form: str
    display_name: str
    species: str
    model_path: str
    sources: tuple[str, ...]

    @property
    def has_model(self) -> bool:
        return bool(self.model_path)


def _slug(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", value.casefold()).strip("-")


def _display_name(raw: str) -> str:
    return re.sub(r"_+", " ", raw).strip()


def build_candidates(project_root: Path) -> list[Candidate]:
    model_root = project_root / "assets/pokemon_attend/pokemon_models"
    by_id: dict[str, Candidate] = {}
    extra_keys = {(dex, form) for dex, form, _name in EXTRA_CANDIDATES}
    for path in sorted(model_root.glob("pm*")):
        match = MODEL_RE.match(path.name)
        if not match:
            continue
        dex = int(match.group("dex"))
        form = match.group("form")
        is_water = dex in WATER_TYPE_DEX
        is_extra = (dex, form) in extra_keys
        if not is_water and not is_extra:
            continue
        name = _display_name(match.group("name"))
        sources = []
        if is_water:
            sources.append("water-type-gen1-7")
        if is_extra:
            sources.append("curated-exception")
        candidate_id = f"{dex:04d}:{form}"
        by_id[candidate_id] = Candidate(
            id=candidate_id,
            dex=dex,
            form=form,
            display_name=name,
            species=_slug(name),
            model_path=path.relative_to(project_root).as_posix(),
            sources=tuple(sources),
        )

    for dex, form, name in EXTRA_CANDIDATES:
        candidate_id = f"{dex:04d}:{form}"
        if candidate_id in by_id:
            current = by_id[candidate_id]
            if "curated-exception" not in current.sources:
                by_id[candidate_id] = Candidate(
                    **{**current.__dict__, "sources": (*current.sources, "curated-exception")}
                )
            continue
        by_id[candidate_id] = Candidate(
            id=candidate_id,
            dex=dex,
            form=form,
            display_name=name,
            species=_slug(name),
            model_path="",
            sources=("curated-exception",),
        )
    return sorted(by_id.values(), key=lambda item: (item.dex, item.form, item.display_name))


def default_entry(candidate: Candidate) -> dict[str, Any]:
    seed = INITIAL_APPROVALS.get(candidate.id, {})
    approved = bool(seed)
    return {
        "id": candidate.id,
        "dex": candidate.dex,
        "species": candidate.species,
        "displayName": candidate.display_name,
        "form": candidate.form,
        "candidateSources": list(candidate.sources),
        "review": {
            "status": "approved" if approved else "unreviewed",
            "rejectionReason": "",
            "notes": "",
        },
        "model": {"path": candidate.model_path},
        "presentation": {
            "animation": seed.get("animation", "slot4_00"),
            "pitchDegrees": float(seed.get("pitchDegrees", 0.0)),
            "yawDegrees": float(seed.get("yawDegrees", 0.0)),
            "scaleMultiplier": float(seed.get("scaleMultiplier", 1.0)),
            "waterlineOffsetBodyHeights": float(seed.get("waterlineOffsetBodyHeights", 0.0)),
        },
        "habitat": {
            "verticalZone": seed.get("vertical", "open-water"),
            "surfaceBehavior": seed.get("surfaceBehavior", "submerged"),
            "waterKinds": list(seed.get("waterKinds", ["saltwater"] if approved else [])),
        },
        "behavior": {
            "movementProfile": seed.get("movement", "free-swimmer"),
            "travelDirection": seed.get("direction", "forward"),
            "idleAnimation": seed.get("idleAnimation", "slot4_00"),
            "idlePitchDegrees": float(seed.get("idlePitchDegrees", 0.0)),
            "minimumGroup": int(seed.get("minimumGroup", 1)),
            "preferredGroup": int(seed.get("preferredGroup", 1)),
        },
        "capacity": {"mask": list(seed.get("mask", ["1"]))},
    }


class CatalogStore:
    def __init__(self, path: Path):
        self.path = path
        self.document: dict[str, Any] = {
            "schema": SCHEMA,
            "schemaVersion": SCHEMA_VERSION,
            "catalogRevision": 0,
            "movementProfiles": MOVEMENT_PROFILE_DEFAULTS,
            "entries": [],
        }
        self._entries: dict[str, dict[str, Any]] = {}

    def load(self) -> None:
        if not self.path.exists():
            return
        document = json.loads(self.path.read_text(encoding="utf-8"))
        if document.get("schema") != SCHEMA:
            raise ValueError(f"Unsupported aquarium species catalogue schema in {self.path}")
        if document.get("schemaVersion") != SCHEMA_VERSION:
            raise ValueError(f"Unsupported aquarium species catalogue version in {self.path}")
        entries = document.get("entries")
        if not isinstance(entries, list):
            raise ValueError("Aquarium species catalogue entries must be an array")
        self.document = document
        self._entries = {str(entry["id"]): entry for entry in entries}

    def entry_for(self, candidate: Candidate) -> dict[str, Any]:
        return json.loads(json.dumps(self._entries.get(candidate.id, default_entry(candidate))))

    def status_for(self, candidate: Candidate) -> str:
        entry = self._entries.get(candidate.id)
        if not entry:
            return "unreviewed"
        return str(entry.get("review", {}).get("status", "unreviewed"))

    def upsert(self, entry: dict[str, Any]) -> bool:
        return self.upsert_many([entry]) > 0

    def upsert_many(self, entries: list[dict[str, Any]]) -> int:
        changed = 0
        for entry in entries:
            entry_id = str(entry["id"])
            if self._entries.get(entry_id) == entry:
                continue
            self._entries[entry_id] = json.loads(json.dumps(entry))
            changed += 1
        if not changed:
            return 0
        self.document["catalogRevision"] = int(self.document.get("catalogRevision", 0)) + 1
        self.document["entries"] = sorted(
            self._entries.values(), key=lambda item: (int(item["dex"]), str(item["form"]))
        )
        self._write_atomic()
        return changed

    def seed_initial_approvals(self, candidates: list[Candidate]) -> bool:
        changed = "movementProfiles" not in self.document
        self.document.setdefault("movementProfiles", MOVEMENT_PROFILE_DEFAULTS)
        for name, profile in MOVEMENT_PROFILE_DEFAULTS.items():
            if name not in self.document["movementProfiles"]:
                self.document["movementProfiles"][name] = json.loads(json.dumps(profile))
                changed = True
        for candidate in candidates:
            if candidate.id in INITIAL_APPROVALS and candidate.id not in self._entries:
                self._entries[candidate.id] = default_entry(candidate)
                changed = True
        if not changed:
            return False
        self.document["catalogRevision"] = int(self.document.get("catalogRevision", 0)) + 1
        self.document["entries"] = sorted(
            self._entries.values(), key=lambda item: (int(item["dex"]), str(item["form"]))
        )
        self._write_atomic()
        return True

    def _write_atomic(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        payload = json.dumps(self.document, indent=2, ensure_ascii=False) + "\n"
        fd, temporary_name = tempfile.mkstemp(prefix=f".{self.path.name}.", dir=self.path.parent)
        temporary = Path(temporary_name)
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as handle:
                handle.write(payload)
                handle.flush()
                os.fsync(handle.fileno())
            temporary.replace(self.path)
        finally:
            if temporary.exists():
                temporary.unlink()
