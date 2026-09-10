#!/usr/bin/env python3
"""Verify the software guard and real GPU shadow pixels on a four-box fixture."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import sys
sys.dont_write_bytecode = True
from test_cli import png_rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('scener', type=Path)
    args = parser.parse_args()
    executable = str(args.scener.resolve())
    fixture = Path(__file__).with_name('shadow_raster.blks')
    with tempfile.TemporaryDirectory(prefix='scener-shadows-') as directory:
        output = Path(directory) / 'shadows'
        command = [executable, '--render', str(fixture), '--size', '320x240', '--supersample', '1', '--format', 'png', '--output-dir', str(output)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=120)
        assert '[scener] OpenGL vendor=' in result.stderr, result.stderr
        if 'Apple Software Renderer' in result.stderr:
            assert result.returncode != 0, 'unsupported software backend silently exported corrupt shadows'
            assert 'shadow export rejected' in result.stderr and 'GPU access' in result.stderr
            assert not output.exists(), 'rejected export created images'
            diagnostic = subprocess.run(command + ['-no-shadows'], capture_output=True, text=True, timeout=120)
            assert diagnostic.returncode == 0, diagnostic.stderr
            assert len(list(output.glob('*.png'))) == 9
            print('PASS: software shadow export rejected before output; diagnostic images remain available')
            return
        assert result.returncode == 0, result.stderr
        assert len(list(output.glob('*.png'))) == 9
        rows, channels = png_rows(output / 'd.png')
        checks = [((130, 150), (195, 166, 131)), ((160, 180), (182, 156, 127)), ((60, 200), (185, 156, 121)), ((140, 120), (87, 96, 103)), ((270, 200), (86, 95, 103))]
        for (x, y), expected in checks:
            actual = rows[y][x * channels:x * channels + 3]
            assert all(abs(a - b) <= 12 for a, b in zip(actual, expected)), f'lit/shadow region {(x, y)}: {list(actual)} expected {expected}'
        for y in range(176, 186):
            for x in range(154, 166):
                blue = rows[y][x * channels + 2]
                assert 112 <= blue <= 142, 'false shadow triangle or stipple on uniformly lit floor'
        print('PASS: nine GPU camera views; expected lit/shadow regions; no false-shadow stipple')


if __name__ == '__main__':
    main()
