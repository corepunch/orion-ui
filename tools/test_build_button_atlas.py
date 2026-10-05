"""Atlas geometry and reproducibility checks; run with python3 -m unittest discover -s tools -p test_build_button_atlas.py."""
import copy
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

from build_button_atlas import authored_atlas, build_svg, ROOT, STATES

MANIFEST = ROOT / "apps/groove/share/icons/transport.json"
CONFIG = json.loads(MANIFEST.with_name("transport-svg.json").read_text())
AUTHORED_CONFIG = json.loads(MANIFEST.read_text())


class ButtonAtlasTest(unittest.TestCase):
    def test_fixed_cells_and_pressed_glyph(self):
        root = ET.fromstring(build_svg(CONFIG, MANIFEST.parent))
        ns = {"svg": "http://www.w3.org/2000/svg"}
        cells = root.findall("svg:g", ns)
        self.assertEqual(len(cells), len(CONFIG["icons"]) * len(STATES))
        self.assertEqual(root.get("width"), "1024")
        self.assertEqual(root.get("height"), "640")
        for index, cell in enumerate(cells):
            row, col = divmod(index, len(CONFIG["icons"]))
            self.assertEqual(cell.get("transform"), f"translate({col*32},{row*32})")
            face = cell.findall("svg:rect", ns)[1]
            self.assertEqual([face.get(key) for key in ("x", "y", "width", "height", "rx")],
                             ["4", "4", "24", "24", "6"])
            glyph = cell.find("svg:g", ns)
            self.assertEqual(glyph.get("transform"),
                             f"translate(8.0,{10.0 if row == 2 else 8.0}) scale({16/24})")
        gradients = root.findall("svg:defs/svg:linearGradient", ns)
        self.assertEqual(len(gradients), 40)
        self.assertTrue(all(g.get("y2") == "100%" for g in gradients))

    def test_rejects_invalid_geometry(self):
        for key, value in (("cell_size", 0), ("scale", 0), ("press_offset", 3), ("icons", [])):
            config = copy.deepcopy(CONFIG)
            config[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                build_svg(config, MANIFEST.parent)

    def test_svg_render_is_reproducible(self):
        renderer = ROOT / "build/bin/svg_atlas_render"
        self.assertTrue(renderer.exists(), "Run make groove-icons first")
        with tempfile.TemporaryDirectory(prefix="orion-atlas-test-") as directory:
            source, output = Path(directory) / "atlas.svg", Path(directory) / "atlas.png"
            source.write_text(build_svg(CONFIG, MANIFEST.parent))
            subprocess.run([str(renderer), str(source), str(output)], check=True)
            data = output.read_bytes()
            self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
            self.assertEqual(struct.unpack(">II", data[16:24]), (1024, 640))
            subprocess.run([str(renderer), str(source), str(output)], check=True)
            self.assertEqual(data, output.read_bytes())

    def test_authored_pixels_and_fixed_state_geometry(self):
        data, regions = authored_atlas(AUTHORED_CONFIG, MANIFEST.parent)
        self.assertEqual(data, MANIFEST.with_suffix(".png").read_bytes())
        self.assertEqual(len(regions), 40)
        for index, region in enumerate(regions):
            row, col = divmod(index, 8)
            self.assertEqual(region, (col * 198, row * 198, 198, 198))

    def test_rejects_authored_cells_outside_png(self):
        for key, value in (("cell_size", 199), ("columns", 9), ("origin", [-1, 0]), ("origin", [0, 3])):
            config = copy.deepcopy(AUTHORED_CONFIG)
            config[key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                authored_atlas(config, MANIFEST.parent)

    def test_preview_leaves_committed_metadata_and_art_unchanged(self):
        original = MANIFEST.with_suffix(".png").read_bytes()
        header = MANIFEST.with_suffix(".h").read_bytes()
        with tempfile.TemporaryDirectory(prefix="orion-atlas-preview-") as directory:
            preview = Path(directory) / "atlas.png"
            subprocess.run(["python3", str(ROOT / "tools/build_button_atlas.py"),
                            str(MANIFEST), "--output", str(preview)], check=True, capture_output=True)
            self.assertEqual(preview.read_bytes(), original)
        self.assertEqual(MANIFEST.with_suffix(".h").read_bytes(), header)
        self.assertEqual(MANIFEST.with_suffix(".png").read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
