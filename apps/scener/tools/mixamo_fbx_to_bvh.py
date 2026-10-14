#!/usr/bin/env python3
"""Convert Mixamo FBX animations to BVH clips that Scener retargets with profile="mixamo".

Runs inside Blender, headless:

    blender -b -P apps/scener/tools/mixamo_fbx_to_bvh.py -- walk.fbx apps/scener/mocap/mixamo/walk.bvh
    blender -b -P apps/scener/tools/mixamo_fbx_to_bvh.py -- --output-dir apps/scener/mocap/mixamo clips/*.fbx

Each FBX is imported with Blender's FBX importer, the armature's object transform is applied so
armature space equals world space (Blender Z up, metres), and the action's frame range is exported
with Blender's BVH exporter at centimetre scale. The result keeps Mixamo's joint names
(mixamorig:Hips, mixamorig:LeftUpLeg, ...), its rest pose as the OFFSET hierarchy, and every
frame of the clip. Scener's built-in "mixamo" capture profile expects exactly this layout.
"""
import argparse
import os
import sys

import bpy

DEFAULT_FPS = 30
BVH_SCALE = 100.0  # metres to centimetres


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(prog="mixamo_fbx_to_bvh.py", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("inputs", nargs="+", help="FBX file(s); with one input the second positional may be the output .bvh")
    parser.add_argument("--output-dir", help="write INPUT.bvh for every input into this directory")
    parser.add_argument("--fps", type=float, default=None, help="frame rate to export (default: the file's, else %d)" % DEFAULT_FPS)
    parser.add_argument("--scale", type=float, default=BVH_SCALE, help="BVH units per Blender metre (default %g, centimetres)" % BVH_SCALE)
    args = parser.parse_args(argv)
    jobs = []
    if args.output_dir:
        os.makedirs(args.output_dir, exist_ok=True)
        for src in args.inputs:
            jobs.append((src, os.path.join(args.output_dir, os.path.splitext(os.path.basename(src))[0] + ".bvh")))
    elif len(args.inputs) == 2 and args.inputs[1].lower().endswith(".bvh"):
        jobs.append((args.inputs[0], args.inputs[1]))
    elif len(args.inputs) == 1:
        jobs.append((args.inputs[0], os.path.splitext(args.inputs[0])[0] + ".bvh"))
    else:
        parser.error("pass INPUT.fbx OUTPUT.bvh, or --output-dir DIR with one or more FBX files")
    return args, jobs


def animated_armature():
    armatures = [o for o in bpy.data.objects if o.type == "ARMATURE"]
    if not armatures:
        raise SystemExit("no armature in the FBX")
    with_action = [o for o in armatures if o.animation_data and o.animation_data.action]
    return (with_action or armatures)[0]


def action_fcurves(action):
    """Every F-curve of an action, through the layered API (Blender 4.4+) or the legacy one."""
    if hasattr(action, "layers"):
        return [fc for layer in action.layers for strip in layer.strips for bag in strip.channelbags for fc in bag.fcurves]
    return list(action.fcurves)


def scale_pose_locations(action, factor):
    """Pose-bone location keys are in bone-local units; applying the object scale does not touch them."""
    scaled = 0
    for fc in action_fcurves(action):
        if not fc.data_path.endswith(".location") or not fc.data_path.startswith("pose.bones"):
            continue
        for key in fc.keyframe_points:
            key.co.y *= factor
            key.handle_left.y *= factor
            key.handle_right.y *= factor
        scaled += 1
    return scaled


def make_root_position_relative(path):
    """Blender writes the root's absolute position; BVH readers add the channels to OFFSET, so store the delta."""
    with open(path) as f:
        lines = f.read().split("\n")
    offset = None
    motion = None
    for i, line in enumerate(lines):
        if offset is None and line.strip().startswith("OFFSET"):
            offset = [float(v) for v in line.split()[1:4]]
        if line.startswith("Frame Time:"):
            motion = i + 1
            break
    if offset is None or motion is None:
        raise SystemExit("%s: unexpected BVH layout" % path)
    for i in range(motion, len(lines)):
        values = lines[i].split()
        if len(values) < 3:
            continue
        values[:3] = ["%.6f" % (float(v) - o) for v, o in zip(values[:3], offset)]
        lines[i] = " ".join(values)
    with open(path, "w") as f:
        f.write("\n".join(lines))


def convert(src, dst, fps, scale):
    os.makedirs(os.path.dirname(os.path.abspath(dst)), exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=src, use_anim=True, ignore_leaf_bones=False, automatic_bone_orientation=False)
    arm = animated_armature()
    bpy.ops.object.select_all(action="DESELECT")
    arm.select_set(True)
    bpy.context.view_layer.objects.active = arm
    if arm.parent:
        bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
    action = arm.animation_data.action if arm.animation_data else None
    if action is None:
        raise SystemExit("%s: the armature carries no animation" % src)
    object_scale = arm.matrix_world.to_scale()
    if max(object_scale) - min(object_scale) > 1e-6:
        raise SystemExit("%s: non-uniform armature scale %s" % (src, tuple(object_scale)))
    scale_pose_locations(action, object_scale.x)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    scene = bpy.context.scene
    start, end = (int(round(v)) for v in action.frame_range)
    scene.render.fps = int(round(fps)) if fps else scene.render.fps or DEFAULT_FPS
    scene.render.fps_base = 1.0
    scene.frame_start, scene.frame_end = start, end
    bpy.ops.export_anim.bvh(filepath=dst, frame_start=start, frame_end=end, global_scale=scale, rotate_mode="NATIVE", root_transform_only=True)
    make_root_position_relative(dst)
    root = [b for b in arm.data.bones if not b.parent]
    print("converted %s -> %s: root %s, %d joints, frames %d..%d at %g fps, %g units per metre" % (
        src, dst, root[0].name if root else "?", len(arm.data.bones), start, end, scene.render.fps, scale))


def main():
    args, jobs = parse_args()
    for src, dst in jobs:
        convert(src, dst, args.fps, args.scale)


if __name__ == "__main__":
    main()
