#!/usr/bin/env python3
"""The Python and Lua building generators write the same scene, and it is the committed one.

    python3 apps/scener/tests/test_nile_building_generators.py [scener-binary]
Without lua on PATH the Lua half is skipped; with a scener binary the scene must load without errors.
"""
import os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS, SCENE = os.path.join(HERE, "..", "tools"), os.path.join(HERE, "..", "scenes", "nile_buildings.blks")

def run(cmd, **kw): return subprocess.run(cmd, check=True, capture_output=True, text=True, **kw)

def main():
  with tempfile.TemporaryDirectory() as d:
    py = os.path.join(d, "py.blks")
    run([sys.executable, "-B", os.path.join(TOOLS, "gen_nile_buildings.py"), py])
    assert open(py).read() == open(SCENE).read(), "scenes/nile_buildings.blks is stale: rerun gen_nile_buildings.py"
    if shutil.which("lua"):
      lua = os.path.join(d, "lua.blks")
      run(["lua", os.path.join(TOOLS, "gen_nile_buildings.lua"), lua])
      assert open(lua).read() == open(py).read(), "Lua and Python generators disagree"
    else:
      print("lua not found: skipped the Lua comparison")
    if len(sys.argv) > 1:
      shutil.copy(os.path.join(HERE, "..", "scenes", "utilities.xml"), d)   # the scene includes it
      r = subprocess.run([sys.argv[1], "--check", py], capture_output=True, text=True)
      assert r.returncode == 0, r.stdout + r.stderr
  print("ok")

main()
