#!/usr/bin/env python3
"""Browser references for Orion's CSS conformance fixtures.

A fixture is tests/conformance/<suite>/<name>.orion and <name>.css. This script
writes <name>.html, the same form as a web page (forms and stacks become
flexboxes, as docs/css-roadmap.md maps them), and renders it in headless Chrome
to <name>.png. tests/css_conformance_test.c paints the form in Orion and
compares the two.

usage: tools/conformance/render.py [fixture.orion ...]   (default: every fixture)
Chrome: $ORION_CHROME, google-chrome / chromium on PATH, or chrome-headless-shell
installed with
  npx @puppeteer/browsers install chrome-headless-shell@stable --path ~/.cache/orion-conformance
"""
import glob, html, os, shutil, subprocess, sys, tempfile
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FIXTURES = os.path.join(ROOT, "tests", "conformance")
CONTAINERS = {"StackView", "GridView", "FlowView"}
DEFAULT_SPACING = 4   # FL_DEFAULT_SPACING in orion/commctl/form_loader.c: the form root's gap


def chrome():
  if os.environ.get("ORION_CHROME"): return os.environ["ORION_CHROME"]
  for name in ("google-chrome", "google-chrome-stable", "chromium", "chromium-browser"):
    if shutil.which(name): return shutil.which(name)
  found = sorted(glob.glob(os.path.expanduser(
    "~/.cache/orion-conformance/chrome-headless-shell/*/*/chrome-headless-shell")))
  if found: return found[-1]
  sys.exit("render.py: no Chrome; set ORION_CHROME or install chrome-headless-shell (see --help)")


FONTS = os.path.join(ROOT, "share", "fonts")
FAMILIES = {"IBMPlexSans": "IBM Plex Sans", "IBMPlexMono": "IBM Plex Mono", "DejaVuSans": "DejaVu Sans"}
WEIGHTS = {"Thin": 100, "ExtraLight": 200, "Light": 300, "Regular": 400, "Medium": 500,
           "SemiBold": 600, "Bold": 700, "ExtraBold": 800, "Black": 900}


def font_faces(base):
  """@font-face rules for share/fonts. Orion reads a face by file name (docs/styling.md, Type)
  and its sheet parser skips @font-face, so the page carries them and fixture sheets never do.
  Oblique and unknown files are left out."""
  rules = []
  for path in sorted(glob.glob(os.path.join(FONTS, "*.ttf"))):
    stem, weight = os.path.basename(path)[:-4], "Regular"
    if "-" in stem: stem, weight = stem.split("-", 1)
    if stem not in FAMILIES or weight not in WEIGHTS: continue
    url = os.path.relpath(path, os.path.dirname(base)).replace(os.sep, "/")
    rules.append(f'  @font-face {{ font-family: "{FAMILIES[stem]}"; font-weight: {WEIGHTS[weight]}; '
                 f'src: url("{url}"); }}\n')
  return "".join(rules)


def px(value):
  return " ".join(f"{int(v)}px" for v in value.split())


def padding(value):
  # .orion padding is one value, "horizontal vertical", or "left top right bottom".
  v = value.split()
  if len(v) == 1: return f"{v[0]}px"
  if len(v) == 2: return f"{v[1]}px {v[0]}px"
  return f"{v[1]}px {v[2]}px {v[3]}px {v[0]}px"


def layout_style(node, root=False, cell=None):
  """Inline style of a node. Orion attributes are inline styles: what the form leaves out
  (spacing, padding, orientation) is the style sheet's to decide, and the page's
  user-agent rule below stands for base.css."""
  s = []
  if root or node.tag in CONTAINERS:
    columns = [c for c in node if c.tag == "Column"] if node.tag == "GridView" else []
    if columns:
      s.append("display:grid")
      s.append("grid-template-columns:" + " ".join("auto" if c.get("width") == "auto" else "1fr" for c in columns))
    else:
      s.append("display:flex")
      if root or node.get("orientation"):
        s.append(f"flex-direction:{'row' if node.get('orientation') == 'horizontal' else 'column'}")
    if root: s.append(f"gap:{int(node.get('spacing', DEFAULT_SPACING))}px")
    elif node.get("spacing") is not None: s.append(f"gap:{int(node.get('spacing'))}px")
    if node.get("padding"): s.append(f"padding:{padding(node.get('padding'))}")
    elif root: s.append("padding:0px")
  has_children = len(list(node)) > 0
  if root or not has_children:   # a container's own size comes from its children
    if node.get("width"): s.append(f"width:{int(node.get('width'))}px")
    if node.get("height"): s.append(f"height:{int(node.get('height'))}px")
  if cell: s.append(f"grid-area:{cell[0] + 1}/{cell[1] + 1}")
  if "flexspace" in (node.get("flags") or ""): s.append("flex:1 1 0")
  elif not root: s.append("flex:none")
  return ";".join(s)


