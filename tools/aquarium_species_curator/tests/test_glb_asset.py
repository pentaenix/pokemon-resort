from __future__ import annotations

import json
from pathlib import Path
import struct
import tempfile
import unittest

from tools.aquarium_species_curator.glb_asset import animation_names, glb_document, transformed_preview_asset


class GlbAssetTests(unittest.TestCase):
    @staticmethod
    def _fixture() -> bytes:
        document = json.dumps({
            "asset": {"version": "2.0"},
            "scene": 0,
            "scenes": [{"nodes": [0]}],
            "nodes": [{"name": "Pokemon"}],
            "animations": [{"name": "idle"}, {"name": "swim"}],
        }).encode()
        document += b" " * ((4 - len(document) % 4) % 4)
        total = 12 + 8 + len(document)
        return struct.pack("<4sII", b"glTF", 2, total) + struct.pack("<II", len(document), 0x4E4F534A) + document

    def test_animation_names_reads_glb_json_chunk(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "model.glb"
            path.write_bytes(self._fixture())
            self.assertEqual(animation_names(path), ["idle", "swim"])

    def test_preview_transform_wraps_scene_without_changing_animations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "model.glb"
            source.write_bytes(self._fixture())
            transformed = transformed_preview_asset(source, root / "cache", -90, 45, 1.25)
            document = glb_document(transformed)
            wrapper = document["nodes"][document["scenes"][0]["nodes"][0]]
            self.assertEqual(wrapper["children"], [0])
            self.assertEqual(wrapper["scale"], [1.25, 1.25, 1.25])
            self.assertEqual(animation_names(transformed), ["idle", "swim"])


if __name__ == "__main__":
    unittest.main()
