#!/usr/bin/env python3
"""Exercise the deployed Scener CLI and actual JPEG/PNG outputs."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib


def dimensions(path):
    data = path.read_bytes()
    if data.startswith(b'\x89PNG\r\n\x1a\n'):
        return struct.unpack('>II', data[16:24])
    assert data[:2] == b'\xff\xd8', f'not JPEG: {path}'
    i = 2
    while i < len(data):
        assert data[i] == 255
        marker = data[i + 1]
        length = int.from_bytes(data[i + 2:i + 4], 'big')
        if marker in (0xc0, 0xc1, 0xc2):
            h, w = struct.unpack('>HH', data[i + 5:i + 9])
            return w, h
        i += length + 2
    raise AssertionError('JPEG dimensions missing')


def png_rows(path):
    data = path.read_bytes()
    width, height = struct.unpack('>II', data[16:24])
    assert data[24] == 8 and data[25] in (2, 6)
    channels = 4 if data[25] == 6 else 3
    compressed = bytearray()
    offset = 8
    while offset < len(data):
        length = int.from_bytes(data[offset:offset + 4], 'big')
        if data[offset + 4:offset + 8] == b'IDAT':
            compressed.extend(data[offset + 8:offset + 8 + length])
        offset += length + 12
    decoded = zlib.decompress(compressed)
    stride = width * channels
    previous = bytearray(stride)
    rows = []
    for y in range(height):
        base = y * (stride + 1)
        kind = decoded[base]
        row = bytearray(decoded[base + 1:base + 1 + stride])
        for x in range(stride):
            left = row[x - channels] if x >= channels else 0
            above = previous[x]
            corner = previous[x - channels] if x >= channels else 0
            if kind == 1:
                predictor = left
            elif kind == 2:
                predictor = above
            elif kind == 3:
                predictor = (left + above) // 2
            elif kind == 4:
                estimate = left + above - corner
                distances = [abs(estimate - value) for value in (left, above, corner)]
                predictor = (left, above, corner)[distances.index(min(distances))]
            else:
                assert kind == 0
                predictor = 0
            row[x] = (row[x] + predictor) & 255
        rows.append(row)
        previous = row
    return rows, channels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('scener', type=Path)
    args = parser.parse_args()
    executable = str(args.scener.resolve())
    with tempfile.TemporaryDirectory(prefix='scener-cli-') as directory:
        work = Path(directory)
        scene = work / 'test.blks'
        scene.write_text('''<scene up="z" ambient="0.3 0.3 0.34">
<camera name="front" pos="400 -600 400" look="0 0 90" fov="50"/>
<camera name="side" pos="-400 -500 260" look="0 0 80" fov="50"/>
<box size="600 600 12" pos="0 0 -6" color="0.7 0.7 0.7"/>
<box size="100 80 200" pos="0 0 100" color="0.8 0.3 0.1"/>
<light pos="-200 -200 400" color="1 0.8 0.6" intensity="2" castShadows="1"/>
</scene>''')
        def run(*arguments, success=True):
            result = subprocess.run([executable, *map(str, arguments)], cwd=work, capture_output=True, text=True, timeout=120)
            assert (result.returncode == 0) == success, result.stdout + result.stderr
            return result
        assert 'scener' in run('--version').stdout
        assert '--render' in run('--help').stdout
        assert run('--list-cameras', scene).stdout.splitlines() == ['front', 'side']
        for arguments in [('--bogus',), ('--render',), ('--render', scene, '--format', 'gif'), ('--render', scene, '--size', '0x100'), ('--render', scene, '--supersample', '5'), ('--render', scene, '--camera', 'absent')]:
            run(*arguments, success=False)
        first = subprocess.run([executable, '--render', str(scene), '--size', '160x120', '--format', 'jpg', '--output-dir', 'jpg'], cwd=work, capture_output=True, text=True, timeout=120)
        software = 'Apple Software Renderer' in first.stderr
        extra = ['-no-shadows'] if software else []
        if software:
            assert first.returncode != 0 and 'shadow export rejected' in first.stderr
            assert not (work / 'jpg').exists()
            run('--render', scene, '--size', '160x120', '--format', 'jpg', '--output-dir', 'jpg', *extra)
        else:
            assert first.returncode == 0, first.stderr
        assert sorted(p.name for p in (work / 'jpg').iterdir()) == ['front.jpg', 'side.jpg']
        for path in (work / 'jpg').iterdir():
            assert dimensions(path) == (160, 120)
        run('--render', scene, '--camera', 'side', '--size', '160x120', '--format', 'png', '--output-dir', 'png', *extra)
        assert sorted(p.name for p in (work / 'png').iterdir()) == ['side.png']
        assert dimensions(work / 'png/side.png') == (160, 120)
        rows, channels = png_rows(work / 'png/side.png')
        assert min(rows[0][(160 - 1) * channels:(160 - 1) * channels + 3]) > 0, 'supersampled framebuffer is only partially filled'
        run('--render', scene, '--camera', 'side', '--size', '160x120', '--format', 'png', '--output-dir', 'no-shadows', '-no-shadows')
        no_shadow_rows, _ = png_rows(work / 'no-shadows/side.png')
        if not software:
            assert sum(a != b for lit, unshadowed in zip(rows, no_shadow_rows) for a, b in zip(lit, unshadowed)) > 100, 'default render has no cast shadows'

        run('--layout', scene, '--scale', '0.2', '--format', 'jpg', '--output-dir', 'layout')
        assert all(value > 0 for value in dimensions(work / 'layout/layout.jpg'))
        run(scene, '--screenshot', 'legacy.png', '--cam', 'front', '--size', '160x120', '-wireframe')
        assert dimensions(work / 'legacy.png') == (160, 120)
        print('PASS: CLI errors, cameras, JPEG/PNG encoding, output dimensions, batch, selection, layout, legacy screenshot')


if __name__ == '__main__':
    main()
