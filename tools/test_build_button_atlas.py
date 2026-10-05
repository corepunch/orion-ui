"""Atlas geometry and reproducibility checks; run with python3 -m unittest discover -s tools -p test_build_button_atlas.py."""
import copy
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

from build_button_atlas import build_svg, ROOT, STATES

MANIFEST = ROOT / "apps/groove/share/icons/transport.json"
CONFIG = json.loads(MANIFEST.read_text())


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

    def test_reproduces_committed_png(self):
        renderer = ROOT / "build/bin/svg_atlas_render"
        self.assertTrue(renderer.exists(), "Run make groove-icons first")
        with tempfile.TemporaryDirectory(prefix="orion-atlas-test-") as directory:
            source, output = Path(directory) / "atlas.svg", Path(directory) / "atlas.png"
            source.write_text(build_svg(CONFIG, MANIFEST.parent))
            subprocess.run([str(renderer), str(source), str(output)], check=True)
            data = output.read_bytes()
            self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
            self.assertEqual(struct.unpack(">II", data[16:24]), (1024, 640))
            self.assertEqual(data, MANIFEST.with_suffix(".png").read_bytes())


if __name__ == "__main__":
    unittest.main()
