#!/usr/bin/env python3
"""Build SVG atlases or stage authored PNG states unchanged with fixed cell metadata."""
import argparse
import json
import re
import struct
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET

STATES = ("normal", "selected", "pressed", "hover", "disabled")
ROOT = Path(__file__).resolve().parents[1]


def authored_atlas(config, directory):
    data = (directory / config["source"]).read_bytes()
    if (data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR" or
            len(data) < 33 or data[24:26] != b"\x08\x06"):
        raise ValueError("expected an 8-bit RGBA PNG atlas")
    width, height = struct.unpack(">II", data[16:24])
    columns, size = config["columns"], config["cell_size"]
    x, y = config.get("origin", [0, 0])
    if (not all(isinstance(value, int) for value in (columns, size, x, y)) or
            columns < 1 or columns > 512 or size < 1 or x < 0 or y < 0 or
            x + columns * size > width or y + len(STATES) * size > height):
        raise ValueError("fixed cells for all five states must fit inside the source PNG")
    regions = [(x + col * size, y + row * size, size, size)
               for row in range(len(STATES)) for col in range(columns)]
    return data, regions


def mix(a, b, amount):
    a, b = a.lstrip("#"), b.lstrip("#")
    return "#" + "".join(f"{round(int(a[i:i+2], 16) * (1-amount) + int(b[i:i+2], 16) * amount):02x}"
                        for i in (0, 2, 4))


def glyph(path, color, solid):
    svg = ET.parse(path).getroot()
    if svg.get("viewBox") != "0 0 24 24":
        raise ValueError(f"{path}: expected a 24x24 SVG viewBox")
    parts = []
    for element in svg:
        element.tag = element.tag.split("}")[-1]
        if element.tag not in ("path", "rect", "circle", "polygon", "polyline", "line"):
            raise ValueError(f"{path}: unsupported glyph element {element.tag}")
        element.set("stroke", color)
        element.set("fill", color if solid else "none")
        element.set("stroke-width", "2")
        parts.append(ET.tostring(element, encoding="unicode"))
    return "".join(parts)


def build_svg(config, directory):
    size, scale = config["cell_size"], config["scale"]
    icons = config["icons"]
    if (not isinstance(size, int) or not isinstance(scale, int) or
            not 24 <= size <= 128 or not 1 <= scale <= 8 or not icons):
        raise ValueError("expected cell_size=24..128, scale=1..8 and a nonempty icon set")
    if not 0 <= config["press_offset"] <= 2:
        raise ValueError("press_offset must be 0..2 logical pixels")
    definitions, cells = [], []
    face_size = size - 8
    for row, state in enumerate(STATES):
        for col, icon in enumerate(icons):
            base = icon["color"]
            top, bottom = mix(base, "#ffffff", .30), mix(base, "#000000", .12)
            rim, ink = mix(base, "#ffffff", .62), "#ffffff"
            if state == "selected":
                top, bottom, rim = mix(base, "#ffffff", .48), base, "#d5f6ff"
            elif state == "pressed":
                top, bottom = mix(base, "#000000", .15), mix(base, "#000000", .32)
            elif state == "hover":
                top, bottom, rim = mix(base, "#ffffff", .45), mix(base, "#ffffff", .12), "#e4fbff"
            elif state == "disabled":
                top, bottom, rim, ink = "#3b5877", "#283e5b", "#617996", "#8fa7bd"
            name = f"face-{row}-{col}"
            definitions.append(f'<linearGradient id="{name}" x1="0%" y1="0%" x2="0%" y2="100%">'
                               f'<stop offset="0" stop-color="{top}"/><stop offset="1" stop-color="{bottom}"/></linearGradient>')
            drop = config["press_offset"] if state == "pressed" else 0
            art = glyph(directory / icon["svg"], ink, icon.get("solid", False))
            # Every state's geometry comes from the same coordinates, never from cropped artwork.
            cells.append(f'<g transform="translate({col*size},{row*size})">'
                         f'<rect x="4" y="7" width="{face_size}" height="{face_size}" rx="6" fill="#08182c" fill-opacity=".5"/>'
                         f'<rect x="4" y="4" width="{face_size}" height="{face_size}" rx="6" fill="url(#{name})" stroke="{rim}" stroke-width="1"/>'
                         f'<path d="M8 7 H{size-8}" fill="none" stroke="#ffffff" stroke-opacity="{.08 if state in ("pressed", "disabled") else .38}" stroke-linecap="round"/>'
                         f'<g transform="translate({(size-16)/2},{(size-16)/2+drop}) scale({16/24})">{art}</g></g>')
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{size*len(icons)*scale}" height="{size*5*scale}" '
            f'viewBox="0 0 {size*len(icons)} {size*5}"><defs>{"".join(definitions)}</defs>{"".join(cells)}</svg>')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--renderer", type=Path, default=ROOT / "build/bin/svg_atlas_render")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    config = json.loads(args.manifest.read_text())
    output = args.output or args.manifest.with_suffix(".png")
    regions = []
    if "source" in config:
        data, regions = authored_atlas(config, args.manifest.parent)
        columns, cell_size = config["columns"], config["cell_size"]
        output.write_bytes(data)
    else:
        svg = build_svg(config, args.manifest.parent)
        columns, cell_size = len(config["icons"]), config["cell_size"] * config["scale"]
        with tempfile.TemporaryDirectory(prefix="orion-atlas-") as directory:
            source = Path(directory) / "atlas.svg"
            source.write_text(svg)
            subprocess.run([str(args.renderer.resolve()), str(source), str(output.resolve())], check=True)
    if not args.output:
        prefix = re.sub(r"[^A-Z0-9_]", "_", args.manifest.stem.upper()) + "_ATLAS"
        region_data = ""
        if regions:
            region_data = f"\nstatic const irect16_t k_{prefix.lower()}_regions[] = {{\n"
            region_data += "".join(f"  {{{x:4}, {y:4}, {w}, {h}}},\n" for x, y, w, h in regions)
            region_data += "};\n"
        args.manifest.with_suffix(".h").write_text(
            f"// Generated by tools/build_button_atlas.py; edit {args.manifest.name}.\n"
            f"#ifndef __{prefix}_H__\n#define __{prefix}_H__\n\n"
            f"#define {prefix}_CELL_SIZE {cell_size}\n"
            f"#define {prefix}_COLUMNS   {columns}\n"
            f"#define {prefix}_STATES    {len(STATES)}\n"
            f"{region_data}\n#endif\n")
    print(f"{output}: {columns} columns x {len(STATES)} states, {cell_size}px cells")


if __name__ == "__main__":
    main()
