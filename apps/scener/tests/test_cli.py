#!/usr/bin/env python3
"""Exercise the deployed Scener CLI and actual JPEG/PNG outputs."""
import argparse
import shutil
from pathlib import Path
import struct
import subprocess
import sys
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


def write_solid_png(path, rgba):
    def chunk(kind, payload):
        return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))
    data = b'\x89PNG\r\n\x1a\n'
    data += chunk(b'IHDR', struct.pack('>IIBBBBB', 1, 1, 8, 6, 0, 0, 0))
    data += chunk(b'IDAT', zlib.compress(b'\0' + bytes(rgba)))
    data += chunk(b'IEND', b'')
    path.write_bytes(data)


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

        (work / 'prefabs/devices').mkdir(parents=True)
        (work / 'prefabs/devices/swatch.blk').write_text('<prefab><screen size="100 100 1" image="red.png" color="1 1 1"/></prefab>')
        write_solid_png(work / 'red.png', (255, 0, 0, 255))
        write_solid_png(work / 'blue.png', (0, 0, 255, 255))
        textured = work / 'textured.blks'
        textured.write_text('''<scene background="black">
<camera name="front" pos="0 0 300" look="0 0 0" fov="60"/>
<prefab source="devices/swatch" pos="-80 0 0"/>
<prefab source="devices/swatch" pos="80 0 0" screenImage="blue.png"/>
</scene>''')
        run('--render', textured, '--camera', 'front', '--size', '256x128', '--format', 'png', '--output-dir', 'textures', '-no-shadows')
        pixels, channels = png_rows(work / 'textures/front.png')
        left = pixels[64][98 * channels:98 * channels + 3]
        right = pixels[64][158 * channels:158 * channels + 3]
        assert left[0] > 180 and left[1] < 50 and left[2] < 50, f'left screen lost red image: {left}'
        assert right[2] > 180 and right[0] < 50 and right[1] < 50, f'instance screenImage override failed: {right}'
        missing = work / 'missing.blks'
        missing.write_text('<scene><screen image="absent.png"/></scene>')
        assert 'cannot load screen image' in run('--list-cameras', missing, success=False).stderr

        reel = work / 'test.reel'
        reel.write_text('''<reel width="200" height="120" fps="10" duration="0.5" background="#000000">
<style name="label" size="18" color="#FFFFFF"/>
<scene src="test.blks" camera="front" x="100" y="0" width="100" height="120"/>
<rect x="10" y="10" width="40" height="30" radius="4" color="#FF0000"/>
<rect x="10" y="60" width="80 * t" height="20" color="#00FF00"/>
<text style="label" x="10" y="110">t={t:%.1f}</text>
<check name="time stays in range" value="t" min="0" max="0.5"/>
</reel>''')
        for arguments in [('--reel', reel), ('--reel', reel, '--check', '--output', 'x.png'), ('--check', scene)]:
            run(*arguments, success=False)
        assert 'check time stays in range' in run('--reel', reel, '--check').stdout
        run('--reel', reel, '--output', 'reel.png', '--time', '0.4', *extra)
        assert dimensions(work / 'reel.png') == (200, 120)
        pixels, channels = png_rows(work / 'reel.png')
        red, green, scene_pixel = pixels[25][30 * channels:30 * channels + 3], pixels[70][40 * channels:40 * channels + 3], pixels[60][150 * channels:150 * channels + 3]
        assert red[0] > 240 and red[1] < 20 and red[2] < 20, f'reel rect lost its colour: {red}'
        assert green[1] > 240 and green[0] < 20, f'time expression did not size the bar: {green}'
        assert max(scene_pixel) > 30, f'scene layer did not render: {scene_pixel}'
        run('--reel', reel, '--output-dir', 'reel-frames', *extra)
        assert sorted(p.name for p in (work / 'reel-frames').iterdir()) == [f'frame_{i:04d}.png' for i in range(5)]
        encodes = sys.platform == 'darwin' or shutil.which('ffmpeg') is not None
        video = run('--reel', reel, '--output', 'reel.mp4', *extra, success=encodes)
        if encodes:
            data = (work / 'reel.mp4').read_bytes()
            assert data[4:8] == b'ftyp' and b'avcC' in data and b'moov' in data, 'reel MP4 is incomplete'
            run('--render', scene, '--camera', 'front', '--frames', '0:0.5:10', '--size', '160x120', '--output', 'render.mp4', *extra)
            assert b'avcC' in (work / 'render.mp4').read_bytes()
        else:
            assert 'ffmpeg' in video.stderr
        failing = work / 'failing.reel'
        failing.write_text('<reel fps="10" duration="1"><check name="early" value="t" max="0.5"/></reel>')
        assert 'check early failed' in run('--reel', failing, '--check', success=False).stderr
        assert not (work / 'never.png').exists() and run('--reel', failing, '--output', 'never.png', success=False)
        print('PASS: CLI errors, cameras, JPEG/PNG encoding, output dimensions, batch, selection, layout, legacy screenshot, reels')


if __name__ == '__main__':
    main()
