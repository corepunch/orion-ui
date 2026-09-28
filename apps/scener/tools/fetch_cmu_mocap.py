#!/usr/bin/env python3
"""Download the motion capture clips used by Scener's mocap study scenes.

The clips come from the CMU Graphics Lab Motion Capture Database
(http://mocap.cs.cmu.edu), in Bruce Hahne's Motionbuilder-friendly BVH
conversion mirrored at https://github.com/una-dinosauria/cmu-mocap. CMU asks
that users credit the source: "The data used in this project was obtained from
mocap.cs.cmu.edu. The database was created with funding from NSF EIA-0196217."

Usage: python3 apps/scener/tools/fetch_cmu_mocap.py [DEST]
DEST defaults to apps/scener/mocap/cmu next to this script's app folder.
"""
import os
import sys
import urllib.request

BASE = "https://raw.githubusercontent.com/una-dinosauria/cmu-mocap/master/data"

CLIPS = [
    ("02_01", "walk"),
    ("09_01", "run"),
    ("02_04", "jump, balance"),
    ("02_05", "punch, strike"),
    ("13_17", "boxing"),
    ("13_27", "direct traffic, wave, point"),
    ("13_29", "jumping jacks, side twists, bend over, squats"),
    ("13_01", "sit on high stool, stand up"),
    ("13_04", "sit on stepstool, chin in hand"),
    ("13_14", "laugh"),
    ("12_04", "tai chi"),
    ("02_07", "swordplay"),
    ("49_06", "cartwheel"),
    ("05_02", "dance, expressive arms, pirouette"),
    ("18_15", "chicken dance"),
    ("20_08", "zombie march"),
    ("20_06", "soldiers march"),
    ("10_01", "soccer, kick ball"),
    ("42_01", "stretch, rotate head, shoulders, arms, legs"),
    ("49_02", "jump up and down, hop on one foot"),
]


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    dest = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "mocap", "cmu")
    os.makedirs(dest, exist_ok=True)
    for clip, label in CLIPS:
        subject = clip.split("_")[0].zfill(3)
        path = os.path.join(dest, clip + ".bvh")
        if os.path.exists(path):
            print(f"have  {clip}  {label}")
            continue
        url = f"{BASE}/{subject}/{clip}.bvh"
        with urllib.request.urlopen(url) as response, open(path, "wb") as out:
            out.write(response.read())
        print(f"got   {clip}  {label}  {os.path.getsize(path)} bytes")


if __name__ == "__main__":
    main()