def grid_cells(node):
  """A GridView's columns hold its cells top to bottom; the grid places each in its row and column."""
  columns = [c for c in node if c.tag == "Column"]
  return [(cell, r, c) for c, col in enumerate(columns) for r, cell in enumerate(col)]


def element(node, depth, cell=None):
  pad = "  " * depth
  attrs = ""
  if node.get("name"): attrs += f' id="{html.escape(node.get("name"))}"'
  if node.get("class"): attrs += f' class="{html.escape(node.get("class"))}"'
  attrs += f' style="{layout_style(node, cell=cell)}"'
  if node.tag == "GridView" and any(c.tag == "Column" for c in node):
    inner = "".join(element(c, depth + 1, (r, col)) for c, r, col in grid_cells(node))
  else:
    inner = "".join(element(c, depth + 1) for c in node)
  text = html.escape(node.get("text", "")) if node.tag not in CONTAINERS else ""
  if inner: return f"{pad}<{node.tag}{attrs}>\n{inner}{pad}</{node.tag}>\n"
  return f"{pad}<{node.tag}{attrs}>{text}</{node.tag}>\n"


def page(orion_path):
  base = os.path.splitext(orion_path)[0]
  tree = ET.parse(orion_path).getroot()
  form = tree.find(".//form")
  if form is None: sys.exit(f"{orion_path}: no <form>")
  css = os.path.splitext(os.path.basename(orion_path))[0] + ".css"
  body = "".join(element(c, 2) for c in form)
  w, h = int(form.get("width")), int(form.get("height"))
  return w, h, f"""<!doctype html>
<!-- Generated by tools/conformance/render.py from {os.path.basename(orion_path)}; do not edit. -->
<html>
<head>
<meta charset="utf-8">
<style>
  /* Orion's box model: borders inside the frame, no default margins. */
  html, body {{ margin: 0; background: transparent; }}
  * {{ box-sizing: border-box; min-width: 0; min-height: 0; }}
  /* base.css's container defaults, which spacing=, padding= and orientation= override. */
  StackView, GridView, FlowView {{ display: flex; flex-direction: column; gap: 4px; padding: 0; }}
  /* A Label is one inline-level box of line boxes (its line-height); as a flex item it is
     blockified, and a width makes it wrap. */
  Label {{ display: inline; }}
{font_faces(base)}</style>
<link rel="stylesheet" href="{css}">
</head>
<body>
  <div style="{layout_style(form, root=True)}">
{body}  </div>
</body>
</html>
"""


def render(orion_path, browser):
  w, h, text = page(orion_path)
  base = os.path.splitext(orion_path)[0]
  with open(base + ".html", "w") as f: f.write(text)
  with tempfile.TemporaryDirectory() as profile:
    subprocess.run([browser, "--headless", "--disable-gpu", "--hide-scrollbars", "--force-color-profile=srgb",
                    "--force-device-scale-factor=1", "--default-background-color=00000000",
                    f"--user-data-dir={profile}", f"--window-size={w},{h}", f"--screenshot={base}.png",
                    "file://" + os.path.abspath(base + ".html")],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
  print(os.path.relpath(base + ".png", ROOT))


def main():
  if any(a in ("-h", "--help") for a in sys.argv[1:]): print(__doc__); return
  paths = sys.argv[1:] or sorted(glob.glob(os.path.join(FIXTURES, "*", "*.orion")))
  browser = chrome()
  for p in paths: render(p, browser)


if __name__ == "__main__":
  main()
