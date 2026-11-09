#!/usr/bin/env python3
"""Exercise `scener --export` through the OBJ and glB exporter plugins.

usage: test_export.py SCENER EXPORTER_DIR [SCENE]
Run from the Orion root with the library path set (see CLI.md).
"""
import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

scener, exporters = sys.argv[1], Path(sys.argv[2]).resolve()
scene = sys.argv[3] if len(sys.argv) > 3 else 'apps/scener/scenes/sample_room.blks'
env = dict(os.environ, SCENER_EXPORTER_PATH=str(exporters))


def run(*args, expect=0):
    p = subprocess.run([scener, *args], capture_output=True, text=True, env=env)
    assert p.returncode == expect, f'{args}: exit {p.returncode}\n{p.stdout}\n{p.stderr}'
    return p


def close(a, b, tol=0.6):
    return all(abs(x - y) <= tol for x, y in zip(a, b))


def matches(lo, hi, qmin, qmax, tol=0.6):
    """--query bounds count shadow-only (renderable="0") nodes the export omits, so the
    export must lie inside them and agree exactly on the axes those nodes do not extend."""
    inside = all(l >= a - tol and h <= b + tol for l, h, a, b in zip(lo, hi, qmin, qmax))
    return inside and close(lo[:2], qmin[:2]) and close(hi[:2], qmax[:2])


query = json.loads(run('--query', scene).stdout)
assert query['units'] == 'cm' and query['up'] == 'y'
qmin, qmax = query['bounds']['min'], query['bounds']['max']

listing = run('--list-exporters').stdout
assert 'obj\t.obj' in listing and 'glb\t.glb' in listing, listing

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)

    # OBJ: indices stay in range, bounds match --query, the mtl names every material used
    obj = tmp / 'room.obj'
    run('--export', str(obj), scene)
    verts, faces, groups, mats = [], [], [], set()
    for line in obj.read_text().splitlines():
        t = line.split()
        if not t: continue
        if t[0] == 'v': verts.append([float(x) * 100 for x in t[1:4]])
        elif t[0] == 'f': faces.append([int(x.split('/')[0]) for x in t[1:]])
        elif t[0] == 'o': groups.append(t[1])
        elif t[0] == 'usemtl': mats.add(t[1])
    assert verts and faces and len(groups) > 1
    assert all(len(f) == 3 and all(1 <= i <= len(verts) for i in f) for f in faces)
    lo = [min(v[i] for v in verts) for i in range(3)]
    hi = [max(v[i] for v in verts) for i in range(3)]
    assert matches(lo, hi, qmin, qmax), (lo, hi, qmin, qmax)
    mtl = (tmp / 'room.mtl').read_text()
    assert all(f'newmtl {m}\n' in mtl for m in mats)

    # OBJ options: Z up swaps the axes, merge writes a single object
    objz = tmp / 'z.obj'
    run('--export', str(objz), scene, '--opt', 'up=z', '--opt', 'merge=1')
    text = objz.read_text()
    assert text.count('\no ') == 1
    zv = [[float(x) * 100 for x in l.split()[1:4]] for l in text.splitlines() if l.startswith('v ')]
    assert len(zv) == len(verts)
    assert close([min(v[2] for v in zv), max(v[2] for v in zv)], [qmin[1], qmax[1]])

    # glB: container, JSON chunk, buffer ranges and accessor bounds
    for merge, name in (('0', 'room.glb'), ('1', 'merged.glb')):
        glb = tmp / name
        run('--export', str(glb), scene, '--opt', f'merge={merge}')
        data = glb.read_bytes()
        magic, version, length = struct.unpack('<III', data[:12])
        assert magic == 0x46546C67 and version == 2 and length == len(data) and length % 4 == 0
        jlen, jtype = struct.unpack('<II', data[12:20])
        assert jtype == 0x4E4F534A
        doc = json.loads(data[20:20 + jlen])
        boff = 20 + jlen
        blen, btype = struct.unpack('<II', data[boff:boff + 8])
        assert btype == 0x004E4942 and boff + 8 + blen == len(data)
        assert doc['buffers'][0]['byteLength'] <= blen
        for view in doc['bufferViews']:
            assert view['byteOffset'] + view['byteLength'] <= blen
        nverts = 0
        for prim in (p for m in doc['meshes'] for p in m['primitives']):
            pos = doc['accessors'][prim['attributes']['POSITION']]
            idx = doc['accessors'][prim['indices']]
            assert pos['count'] == doc['accessors'][prim['attributes']['NORMAL']]['count']
            assert idx['count'] % 3 == 0 and prim['material'] < len(doc['materials'])
            view = doc['bufferViews'][idx['bufferView']]
            raw = data[boff + 8 + view['byteOffset']:boff + 8 + view['byteOffset'] + view['byteLength']]
            assert max(struct.unpack(f'<{idx["count"]}I', raw)) < pos['count']
            nverts += pos['count']
        assert nverts == len(verts)
        gmin = [min(a['min'][i] for a in doc['accessors'] if 'min' in a) * 100 for i in range(3)]
        gmax = [max(a['max'][i] for a in doc['accessors'] if 'max' in a) * 100 for i in range(3)]
        assert matches(gmin, gmax, qmin, qmax), (gmin, gmax)

    # failures name the problem and exit 1
    assert 'no exporter handles' in run('--export', str(tmp / 'a.xyz'), scene, expect=1).stderr
    assert 'not valid' in run('--export', str(obj), scene, '--opt', 'up=q', expect=1).stderr
    assert 'no option "colour"' in run('--export', str(obj), scene, '--opt', 'colour=1', expect=1).stderr

    # a broken plugin directory never stops the good exporters
    bad = tmp / 'bad'
    bad.mkdir()
    (bad / 'junk.dylib').write_bytes(b'not a library')
    (bad / 'junk.so').write_bytes(b'not a library')
    broken = dict(env, SCENER_EXPORTER_PATH=f'{bad}{os.pathsep}{exporters}')
    p = subprocess.run([scener, '--list-exporters'], capture_output=True, text=True, env=broken)
    assert p.returncode == 0 and 'obj\t.obj' in p.stdout and 'cannot load' in p.stderr, p.stderr

print('scener export tests passed')
