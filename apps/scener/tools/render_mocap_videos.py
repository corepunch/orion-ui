#!/usr/bin/env python3
"""Render one MP4 per motion capture clip, retargeted onto the CAT biped preset.

Uses scenes/mocap_study.blks as the template (its follow camera tracks the character),
and renders each clip straight to MP4 with `scener --render --frames ... --output CLIP.mp4`.

Usage (from the repository root, after fetch_cmu_mocap.py):
  DYLD_LIBRARY_PATH=$PWD/build/lib python3 apps/scener/tools/render_mocap_videos.py \
      ./build/bin/scener OUTPUT_DIR [--camera Front] [--size 960x540] [--fps 30] [--max-seconds 20]
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
APP = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from fetch_cmu_mocap import CLIPS  # noqa: E402


def clip_seconds(path):
    with open(path) as f:
        head = f.read(1 << 16)
        rest = head[head.index("MOTION"):]
    frames = int(re.search(r"Frames:\s*(\d+)", rest).group(1))
    frame_time = float(re.search(r"Frame Time:\s*([0-9.eE+-]+)", rest).group(1))
    return (frames - 2) * frame_time  # the first frame is the reference T-pose


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("scener")
    parser.add_argument("output")
    parser.add_argument("--camera", default="Front")
    parser.add_argument("--size", default="960x540")
    parser.add_argument("--fps", type=int, default=30)
    parser.add_argument("--max-seconds", type=float, default=20)
    parser.add_argument("--only", nargs="*", help="clip ids to render, e.g. 02_01 09_01")
    args = parser.parse_args()

    os.makedirs(args.output, exist_ok=True)
    template = open(os.path.join(APP, "scenes", "mocap_study.blks")).read()
    scene = os.path.join(APP, "scenes", "_mocap_video.blks")
    try:
        for clip, label in CLIPS:
            if args.only and clip not in args.only:
                continue
            bvh = os.path.join(APP, "mocap", "cmu", clip + ".bvh")
            if not os.path.exists(bvh):
                print(f"skip  {clip}: run fetch_cmu_mocap.py first")
                continue
            seconds = min(clip_seconds(bvh), args.max_seconds)
            text = re.sub(r'mocap="[^"]*"', f'mocap="mocap/cmu/{clip}.bvh"', template)
            text = re.sub(r'duration="[^"]*"', f'duration="{seconds:.3f}"', text)
            with open(scene, "w") as f:
                f.write(text)
            slug = re.sub(r"[^a-z0-9]+", "_", label.lower()).strip("_")
            video = os.path.join(args.output, f"{clip}_{slug}.mp4")
            subprocess.run([args.scener, "--render", scene, "--camera", args.camera, "--frames",
                            f"0:{seconds:.3f}:{args.fps}", "--size", args.size, "--output", video],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            print(f"wrote {video}  ({seconds:.1f} s)")
    finally:
        if os.path.exists(scene):
            os.remove(scene)


if __name__ == "__main__":
    main()
