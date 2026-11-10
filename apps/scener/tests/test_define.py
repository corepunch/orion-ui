#!/usr/bin/env python3
"""<define>/<include> utilities expand at load; mistakes are reported.

    python3 apps/scener/tests/test_define.py scener-binary
"""
import os, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
BIN = sys.argv[1]

def check(xml, files=None):
  with tempfile.TemporaryDirectory() as d:
    for n, t in (files or {}).items(): open(os.path.join(d, n), "w").write(t)
    p = os.path.join(d, "s.blks"); open(p, "w").write(xml)
    r = subprocess.run([BIN, "--check", p], capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr

HEAD = '<camera name="C" pos="0 100 300" look="0 50 0"/><sun dir="0 -1 -1"/>'
r = subprocess.run([BIN, "--check", os.path.join(HERE, "..", "scenes", "handwritten_hut.blks")], capture_output=True, text=True)
assert r.returncode == 0 and "0 warnings" in r.stdout and "7 objects" in r.stdout, r.stdout + r.stderr

lib = '<library><define name="peg"><param name="h" doc="height"/><param name="r" default="5"/><circle radius="${r}" rot="-90 0 0"><extrude amount="${h}"/></circle></define></library>'
rc, out = check('<scene>%s<include src="lib.xml"/><peg h="40" pos="0 20 0"/><peg h="{2*20}" r="3"/></scene>' % HEAD, {"lib.xml": lib})
assert rc == 0 and "2 objects" in out, out
rc, out = check('<scene>%s<include src="lib.xml"/><peg/></scene>' % HEAD, {"lib.xml": lib})
assert rc != 0 and "needs h" in out, out
rc, out = check('<scene>%s<define name="bad"><box size="${nope} 1 1"/></define><bad/></scene>' % HEAD)
assert rc != 0 and "unknown parameter" in out, out
rc, out = check('<scene>%s<define name="loop"><loop/></define><loop/></scene>' % HEAD)
assert rc != 0 and "calls itself" in out, out
line = '<shape closed="0"><v x="-50" y="-50"/><v x="50" y="-50"/><shell amount="100"/><fillet radius="10"/><extrude amount="60"/><bevel amount="2"/></shape>'
rc, out = check('<scene>%s%s</scene>' % (HEAD, line))
assert rc == 0 and "1 objects" in out, out
print("ok")
