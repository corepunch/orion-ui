#!/usr/bin/env python3
"""Render the editable Ecstatica II IK/FK comparison and annotated MP4."""
import argparse
import json
import math
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
SCENE = ROOT / 'apps/scener/scenes/infographics/ik_fk.blks'
FPS, SECONDS = 24, 6
TARGET = (178.7, -15.0, 118.5)
JOINTS = ('left_upperarm', 'left_forearm', 'left_palm', 'left_foot', 'right_foot')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scener', type=Path, default=ROOT / 'build/bin/scener')
    parser.add_argument('--output', type=Path, default=ROOT / 'video/ik-fk')
    parser.add_argument('--preview', action='store_true', help='Render only the 1.5-second poster')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, DYLD_LIBRARY_PATH=str(ROOT / 'build/lib'))
    samples = []
    baseline = None
    times = [0, 1.5] if args.preview else [i / FPS for i in range(SECONDS * FPS + 1)]
    for time in times:
        result = subprocess.run([str(args.scener), '--list-joints', str(SCENE), '--camera',
                                 'Comparison', '--time', str(time)], env=env, text=True,
                                capture_output=True, check=True)
        if result.stderr.strip():
            raise RuntimeError(result.stderr)
        joints = {name: {} for name in ('FK', 'IK')}
        for line in result.stdout.splitlines():
            values = line.split()
            if len(values) == 5 and values[0] in joints and values[1] in JOINTS:
                joints[values[0]][values[1]] = [float(x) for x in values[2:]]
        if any(set(joints[name]) != set(JOINTS) for name in joints):
            raise RuntimeError(f'Missing joints at {time}: {joints}')
        if baseline is None:
            baseline = joints
        error = math.dist(joints['IK']['left_palm'], TARGET)
        if error > 0.18:
            raise RuntimeError(f'IK hand missed target at {time}: {error} cm')
        for name in joints:
            for foot in ('left_foot', 'right_foot'):
                if math.dist(joints[name][foot], baseline[name][foot]) > 0.18:
                    raise RuntimeError(f'{name} foot moved at {time}')
            for a, b, length in (('left_upperarm', 'left_forearm', 22.356),
                                 ('left_forearm', 'left_palm', 22.149)):
                if abs(math.dist(joints[name][a], joints[name][b]) - length) > 0.18:
                    raise RuntimeError(f'{name} arm stretched at {time}')
        samples.append(dict(time=time, joints=joints,
                            drift=math.dist(joints['FK']['left_palm'], baseline['FK']['left_palm']),
                            error=error))
    if max(x['drift'] for x in samples) < 10:
        raise RuntimeError('The FK comparison does not show enough hand displacement')
    metadata = dict(fps=FPS, seconds=SECONDS, target=TARGET, samples=samples,
                    preview=args.preview, scene=str(SCENE.relative_to(ROOT)))
    (out / 'measurements.json').write_text(json.dumps(metadata, indent=2) + '\n')
    raw = out / ('preview-raw' if args.preview else 'raw')
    command = [str(args.scener), '--render', str(SCENE), '--camera', 'Comparison',
               '--size', '1600x640', '--format', 'png', '--output-dir', str(raw)]
    command += ['--time', '1.5'] if args.preview else ['--frames', f'0:{SECONDS}:{FPS}']
    print(f'Rendering {len(samples) if not args.preview else 1} frames', flush=True)
    with (out / 'render.log').open('w') as log:
        subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    log = (out / 'render.log').read_text()
    if any(word in log.lower() for word in ('warning:', 'out of reach', 'unsupported xml', 'error:')):
        raise RuntimeError(f'Render diagnostics require review: {out / "render.log"}')
    compiler = ['swiftc', '-O', '-module-cache-path', str(out / '.swift-cache')]
    compositor = out / '.compose'
    subprocess.run(compiler + [str(ROOT / 'apps/scener/tools/compose_ik_fk_infographic.swift'),
                               '-o', str(compositor)], check=True)
    subprocess.run([str(compositor), str(out)], check=True)
    if not args.preview:
        encoder = out / '.encode'
        subprocess.run(compiler + [str(ROOT / 'apps/scener/tools/frames_to_mp4.swift'),
                                   '-o', str(encoder)], check=True)
        subprocess.run([str(encoder), str(out / 'frames'), 'Comparison', str(FPS),
                        str(out / 'ik-vs-fk.mp4')], check=True)
        shutil.copy2(out / 'frames/Comparison_0036.png', out / 'ik-vs-fk.png')
    print(f'Checked {len(samples)} poses: IK max error {max(x["error"] for x in samples):.2f} cm; '
          f'FK max displacement {max(x["drift"] for x in samples):.1f} cm', flush=True)


if __name__ == '__main__':
    main()
