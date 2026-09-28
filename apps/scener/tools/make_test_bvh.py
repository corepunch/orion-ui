#!/usr/bin/env python3
"""Write a synthetic jumping-jacks BVH with a CMU/Mixamo-style humanoid hierarchy.

It exercises Scener's BVH loader and humanoid retargeting without shipping third
party capture data. Real captures (CMU cgspeed BVH, Mixamo "FBX to BVH" exports)
use the same joint names and load the same way.

    python3 apps/scener/tools/make_test_bvh.py apps/scener/mocap/jumping_jacks.bvh
"""
import math
import sys

FPS = 30
CYCLE = 1.2
CYCLES = 3
HIP_HEIGHT = 95.0

# name, parent, offset (cm, Y up, facing +Z, left is +X), end site or None
JOINTS = [
    ("Hips", None, (0, HIP_HEIGHT, 0), None),
    ("LeftUpLeg", "Hips", (9, -4, 0), None),
    ("LeftLeg", "LeftUpLeg", (0, -44, 0), None),
    ("LeftFoot", "LeftLeg", (0, -42, 0), None),
    ("LeftToeBase", "LeftFoot", (0, -6, 13), (0, 0, 5)),
    ("RightUpLeg", "Hips", (-9, -4, 0), None),
    ("RightLeg", "RightUpLeg", (0, -44, 0), None),
    ("RightFoot", "RightLeg", (0, -42, 0), None),
    ("RightToeBase", "RightFoot", (0, -6, 13), (0, 0, 5)),
    ("Spine", "Hips", (0, 10, 0), None),
    ("Spine1", "Spine", (0, 22, 0), None),
    ("Neck", "Spine1", (0, 22, 0), None),
    ("Head", "Neck", (0, 10, 0), (0, 18, 0)),
    ("LeftShoulder", "Spine1", (4, 18, 0), None),
    ("LeftArm", "LeftShoulder", (14, 0, 0), None),
    ("LeftForeArm", "LeftArm", (29, 0, 0), None),
    ("LeftHand", "LeftForeArm", (26, 0, 0), (10, 0, 0)),
    ("RightShoulder", "Spine1", (-4, 18, 0), None),
    ("RightArm", "RightShoulder", (-14, 0, 0), None),
    ("RightForeArm", "RightArm", (-29, 0, 0), None),
    ("RightHand", "RightForeArm", (-26, 0, 0), (-10, 0, 0)),
]
ROTATION_ORDER = ("Zrotation", "Xrotation", "Yrotation")


def children(name):
    return [j for j in JOINTS if j[1] == name]


def write_joint(out, joint, depth):
    name, parent, offset, end = joint
    pad = "  " * depth
    out.append(f"{pad}{'ROOT' if parent is None else 'JOINT'} {name}")
    out.append(f"{pad}{{")
    out.append(f"{pad}  OFFSET {offset[0]:.2f} {offset[1]:.2f} {offset[2]:.2f}")
    channels = (("Xposition", "Yposition", "Zposition") if parent is None else ()) + ROTATION_ORDER
    out.append(f"{pad}  CHANNELS {len(channels)} {' '.join(channels)}")
    for child in children(name):
        write_joint(out, child, depth + 1)
    if end:
        out.append(f"{pad}  End Site")
        out.append(f"{pad}  {{")
        out.append(f"{pad}    OFFSET {end[0]:.2f} {end[1]:.2f} {end[2]:.2f}")
        out.append(f"{pad}  }}")
    out.append(f"{pad}}}")


def pose(t):
    """Per-joint (Z, X, Y) degrees plus hip position for time t in seconds."""
    p = (t / CYCLE) % 1.0
    spread = 0.5 - 0.5 * math.cos(2 * math.pi * p)
    air = abs(math.sin(2 * math.pi * p))
    crouch = 1.0 - air
    rot = {name: [0.0, 0.0, 0.0] for name, *_ in JOINTS}
    rot["LeftArm"][0] = -80 + 165 * spread
    rot["RightArm"][0] = 80 - 165 * spread
    rot["LeftForeArm"][0] = 10 * spread
    rot["RightForeArm"][0] = -10 * spread
    rot["LeftUpLeg"][0] = 14 * spread
    rot["RightUpLeg"][0] = -14 * spread
    for side in ("Left", "Right"):
        rot[f"{side}UpLeg"][1] = -22 * crouch
        rot[f"{side}Leg"][1] = 40 * crouch
        rot[f"{side}Foot"][1] = -18 * crouch
    rot["Spine"][1] = 6 * crouch
    rot["Head"][1] = -8 * crouch
    hips = (0.0, HIP_HEIGHT - 9 * crouch + 10 * air, 0.0)
    return hips, rot


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "jumping_jacks.bvh"
    out = ["HIERARCHY"]
    write_joint(out, JOINTS[0], 0)
    frames = int(FPS * CYCLE * CYCLES)
    out += ["MOTION", f"Frames: {frames}", f"Frame Time: {1.0 / FPS:.6f}"]

    def emit(name, hips, rot, values):
        if name == "Hips":
            values += list(hips)
        values += rot[name]
        for child in children(name):
            emit(child[0], hips, rot, values)

    for f in range(frames):
        hips, rot = pose(f / FPS)
        values = []
        emit("Hips", hips, rot, values)
        out.append(" ".join(f"{v:.4f}" for v in values))
    with open(path, "w") as fh:
        fh.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
