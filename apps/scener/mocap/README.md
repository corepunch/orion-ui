# Motion capture clips

`<layer mocap="mocap/cmu/02_01.bvh"/>` retargets a BVH clip onto a CAT rig; see
[character-authoring.md](../docs/character-authoring.md#motion-capture).

Only `cmu/02_01.bvh` (a walk, used by the tests) is kept in the repository. Fetch the other
nineteen study clips with:

```sh
python3 apps/scener/tools/fetch_cmu_mocap.py
```

The clips come from the CMU Graphics Lab Motion Capture Database, converted to BVH by Bruce
Hahne (the 2010 Motionbuilder-friendly release, mirrored at
https://github.com/una-dinosauria/cmu-mocap). Every file starts with a T-pose frame facing +Z.

"The data used in this project was obtained from mocap.cs.cmu.edu. The database was created
with funding from NSF EIA-0196217."
