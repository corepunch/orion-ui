#!/usr/bin/env python3
"""Kenney swatches work as kit colours in Python and Lua, and as <material>s in a scene.

    python3 apps/scener/tests/test_kenney_palette.py [scener-binary]
"""
import os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import building_kit as bk

def main():
  pal = bk.palette("fantasy_town")
  assert len(pal) > 30, len(pal)
  name, (bottom, top) = next((n, s) for n, s in pal.items() if s[0] != s[1])
  k = bk.Kit(2, 2, {})
  k.box("stone", 0, 0, 0, 1, 1, 1, (bottom, top))
  line = k.lines[0]
  assert 'color="1 1 1"' in line and "gradientFrom=" in line and "gradientTo=" in line, line
  if shutil.which("lua"):
    lua = 'package.path="%s/?.lua;"..package.path local K=require("building_kit") local p=K.palette("fantasy_town") local n=0 for _ in pairs(p) do n=n+1 end assert(n==%d, n) local k=K.new_kit(2,2,{ids={},order={}}) k:box("stone",0,0,0,1,1,1,p[%r]) print(k.lines[1])' % (
      os.path.join(HERE, "..", "tools"), len(pal), name)
    out = subprocess.run(["lua", "-e", lua], check=True, capture_output=True, text=True).stdout.strip()
    assert out == line, (out, line)
  if len(sys.argv) > 1:
    with tempfile.TemporaryDirectory() as d:
      scene = os.path.join(d, "s.blks")
      mats = open(os.path.join(HERE, "..", "palettes", "kenney_fantasy_town.xml")).read()
      open(scene, "w").write('<scene><camera name="C" pos="0 100 300" look="0 50 0"/><sun dir="0 -1 -1"/>%s<box size="50 100 50" pos="0 50 0" material="%s"/></scene>' % (mats, name))
      r = subprocess.run([sys.argv[1], "--check", scene], capture_output=True, text=True)
      assert r.returncode == 0, r.stdout + r.stderr
  print("ok")

main()
