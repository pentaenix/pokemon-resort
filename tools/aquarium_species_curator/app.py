"""Qt aquarium species curator using RAE's authoritative GLB preview."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

from PySide6.QtCore import QTimer, Qt
from PySide6.QtWidgets import (
    QApplication, QCheckBox, QComboBox, QDoubleSpinBox, QFormLayout, QHBoxLayout,
    QLabel, QLineEdit, QMainWindow, QMessageBox, QPushButton, QScrollArea,
    QSpinBox, QSplitter, QTextEdit, QVBoxLayout, QWidget,
)

from src.ui.preview.web_preview import WebGlbPreviewWidget

from .capacity_mask import CapacityMaskWidget
from .catalog import Candidate, CatalogStore, build_candidates
from .glb_asset import animation_names, prepare_preview_asset, transformed_preview_asset
from .presets import PRESETS, preset_values

VERTICAL_ZONES = ("surface", "upper-water", "open-water", "lower-water", "bottom")
SURFACE_BEHAVIORS = ("submerged", "touches-surface", "top-protrudes", "stands-on-surface")
MOVEMENT_PROFILES = (
    "free-swimmer", "schooling", "escort", "hover", "jelly-drift", "surface-floater", "surface-walker",
    "large-cruiser", "benthic-rest-swimmer", "bottom-crawler", "bottom-swimmer", "bottom-stationary",
    "bottom-burrower", "timid-reef", "anchored",
)
REJECTION_REASONS = (
    "No suitable swimming animation", "Too terrestrial", "Model presentation is unsuitable",
    "Movement cannot be represented yet", "Missing usable model", "Other",
)


class CuratorWindow(QMainWindow):
    def __init__(self, project_root: Path, catalog_path: Path):
        super().__init__()
        self.project_root = project_root
        self.cache_root = project_root / "build/aquarium_species_curator_cache"
        self.store = CatalogStore(catalog_path)
        self.store.load()
        self.candidates = build_candidates(project_root)
        self.store.seed_initial_approvals(self.candidates)
        self.filtered: list[Candidate] = []
        self.current_index = 0
        self.current_preview: Path | None = None
        self.current_source_preview: Path | None = None
        self.loaded_entry: dict | None = None
        self.loading_form = False
        self.transform_timer = QTimer(self)
        self.transform_timer.setSingleShot(True)
        self.transform_timer.setInterval(220)
        self.transform_timer.timeout.connect(self._reload_transformed_preview)

        self.setWindowTitle("Pokémon Resort — Aquarium Species Curator")
        self.resize(1320, 820)
        self._build_ui()
        self._refresh_filter(select_id=None)

    def _build_ui(self) -> None:
        splitter = QSplitter(Qt.Orientation.Horizontal)
        self.setCentralWidget(splitter)

        preview_host = QWidget()
        preview_layout = QVBoxLayout(preview_host)
        preview_layout.setContentsMargins(8, 8, 4, 8)
        self.viewer = WebGlbPreviewWidget()
        self.viewer.set_preview_platform("threeds")
        preview_layout.addWidget(self.viewer, 1)
        animation_row = QHBoxLayout()
        animation_row.addWidget(QLabel("Animation"))
        self.animation = QComboBox()
        self.animation.setSizeAdjustPolicy(QComboBox.SizeAdjustPolicy.AdjustToContents)
        self.animation.currentTextChanged.connect(self._play_animation)
        animation_row.addWidget(self.animation, 1)
        play = QPushButton("Play")
        play.clicked.connect(self._play_animation)
        animation_row.addWidget(play)
        stop = QPushButton("Stop")
        stop.clicked.connect(self.viewer.stop_animation)
        animation_row.addWidget(stop)
        reset = QPushButton("Reset view")
        reset.clicked.connect(self.viewer.reset_view)
        animation_row.addWidget(reset)
        preview_layout.addLayout(animation_row)
        self.preview_error = QLabel()
        self.preview_error.setStyleSheet("color: #b03030;")
        self.preview_error.setWordWrap(True)
        preview_layout.addWidget(self.preview_error)
        splitter.addWidget(preview_host)

        inspector_scroll = QScrollArea()
        inspector_scroll.setWidgetResizable(True)
        inspector = QWidget()
        inspector_scroll.setWidget(inspector)
        right = QVBoxLayout(inspector)
        right.setContentsMargins(10, 8, 10, 8)

        search_row = QHBoxLayout()
        self.search = QLineEdit()
        self.search.setPlaceholderText("Filter by name or Pokédex number")
        self.search.textChanged.connect(lambda _text: self._refresh_filter(self._current_id()))
        search_row.addWidget(self.search, 1)
        self.filter = QComboBox()
        self.filter.addItems(("Review queue", "Proposed", "All", "Approved", "Rejected", "Missing model"))
        self.filter.currentTextChanged.connect(lambda _text: self._refresh_filter(self._current_id()))
        search_row.addWidget(self.filter)
        right.addLayout(search_row)

        nav = QHBoxLayout()
        previous = QPushButton("Previous")
        previous.clicked.connect(lambda: self._navigate(-1))
        nav.addWidget(previous)
        self.progress = QLabel()
        self.progress.setAlignment(Qt.AlignmentFlag.AlignCenter)
        nav.addWidget(self.progress, 1)
        following = QPushButton("Next")
        following.clicked.connect(lambda: self._navigate(1))
        nav.addWidget(following)
        right.addLayout(nav)

        self.heading = QLabel()
        self.heading.setStyleSheet("font-size: 20px; font-weight: 600;")
        right.addWidget(self.heading)
        self.model_status = QLabel()
        self.model_status.setWordWrap(True)
        right.addWidget(self.model_status)

        form = QFormLayout()
        self.preset = self._combo(("Custom", *PRESETS.keys()))
        self.preset.currentTextChanged.connect(self._apply_preset)
        form.addRow("Behavior preset", self.preset)
        self.vertical = self._combo(VERTICAL_ZONES)
        form.addRow("Preferred vertical zone", self.vertical)
        self.surface = self._combo(SURFACE_BEHAVIORS)
        form.addRow("Waterline behavior", self.surface)
        self.movement = self._combo(MOVEMENT_PROFILES)
        form.addRow("Movement profile", self.movement)
        self.direction = self._combo(("forward", "backward", "sideways"))
        form.addRow("Travel direction", self.direction)
        self.rest_animation = self._combo(())
        form.addRow("Rest animation", self.rest_animation)
        self.pitch = self._double_spin(-180, 180, 5, "°")
        self.pitch.valueChanged.connect(self._schedule_transform)
        form.addRow("Model pitch", self.pitch)
        self.yaw = self._double_spin(-180, 180, 5, "°")
        self.yaw.valueChanged.connect(self._schedule_transform)
        form.addRow("Model yaw", self.yaw)
        self.scale = self._double_spin(0.1, 5.0, 0.05, "×")
        self.scale.valueChanged.connect(self._schedule_transform)
        form.addRow("Scale multiplier", self.scale)
        self.waterline_offset = self._double_spin(-1.0, 1.0, 0.05, " body heights")
        form.addRow("Waterline offset", self.waterline_offset)
        self.minimum_group = self._integer_spin(1, 99)
        form.addRow("Minimum group", self.minimum_group)
        self.preferred_group = self._integer_spin(1, 99)
        form.addRow("Preferred group", self.preferred_group)
        right.addLayout(form)

        right.addWidget(QLabel("Water kinds"))
        water_row = QHBoxLayout()
        self.freshwater = QCheckBox("Freshwater")
        self.saltwater = QCheckBox("Saltwater")
        self.brackish = QCheckBox("Brackish")
        for checkbox in (self.freshwater, self.saltwater, self.brackish):
            water_row.addWidget(checkbox)
        right.addLayout(water_row)

        right.addWidget(QLabel("Capacity footprint — click cells to paint"))
        self.capacity = CapacityMaskWidget()
        right.addWidget(self.capacity)
        right.addWidget(QLabel("Curator notes"))
        self.notes = QTextEdit()
        self.notes.setMaximumHeight(70)
        right.addWidget(self.notes)
        right.addWidget(QLabel("Rejection reason"))
        self.rejection_reason = QComboBox()
        self.rejection_reason.setEditable(True)
        self.rejection_reason.addItems(REJECTION_REASONS)
        right.addWidget(self.rejection_reason)

        actions = QHBoxLayout()
        reject = QPushButton("Reject")
        reject.clicked.connect(self._reject)
        actions.addWidget(reject)
        skip = QPushButton("Save / Skip")
        skip.clicked.connect(self._skip)
        actions.addWidget(skip)
        approve = QPushButton("Approve")
        approve.clicked.connect(self._approve)
        approve.setDefault(True)
        actions.addWidget(approve)
        right.addLayout(actions)
        self.save_status = QLabel()
        self.save_status.setWordWrap(True)
        right.addWidget(self.save_status)
        right.addStretch()
        splitter.addWidget(inspector_scroll)
        splitter.setSizes([850, 470])

    @staticmethod
    def _combo(values) -> QComboBox:
        combo = QComboBox()
        combo.addItems(values)
        return combo

    @staticmethod
    def _double_spin(low: float, high: float, step: float, suffix: str) -> QDoubleSpinBox:
        spin = QDoubleSpinBox()
        spin.setRange(low, high)
        spin.setSingleStep(step)
        spin.setSuffix(suffix)
        spin.setDecimals(2)
        return spin

    @staticmethod
    def _integer_spin(low: int, high: int) -> QSpinBox:
        spin = QSpinBox()
        spin.setRange(low, high)
        return spin

    def _current_id(self) -> str | None:
        return self.filtered[self.current_index].id if self.filtered else None

    def _refresh_filter(self, select_id: str | None) -> None:
        text = self.search.text().casefold().strip() if hasattr(self, "search") else ""
        mode = self.filter.currentText() if hasattr(self, "filter") else "Review queue"
        filtered = []
        for candidate in self.candidates:
            status = self.store.status_for(candidate)
            haystack = f"{candidate.dex} {candidate.display_name} {candidate.species}".casefold()
            if text and text not in haystack:
                continue
            if mode == "Review queue" and (not candidate.has_model or status not in {"unreviewed", "proposed"}):
                continue
            wanted = {"Proposed": "proposed", "Approved": "approved", "Rejected": "rejected"}.get(mode)
            if wanted and status != wanted:
                continue
            if mode == "Missing model" and candidate.has_model:
                continue
            filtered.append(candidate)
        self.filtered = filtered
        self.current_index = 0
        if select_id:
            for index, candidate in enumerate(filtered):
                if candidate.id == select_id:
                    self.current_index = index
                    break
        self._load_current()

    def _load_current(self) -> None:
        self.loading_form = True
        self.preview_error.clear()
        self.viewer.clear_scene()
        self.current_preview = None
        self.current_source_preview = None
        if not self.filtered:
            self.heading.setText("No matching candidates")
            self.progress.setText("0 / 0")
            self.model_status.clear()
            self.loading_form = False
            return
        candidate = self.filtered[self.current_index]
        entry = self.store.entry_for(candidate)
        self.loaded_entry = json.loads(json.dumps(entry))
        status = entry["review"]["status"]
        self.progress.setText(f"{self.current_index + 1} / {len(self.filtered)}")
        self.heading.setText(f"#{candidate.dex:04d}  {candidate.display_name}  [{status}]")
        self.model_status.setText(
            f"Form: {candidate.form}   Sources: {', '.join(candidate.sources)}\n"
            + (f"Model: {candidate.model_path}" if candidate.has_model else "Model: not available in the current asset pack")
        )
        presentation = entry["presentation"]
        habitat = entry["habitat"]
        behavior = entry["behavior"]
        self.preset.setCurrentIndex(0)
        self._set_combo(self.vertical, habitat["verticalZone"])
        self._set_combo(self.surface, habitat["surfaceBehavior"])
        self._set_combo(self.movement, behavior["movementProfile"])
        self._set_combo(self.direction, behavior.get("travelDirection", "forward"))
        self.pitch.setValue(float(presentation["pitchDegrees"]))
        self.yaw.setValue(float(presentation["yawDegrees"]))
        self.scale.setValue(float(presentation["scaleMultiplier"]))
        self.waterline_offset.setValue(float(presentation.get("waterlineOffsetBodyHeights", 0.0)))
        self.minimum_group.setValue(int(behavior["minimumGroup"]))
        self.preferred_group.setValue(int(behavior["preferredGroup"]))
        kinds = set(habitat.get("waterKinds", []))
        self.freshwater.setChecked("freshwater" in kinds)
        self.saltwater.setChecked("saltwater" in kinds)
        self.brackish.setChecked("brackish" in kinds)
        self.capacity.set_mask(entry["capacity"]["mask"])
        self.notes.setPlainText(entry["review"].get("notes", ""))
        reason = entry["review"].get("rejectionReason", "")
        self.rejection_reason.setCurrentText(reason or REJECTION_REASONS[0])
        self.animation.clear()
        self.rest_animation.clear()
        self.loading_form = False
        if candidate.has_model:
            try:
                source = self.project_root / candidate.model_path
                self.current_source_preview = prepare_preview_asset(source, self.cache_root)
                names = animation_names(self.current_source_preview)
                self.animation.addItems(names)
                self.animation.setCurrentText(str(presentation.get("animation", "")))
                self.rest_animation.addItems(names)
                self.rest_animation.setCurrentText(str(behavior.get("idleAnimation", "slot4_00")))
                self._reload_transformed_preview()
            except Exception as exc:
                self.preview_error.setText(f"Could not prepare preview: {exc}")
        else:
            self.preview_error.setText("This requested candidate has no renderable model yet.")

    @staticmethod
    def _set_combo(combo: QComboBox, value: str) -> None:
        index = combo.findText(value)
        combo.setCurrentIndex(max(0, index))

    def _play_animation(self, *_args) -> None:
        if self.loading_form:
            return
        name = self.animation.currentText()
        if name:
            self.viewer.play_animation(name)

    def _apply_preset(self, name: str) -> None:
        if self.loading_form:
            return
        values = preset_values(name, [self.animation.itemText(i) for i in range(self.animation.count())])
        if not values:
            return
        self._set_combo(self.movement, values["movement"])
        self._set_combo(self.vertical, values["vertical"])
        self._set_combo(self.surface, values["surface"])
        self._set_combo(self.direction, values["direction"])
        self.minimum_group.setValue(values["minimumGroup"])
        self.preferred_group.setValue(values["preferredGroup"])
        if values["animation"]:
            self.animation.setCurrentText(values["animation"])
        self.rest_animation.setCurrentText(values["idleAnimation"])
        self.save_status.setText(f"{name} preset applied. Adjust any field, then approve or save/skip.")

    def _schedule_transform(self, *_args) -> None:
        if not self.loading_form and self.current_source_preview is not None:
            self.transform_timer.start()

    def _reload_transformed_preview(self) -> None:
        if self.current_source_preview is None:
            return
        try:
            self.current_preview = transformed_preview_asset(
                self.current_source_preview,
                self.cache_root,
                self.pitch.value(),
                self.yaw.value(),
                self.scale.value(),
            )
            self.viewer.load_glb(self.current_preview, preview_platform_id="threeds")
            QTimer.singleShot(650, self._play_animation)
        except Exception as exc:
            self.preview_error.setText(f"Could not apply preview orientation: {exc}")

    def _capture(self, status: str | None = None) -> dict | None:
        if not self.filtered:
            return None
        candidate = self.filtered[self.current_index]
        entry = self.store.entry_for(candidate)
        if status:
            entry["review"]["status"] = status
        entry["review"]["notes"] = self.notes.toPlainText().strip()
        entry["review"]["rejectionReason"] = (
            self.rejection_reason.currentText().strip()
            if entry["review"]["status"] == "rejected" else ""
        )
        entry["model"]["path"] = candidate.model_path
        entry["presentation"] = {
            "animation": self.animation.currentText().strip(),
            "pitchDegrees": self.pitch.value(),
            "yawDegrees": self.yaw.value(),
            "scaleMultiplier": self.scale.value(),
            "waterlineOffsetBodyHeights": self.waterline_offset.value(),
        }
        kinds = []
        if self.freshwater.isChecked(): kinds.append("freshwater")
        if self.saltwater.isChecked(): kinds.append("saltwater")
        if self.brackish.isChecked(): kinds.append("brackish")
        entry["habitat"] = {
            "verticalZone": self.vertical.currentText(),
            "surfaceBehavior": self.surface.currentText(),
            "waterKinds": kinds,
        }
        behavior = dict(entry.get("behavior", {}))
        behavior.update({
            "movementProfile": self.movement.currentText(),
            "travelDirection": self.direction.currentText(),
            "idleAnimation": self.rest_animation.currentText(),
            "idlePitchDegrees": float(entry.get("behavior", {}).get("idlePitchDegrees", 0.0)),
            "minimumGroup": self.minimum_group.value(),
            "preferredGroup": max(self.minimum_group.value(), self.preferred_group.value()),
        })
        entry["behavior"] = behavior
        entry["capacity"] = {"mask": self.capacity.mask()}
        return entry

    def _save(self, status: str | None = None) -> bool:
        entry = self._capture(status)
        if not entry:
            return False
        if status is None and entry == self.loaded_entry:
            self.save_status.setText("No changes to save.")
            return False
        changed = self.store.upsert(entry)
        self.loaded_entry = json.loads(json.dumps(entry))
        self.save_status.setText(
            f"Saved revision {self.store.document['catalogRevision']} to {self.store.path}"
            if changed else "No changes to save."
        )
        return True

    def _navigate(self, offset: int) -> None:
        self._save()
        if not self.filtered:
            return
        self.current_index = (self.current_index + offset) % len(self.filtered)
        self._load_current()

    def _after_decision(self, decided_id: str) -> None:
        old_index = self.current_index
        self._refresh_filter(select_id=decided_id)
        if self.filtered and self.filter.currentText() == "Review queue":
            self.current_index = min(old_index, len(self.filtered) - 1)
            self._load_current()

    def _approve(self) -> None:
        if not self.filtered:
            return
        candidate = self.filtered[self.current_index]
        if not candidate.has_model:
            QMessageBox.warning(self, "Cannot approve", "This candidate has no model in the current asset pack.")
            return
        if not self.animation.currentText() or not self.capacity.mask():
            QMessageBox.warning(self, "Cannot approve", "Choose an animation and paint at least one capacity cell.")
            return
        if not any((self.freshwater.isChecked(), self.saltwater.isChecked(), self.brackish.isChecked())):
            QMessageBox.warning(self, "Cannot approve", "Choose at least one supported water kind.")
            return
        decided_id = candidate.id
        self._save("approved")
        self._after_decision(decided_id)

    def _reject(self) -> None:
        if not self.filtered:
            return
        decided_id = self.filtered[self.current_index].id
        self._save("rejected")
        self._after_decision(decided_id)

    def _skip(self) -> None:
        self._save("unreviewed")
        self._navigate(1)

    def closeEvent(self, event) -> None:
        self._save()
        super().closeEvent(event)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Review Pokémon for player-built aquariums")
    default_root = Path(__file__).resolve().parents[2]
    parser.add_argument("--project-root", type=Path, default=default_root)
    parser.add_argument("--catalog", type=Path)
    parser.add_argument(
        "--initialize-only",
        action="store_true",
        help="create/seed the catalogue without opening the Qt window",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    project_root = args.project_root.resolve()
    catalog = args.catalog or project_root / "config/gameplay/world3d/aquarium_species.json"
    if args.initialize_only:
        store = CatalogStore(catalog.resolve())
        store.load()
        candidates = build_candidates(project_root)
        store.seed_initial_approvals(candidates)
        print(
            f"Aquarium curator catalogue: {catalog.resolve()} "
            f"({len(candidates)} candidates, revision {store.document['catalogRevision']})"
        )
        return 0
    app = QApplication(sys.argv[:1])
    window = CuratorWindow(project_root, catalog.resolve())
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
