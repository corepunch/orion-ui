# Native render review

- `Cast.png`: original Joe, fuller villager, round-bellied villager and slender villager. Render apps/scener/scenes/volume_character_lineup.blks, camera Cast.
- `Fullness.png`: the same Joe at fullness −1, 0 and +1. Height and muscle settings stay at their defaults. Render apps/scener/scenes/volume_character_morphs.blks, camera Fullness.
- `Height.png`: Joe at 4 and 8 source-head units. All head, hand and foot geometry is identical between the two variants. Render the same morph scene, camera Height.

2026-09-29 checks: 45 native Scener tests, 24 importer tests and 9 builder tests pass. Builder coverage includes every slider endpoint on all five sources, per-part fullness changes, unchanged head/hand/foot geometry under height edits, and native rendered-pixel changes for every slider. Browser review verified source selection, shoulder endpoints, rendered updates, reset and recipe export. Original comparison remains unchanged when editing.

`Frames.png`: 6 heads / frame 1.0, 8 heads / frame .85, 8 heads / frame 1.5. Every model has muscle −1 and fullness 0. Render apps/scener/scenes/volume_character_frames.blks, camera Frames. Frame regression checks horizontal arm/leg attachment changes with unchanged attachment height, unchanged head/cheeks, and breadth/depth changes in trunk, limbs and extremities. After adding frame, all 10 builder tests and 45 native Scener tests pass.

Shoulder/chest fix: the frame comparison still uses muscle −1; shoulder caps and pectorals now retain joint coverage and visible chest definition. The earlier .62 fallback for missing reference parts exposed skin-coloured arm tips and buried the chest volumes. New geometry regressions require the upper-arm proximal pole to remain inside its shoulder cap and the pectoral front surface to remain outside the central torso.

[Complete screenshot history](history/README.md) contains 70 distinct available references, issue screenshots and historical native renders, including rejected intermediate work. Four early Desktop references are listed as unavailable in its manifest. [Ecstatica II animations](../../imports/animation-review/README.md) cover four different actors.

The slender villager in Cast and the animation sample now includes documented reconstructed shoulder connections; original decoded records are unchanged.
