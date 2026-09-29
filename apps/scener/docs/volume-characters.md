# Source-based Ecstatica II characters

The character studio now starts from decoded Ecstatica II actors, retaining their original face pieces, body volumes and asymmetry. The rejected synthetic faces, generic torso overlays and Jobs/Gates/storybook recipes have been removed. Both imported game libraries remain intact.

## Build and preview

```sh
python3 tools/characters/build_character.py \
  apps/scener/characters/recipes/villager-full.json --output /tmp/my-villager
python3 tools/characters/studio.py --output /tmp/character-library --port 8765
```

Open `http://127.0.0.1:8765/`. Choose a source, adjust controls, then export. The comparison shows the unchanged source alongside the edited model through the same camera. Reset restores that source's geometry. Preview uses the native Scener renderer, not a browser approximation; `--scener PATH` selects a renderer. Export writes a new timestamped folder, with recipe, prefab, standing study, bend, reach and walking scenes. Existing exports are never overwritten. Recipe JSON is the same input accepted by the CLI. Python's standard library is sufficient; the five included decoded records mean an ISO is not needed to build these characters.

## Source selection

| Base | Original actor | Use |
|---|---|---|
| `joe` | Joe | Athletic body, gloves and boots |
| `villager-full` | w_vil1 | Fuller woman villager; original chest and broad trunk |
| `villager-belly` | w_vil2 | Stocky villager with prominent abdominal volume |
| `villager-slim` | w_vil3 | Slender elder; narrow arms and chest |
| `freegirl` | freegirl | Slender woman, original facial setup |

Source features are offered only when the model actually contains those parts. Hands stay fingerless. Unknown source flags do not suppress anatomy: thighs, knees, calves and feet are preserved.

## Recipe controls

Unspecified fields use defaults. Unknown fields are rejected, including the removed overall `size`, `age`, `body_fat`, `muscularity`, synthetic face and palette controls.

| Field | Range; default | Visible change |
|---|---|---|
| `base` | Source key above; `joe` | Selects the complete original actor |
| `name` | Nonempty text; `Joe` | Export label |
| `height_heads` | 3–9; measured source ratio | Lengthens/shortens the body, keeping head, hand and foot dimensions fixed |
| `frame` | .7–1.6; 1 | Structural breadth/depth: ribcage, pelvis and joint spacing, limb cross-sections, neck, hands and feet. Head dimensions and vertical lengths stay independent. |
| `fullness` | −1–1; 0 | Whole-body fullness: chest/waist/hips, shoulders, arms, thighs, calves, cheeks/neck and smaller changes to hands/feet. The original w_vil2 abdominal primitive emerges on sources without one |
| `muscle` | −1–1; 0 | Changes existing shoulder, arm, pec, back, thigh and calf volumes; no synthetic pecs added |
| `shoulders` | .7–1.3; 1 | Chest/back/pec width and arm attachment spacing change together |
| `head_size` | .75–1.4; 1 | Scales the complete original head setup |
| `youth` | 0–1; 0 | Shorter limbs and torso, narrower shoulders, larger relative head, smaller hands/feet; authored child proportions, not chronological age |
| `posture` | −1–1; 0 | Distributed torso bend: 6° open to 24° hunched; neck/head and arms follow the chest |
| `gloves`, `boots`, `hair` | Boolean; true | Toggle source parts when available |

Fullness and muscle are artistic directions measured from reference actors, not a topology-matched interpolation that turns every face/costume into another actor. Select a different source to obtain its exact default silhouette. Head width/depth change modestly, while cheek width and neck girth follow the measured source differences. Hands and feet change by 12% at the endpoints; calf girth by 22%, thighs by 27%, forearms by 30%, upper arms by 32%. These authored blends preserve the selected source identity. Foot support is recomputed from transformed source ellipsoids. The positive muscle direction extrapolates from the slender-to-athletic difference. Missing corresponding back pieces use an authored reduction. Shoulder caps and pectoral surfaces keep at least 92% of each source axis at minimum muscle: they also cover joints and define the chest envelope, so reducing them like isolated muscle blobs exposes the upper-arm ends and buries the pecs. Default muscle retains exact source geometry. These are not medical body-fat or muscle measurements.

Height is expressed in the original source head unit (hair crown to the bottom of the face, excluding the neck), measured in the rest pose. Joe starts at 5.68 heads; the fuller woman villager at 4.61 and the slender villager at 6.32. Changing height preserves the complete head geometry; it changes body segment lengths and joint locations. Head size and youth remain separate artistic controls, so changing those can change the resulting head-count ratio. Posture can also reduce standing height without changing the height setting.

Frame is independent of height, muscle and fullness. A tall slender character uses high height and a narrow frame; a Hulk-like silhouette starts with high height and a broad frame, then adds muscle. Shoulder span remains a local upper-body adjustment. No automatic widening is forced by height: the decoded taller slender villager has narrower shoulder joints than the shorter fuller villager. Frame edits move both arm and leg attachments and resize their associated volumes, so reducing muscle does not collapse the broad skeleton.

Geometry and skeleton are changed together. Each source volume is retained under its calibrated CAT owner; the same affine transform moves its centre and changes its axes. Shoulder width therefore changes the torso envelope and arm roots together. Limb shortening moves elbows/knees and hands/feet as well as resizing the volume. Torso bending keeps the pelvis and leg chains at rest. Presentation targets are calculated from each model's own arm lengths.

## Evidence and limits

See [source measurements](../../../tools/characters/references/README.md). At defaults, native mesh tests compare all five generated models against the original calibrated source within 0.2 mm. A volume regression checks that fullness changes every ellipsoid part on every base, including face and extremities. Endpoint tests evaluate every control on every source, check both knees, and check native reach/bend/walk poses. GPU tests compare actual rendered pixels at both ends of each control with a fixed camera; every control must produce a visible difference.

The original game palette lookup and rasterizer are not emulated, and these are decoded ellipsoid parts rather than a conventional skinned surface. CAT controls animate rigid volume groups. Source provenance remains attached; this workflow does not make the imported shapes independently authored. The child names in the archive have missing indexed records, so the youth control does not claim a recovered child face or model. Source facial identities remain intact. Extreme combinations still need visual review, especially costume overlap and hand/foot contact; this is a base-character tool, not a finished likeness generator.

### Missing shoulder connections

The slender villager has no decoded shoulder parts. Its upper-arm roots lie outside the narrow chest ellipsoid, leaving visible gaps during motion. Preview conversion now adds two explicitly reconstructed connecting ellipsoids, sized from arm girth and the chest surface, attached to the chest. They follow torso motion while covering the arm pivots. Original source parts and raw JSON remain unchanged; `preview_repairs` records the additions. This is an authored conversion repair, not recovered game geometry. Calibrated CAT rigs and the studio use the same repair.
