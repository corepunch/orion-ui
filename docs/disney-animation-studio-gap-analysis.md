---
layout: default
title: Disney Animation Studio tools, menus, and Pencil Test gaps
nav_exclude: true
---

# Disney Animation Studio: tools, menus, and the Pencil Test gap

**Audit date:** 2026-10-15. **Target:** the supplied four-disk DOS release, specifically `IP.EXE` (Ink & Paint G.BC30) and `PT.EXE` (Pencil Test / Exposure Sheet G.C128). **Comparison:** the current Orion `apps/imageeditor` Pencil Test build, including its existing uncommitted changes. This is an inventory and gap analysis; it does not change application behavior.

## Implementation update — 2026-10-15

Pencil Test now implements FPS and per-exposure duration controls, a reusable cel
bank, Make Unique, reference-aware cel deletion, and a basic exposure-sheet editor
with row notes and reordering. Shared identities, independent durations, notes and
unused drawings survive history and PTF save/load. See the
[implementation contract](../apps/imageeditor/ARCHITECTURE.md#timing-reusable-cels-and-exposure-sheet).
The catalogue and scores below remain the **pre-implementation audit snapshot**;
block editing, sheet search/ranges, audio, bitmap brushes and palette workflows
remain outstanding.

## Assessment

The gap is **moderate in basic drawing and large in animation authoring and specialist cel painting**. Our editor already has a usable stroke/fill/selection engine, layered documents, independently compressed drawing planes, onion skin and structural undo/redo. Disney adds a separate bank of reusable drawing cels, an editable exposure sheet, timing/range operations, per-frame notes/sound, a bitmap-brush workflow and extensive palette/background operations.

This is several coherent subsystems of work. A few additional toolbar buttons would not provide parity. The largest model change is separating a drawing's identity from the times it is displayed. Existing per-frame delays can represent a hold during playback, but recurring references to one editable drawing cannot be represented by the current array of independently owned frame payloads.

| Work area | Size of gap | What exists / what is missing |
|---|---|---|
| Freehand drawing, erase, line, rectangle, fill | Small–moderate | Core functions exist; discrete-point mode, exact legacy fill variants and some gestures differ. |
| Circles, axis-defined ellipses, multi-stage curve | Moderate / partly unverified | We have ordinary shapes; original axis/third-point interactions and the curve tool require additional work or verification. |
| Selection and custom bitmap brushes | Large | Selection, move and clipboard exist; reusable captured brushes, hotspots, load/save and transforms do not. |
| Text and fonts | Moderate | Text insertion exists; user font selection and bold/italic/underline do not. |
| Drawing cels versus exposures | Large, foundational | No separate cel bank, stable cel references, reuse/make-unique operations or reference-aware deletion. |
| Exposure sheet and retiming | Large | No sheet, row/block editing, sparse editing, sheet search or range-driven exports. |
| FPS, durations and playback range | Moderate–large | Stored delays and basic playback exist; timing controls are unfinished. |
| Sound and frame comments | Large | No comparable event/annotation model or synchronized audio path. |
| Palette editing, propagation and cycling | Large | Existing indexed swatches/import remapping are not an editable per-cel palette/cycle workflow. |
| Background/cel compositing and Frisket | Moderate–large | Shared Background and selections cover part of the need; color-selected masks and batch background application differ. |
| Onion skin and history | Small | Our adjustable before/after textures and document undo/redo are a strong foundation. |
| Legacy format interoperability | Large if required | CFAST, XPO, ILBM and Amiga ANIM are not application import/export formats. PTF/FLIC and modern exports cover different interoperability. |
| Adapter selection, manual EMS/font freeing, secured output | No product-parity requirement | Historical platform/storage concerns should not inflate the modern feature backlog. |

## Evidence and limits

The menu inventory below is recovered from **actual executable UI structures**, including each menu's child-menu pointers, text pointers and byte command IDs. It is not an alphabetical list of strings. The roots are IP `DS:0A76`, PT `DS:0F87`, and the Exposure Sheet inside PT `DS:4632`.

- **48** IP leaf commands, **71** PT leaf commands and **21** exposure-sheet leaf commands: **140 distinct menu command IDs** in these three command spaces.
- Those include **7 IP tool slots and 13 PT tool slots**. Their menus primarily store shortcut letters beside bitmap icons. Functional names below are reconstructed from their selectors and drawing callbacks; explicitly marked inferences are not original printed labels.
- Parent submenu entries are included in paths but not counted as additional commands. Dialog controls and keyboard-only actions are inventoried separately. `PTTM.EXE`, the DAS launcher, FLICK and CONVERT are outside the full menu audit.
- **D** means inspected disassembly; **T** means UI/menu/help text or structures; **F** means decoded sample-file evidence. UI presence does not prove an operation's complete runtime semantics. No original executable was run for this audit.
- The source code review follows actual handlers and build conditions. Some functions in `tools/tool_stubs.c` are stubs while `windows/win_canvas.c` implements the corresponding behavior directly; they are not counted as missing merely because of the stubs.
- A search for an original DOS manual did not supply one. Amiga review descriptions were not used to assert DOS feature parity or fill in unverified icon names.

### Coverage labels

| Label | Meaning |
|---|---|
| **C — covered** | A user-facing equivalent for the basic operation exists. This is not a claim of identical pixels, format or gesture. |
| **P — partial** | Related code/functionality exists, but scope, UI, semantics or integration differ. |
| **M — missing** | No matching operation/workflow found in the audited Pencil Test path. |
| **L — legacy/integrated** | Historical platform control or a module boundary that our integrated application does not need to reproduce. |
| **U — unresolved** | Original semantics are not sufficiently reconstructed to score parity. |

### Command-inventory totals

| Command space | C | P | M | L | U | Total |
|---|---:|---:|---:|---:|---:|---:|
| Ink & Paint | 10 | 12 | 14 | 9 | 3 | 48 |
| Pencil Test | 15 | 20 | 32 | 3 | 1 | 71 |
| Exposure Sheet | 1 | 1 | 18 | 1 | 0 | 21 |
| **Total** | 26 | 33 | 64 | 13 | 4 | **140** |

These are **unweighted command rows, not a completion percentage**. Import/open/append/save expose the same missing codecs repeatedly, and one missing exposure-sheet model accounts for many rows. Conversely, a single palette or sound command can imply a substantial subsystem. Use the work-area assessment and implementation groups rather than treating all 140 rows as equal-sized tasks.

## All recovered drawing-tool slots

The selectors explicitly mark indices `0..6` in IP and `0..12` in PT. Uppercase and lowercase shortcuts are distinct. Rectangle/fill path variants and the curve naming retain uncertainty where callback inspection is incomplete.

### Ink & Paint — 7 tools

| ID | Slot | Functional tool | Key | Selector / callback, load-relative CS:IP | Gap | Comparison / evidence boundary |
|---|---:|---|---|---|---|---|
| IP-34 | 0 | Connected freehand drawing | `d` | `041A:111D / 24C3 / 24D5` | **C** | Continuous raster path; our pencil/brush/eraser cover the basic function. |
| IP-35 | 1 | Straight line | `v` | `041A:117C / 26DD / 26FF` | **C** | Line tool exists. |
| IP-36 | 2 | Rectangle area painting, path A | `r` | `041A:11DD / 248E / 2582` | **P** | Filled rectangles exist; the legacy solid/dither path distinction is not fully verified. |
| IP-37 | 3 | Rectangle area painting, path B | `R` | `041A:1211 / 2582` | **U** | Shares rectangle scan conversion with slot 2 and chooses a different span callback; likely dither variant, not yet proven for this DOS build. |
| IP-38 | 4 | Flood fill, path A | `f` | `041A:1245 / 2680 / 2310` | **P** | Our fill has gap closing; no complete equivalence established for the original color/boundary rules. |
| IP-39 | 5 | Flood fill, path B | `F` | `041A:12AB / 20A4` | **U** | Different span callback selected by DS:1D05; likely dither fill, exact pattern needs confirmation. |
| IP-40 | 6 | Boundary/color-stop fill, inferred | `—` | `041A:1311 / 2680` | **P** | Third fill selector uses a different stop value; our target-color flood fill is not a full substitute for an arbitrary boundary-color mode. |

### Pencil Test — 13 tools

| ID | Slot | Functional tool | Key | Selector / callback, load-relative CS:IP | Gap | Comparison / evidence boundary |
|---|---:|---|---|---|---|---|
| PT-49 | 0 | Connected freehand drawing | `d` | `040D:0EAD / 3242 / 3267` | **C** | Our pencil/brush interpolate strokes and add soft coverage, grain and fading. |
| PT-50 | 1 | Spot/discrete drawing, functional name | `s` | `040D:0F18 / 367C / 36C5` | **P** | Plots the current point/stamp without the connected-path routine; no separately exposed equivalent mode located. |
| PT-51 | 2 | Straight line | `v` | `040D:0F85 / 35A3` | **C** | Line tool exists through the shared Tools menu/shape controls. |
| PT-52 | 3 | Multi-stage curve, inferred | `q` | `040D:0FF2 / 378C / 3709` | **U** | Changes callback state after the first drag; exact curve construction needs tracing. No explicit curve editor found in ours. |
| PT-53 | 4 | Circle outline, inferred from radius calculation | `c` | `040D:105F / 31E0 / 393A` | **P** | Ellipse geometry exists; dedicated center/radius circle gesture not verified in our UI. |
| PT-54 | 5 | Filled circle, same radius path | `C` | `040D:1093 / 31E0` | **P** | Filled ellipse exists; same center/radius workflow gap. |
| PT-55 | 6 | Axis-defined ellipse outline | `e` | `040D:10C7 / 3197 / 384B / 38AC` | **P** | Our ellipse tool exists; Disney has an additional axis/third-point phase. |
| PT-56 | 7 | Filled axis-defined ellipse | `E` | `040D:10FD / 3197` | **P** | Filled ellipse exists; axis/third-point interaction differs. |
| PT-57 | 8 | Rectangle outline | `r` | `040D:1133 / 3056 / 3377` | **C** | Rectangle tool exists. |
| PT-58 | 9 | Filled rectangle | `R` | `040D:1167 / 3377` | **C** | Filled-shape option exists. |
| PT-59 | 10 | Text | `t` | `040D:440C / 446B / 44A7` | **P** | Text insertion exists; user font loading and bold/italic/underline do not. |
| PT-60 | 11 | Flood fill | `f` | `040D:119B / 3524 / 2F5A` | **C** | Fill exists on painting layers, with gap closing; intentionally hidden on the soft Pencil plane. |
| PT-61 | 12 | Grab/capture bitmap brush | `b` | `040D:409E / 40C4 / 3E8F` | **M** | Selection/move/clipboard exist; no captured bitmap becomes a reusable painting brush. |

The PT circle radius path computes a distance from the center before calling the common conic renderer; the ellipse path has a separate callback phase for another point/axis. Freehand and spot modes differ in whether an interpolated point path is built. IP rectangle and flood pairs switch the span callback; their exact dither patterns remain unverified. No anti-aliased/vector-path behavior is inferred for the original binary PT raster tools.

## Complete recovered menu catalogue

The 20 `Tools` entries are the slots above; the following tables list the other **120 leaf commands**. Menu names and nesting follow the executable child pointers. Labels have display padding and the submenu-arrow byte removed. `DS` is the text offset in the program data segment, not a physical runtime segment. Addresses are included so an ambiguous label can be rechecked against the binary.

### Ink & Paint menus

| ID | Menu path and original shortcut label | DS text | Gap | Our equivalent / outstanding work |
|---|---|---|---|---|
| IP-01 | Project → New N | `0F9C` | **C** | Create a project; our document can contain an animation from creation. |
| IP-02 | Project → Open → IFF... | `0F44` | **M** | No IFF/ILBM sequence importer in Pencil Test. |
| IP-03 | Project → Open → Anim... | `0F4B` | **M** | No Amiga ANIM importer. |
| IP-04 | Project → Open → CFast... o | `0F53` | **M** | No CFAST/CFT importer in the application; research decoder is external. |
| IP-05 | Project → Append → IFF... | `0F5F` | **M** | No append-IFF-sequence workflow. |
| IP-06 | Project → Append → Anim... | `0F66` | **M** | No append-ANIM workflow. |
| IP-07 | Project → Append → CFast... | `0F6E` | **M** | No append-CFAST workflow. |
| IP-08 | Project → Save S | `0FD3` | **C** | Save/Save As persist an editable PTF project. |
| IP-09 | Project → Save as → IFF... | `0F77` | **M** | No IFF sequence writer; GIF/APNG/sprite-sheet exports provide different outputs. |
| IP-10 | Project → Save as → Anim... | `0F7E` | **M** | No Amiga ANIM writer. |
| IP-11 | Project → Save as → CFast... w | `0F86` | **M** | No CFAST writer. |
| IP-12 | Project → Save as → Secure... | `0F92` | **L** | Artist-secured legacy output; not a requirement for our editable projects. |
| IP-13 | Project → Animate a | `0FF7` | **C** | Play/pause/stop and loop exist. |
| IP-14 | Project → Pencil Test... F5 | `1008` | **L** | Our Pencil/Color workflow is integrated in one application; no DOS process transfer required. |
| IP-15 | Project → About... ? | `101A` | **C** | About dialog exists. |
| IP-16 | Project → Quit... Esc | `102B` | **C** | Quit/close exist. |
| IP-17 | Edit → Undo u | `103E` | **C** | Document undo/redo covers pixels and structural edits. |
| IP-18 | Edit → Restore U | `104B` | **P** | Undo/redo provides recovery, but no separately named restore-screen operation. |
| IP-19 | Edit → Clear K | `1058` | **C** | Clear/fill operations exist; scope is active layer/selection. |
| IP-20 | Edit → Go to... g | `1065` | **P** | Frame selection API and navigation exist; no numbered Go To Cel dialog. |
| IP-21 | Edit → Load Cel... | `1072` | **P** | Opening/importing images exists, but not this single-cel IFF replacement workflow. |
| IP-22 | Edit → Save Cel... | `107E` | **P** | Modern flattened export exists; no dedicated Save Cel command. |
| IP-23 | Camera → Frisket... ~ | `10A4` | **P** | Selections and separate layers provide masking, but no equivalent color-selected Frisket compositor. |
| IP-24 | Camera → In use \` | `10B3` | **P** | Selection-mask/layer visibility controls exist; not the same Frisket enable switch. |
| IP-25 | Camera → Free | `10C2` | **U** | Exact scope of Camera > Free needs tracing; do not equate it to freeing a palette or a mask. |
| IP-26 | Camera → Palette... p | `10C7` | **M** | Pencil Test selects existing palette entries; the palette-editing color picker is excluded from its toolbar build. |
| IP-27 | Camera → Default | `10D6` | **P** | Default palette loads at startup; no equivalent Camera > Default reset command. |
| IP-28 | Camera → Copy to all | `10DE` | **P** | A shared document palette avoids some copying, but there is no range-aware per-cel palette copy action. |
| IP-29 | Camera → Copy Forward | `10EA` | **M** | No forward-range palette propagation command. |
| IP-30 | Camera → Copy Reverse | `10F7` | **M** | No reverse-range palette propagation command. |
| IP-31 | Camera → Auto Remap | `1104` | **P** | Import remapping exists for external FLICs; no toggle for legacy background/cel palette remapping. |
| IP-32 | Camera → Map to → Foreground | `108A` | **M** | No matching palette Map to Foreground workflow. |
| IP-33 | Camera → Map to → Background | `1097` | **M** | No matching palette Map to Background workflow. |
| IP-41 | Preferences → Set Screen Mode → CGA | `111F` | **L** | Historical CGA adapter selection. |
| IP-42 | Preferences → Set Screen Mode → Tandy | `1125` | **L** | Historical Tandy adapter selection. |
| IP-43 | Preferences → Set Screen Mode → EGA | `112D` | **L** | Historical EGA adapter selection. |
| IP-44 | Preferences → Set Screen Mode → VGA | `1133` | **L** | Historical VGA adapter selection. |
| IP-45 | Preferences → Set Screen Mode → MCGA | `1139` | **L** | Historical MCGA adapter selection. |
| IP-46 | Preferences → NTSC Filter → Set... | `1140` | **L** | Analog NTSC palette filter; exact original filter not reconstructed. |
| IP-47 | Preferences → NTSC Filter → In Use | `1147` | **L** | Enable/disable that legacy NTSC palette filter. |
| IP-48 | Preferences → Toolbox F10 | `1176` | **C** | Tools palette can be shown/hidden. |

### Pencil Test menus

| ID | Menu path and original shortcut label | DS text | Gap | Our equivalent / outstanding work |
|---|---|---|---|---|
| PT-01 | Project → New N | `141D` | **C** | Create a project/animation. |
| PT-02 | Project → Open → IFF... | `13CD` | **M** | No IFF/ILBM sequence importer. |
| PT-03 | Project → Open → Anim... | `13D4` | **M** | No Amiga ANIM importer. |
| PT-04 | Project → Open → CFast... o | `13DC` | **M** | No CFAST importer in the application. |
| PT-05 | Project → Append → IFF... | `13E9` | **M** | No append-IFF-sequence command. |
| PT-06 | Project → Append → Anim... | `13F0` | **M** | No append-ANIM command. |
| PT-07 | Project → Append → CFast... | `13F8` | **M** | No append-CFAST command. |
| PT-08 | Project → Save S | `145D` | **C** | Editable project saving exists as PTF. |
| PT-09 | Project → Save as → IFF... | `1401` | **M** | No IFF sequence writer. |
| PT-10 | Project → Save as → Anim... | `1408` | **M** | No Amiga ANIM writer. |
| PT-11 | Project → Save as → CFast w | `1410` | **M** | No CFAST writer. |
| PT-12 | Project → Exposure Sheet... F6 | `1487` | **M** | No exposure-sheet document/view. |
| PT-13 | Project → Animate a | `149C` | **C** | Playback exists for the current frame list. |
| PT-14 | Project → About ? | `14B0` | **C** | About dialog exists. |
| PT-15 | Project → Ink & Paint... F5 | `14C4` | **L** | Integrated Pencil/Color editing removes the need for a separate IP process. |
| PT-16 | Project → Quit... Esc | `14D9` | **C** | Quit/close exist. |
| PT-17 | Edit → Undo u | `14EF` | **C** | Document undo/redo, including frame/layer structure. |
| PT-18 | Edit → Restore U | `14FC` | **P** | Undo/redo recovery exists; no explicit restore-screen buffer command. |
| PT-19 | Edit → Swap j | `1509` | **M** | No dedicated spare/swap-screen editing buffer. |
| PT-20 | Edit → Invert ~ | `1516` | **P** | Invert exists for the active layer; Disney also offers this-cel/all-cels scope. |
| PT-21 | Edit → Clean up F7 | `1523` | **M** | No dedicated binary-cel cleanup command. Gap fill and video line-art processing are different operations. |
| PT-22 | Edit → Insert i | `1531` | **P** | New Frame adds after the active frame; Disney has an Insert cel command with its own placement semantics. |
| PT-23 | Edit → Delete Del | `153E` | **C** | Delete Frame exists and preserves at least one frame. |
| PT-24 | Edit → Copy O | `154D` | **P** | Duplicate Frame and selection copy exist; no independent cel clipboard/bank workflow. |
| PT-25 | Edit → Paste P | `155A` | **P** | Selection paste exists; no dedicated paste/replace-cel clipboard action. |
| PT-26 | Edit → Clear K | `1567` | **C** | Clear/fill selection or active layer exists. |
| PT-27 | Edit → Go to... g | `1574` | **P** | API/navigation exists; no numeric Go To Cel dialog. |
| PT-28 | Edit → Load Cel... | `1581` | **P** | Image/FLIC import exists; no matching replace-current-cel IFF workflow. |
| PT-29 | Edit → Save Cel... | `158D` | **P** | Flattened modern exports exist; no dedicated Save Cel action. |
| PT-30 | Brush → Load... | `1657` | **M** | No custom bitmap-brush file loader. |
| PT-31 | Brush → Save... | `165F` | **M** | No custom bitmap-brush file writer. |
| PT-32 | Brush → Rotate → 45 deg $ | `1599` | **M** | No 45-degree custom bitmap-brush transform. |
| PT-33 | Brush → Rotate → 90 deg ( | `15A7` | **M** | No 90-degree custom bitmap-brush transform. |
| PT-34 | Brush → Rotate → Any Angle | `15B5` | **M** | No arbitrary-angle bitmap-brush transform. |
| PT-35 | Brush → Resize → Halve h | `15BF` | **M** | No halve-bitmap-brush action. |
| PT-36 | Brush → Resize → Double H | `15CB` | **M** | No double-bitmap-brush action. |
| PT-37 | Brush → Resize → Wider X | `15D7` | **M** | No horizontal-only bitmap-brush scale action. |
| PT-38 | Brush → Resize → Taller Y | `15E3` | **M** | No vertical-only bitmap-brush scale action. |
| PT-39 | Brush → Resize → Size Z | `15EF` | **M** | No bitmap-brush resize dialog. |
| PT-40 | Brush → Flip → Horiz x | `15FB` | **M** | Image/layer Flip Horizontal is not a reusable brush flip. |
| PT-41 | Brush → Flip → Vert y | `1606` | **M** | Image/layer Flip Vertical is not a reusable brush flip. |
| PT-42 | Brush → Grab From → Center | `1611` | **M** | No captured-brush center hotspot selection. |
| PT-43 | Brush → Grab From → Corner | `161A` | **M** | No captured-brush corner hotspot selection. |
| PT-44 | Brush → Font → Load... T | `1623` | **P** | Text rendering loads a bundled/fallback TTF internally; no user-selectable legacy soft-font loader. |
| PT-45 | Brush → Font → Flush Fonts | `162F` | **L** | Manual unloading of loaded fonts is a historical memory-management control. |
| PT-46 | Brush → Font → Bold | `163B` | **M** | No bold option in text_options_t/text dialog. |
| PT-47 | Brush → Font → Italic | `1642` | **M** | No italic option in text_options_t/text dialog. |
| PT-48 | Brush → Font → Underline | `164B` | **M** | No underline option in text_options_t/text dialog. |
| PT-62 | Preferences → Screen Format... | `16CD` | **C** | Image Size/Canvas Size offer general dimensions rather than adapter-specific screen modes. |
| PT-63 | Preferences → Frame Rate F | `16DE` | **P** | Frame delays/FPS fields exist; Set FPS menu is currently unwired, with no exposure/duration editor. |
| PT-64 | Preferences → Set Colors... | `16F0` | **P** | Paper/pencil colors and onion tints exist; no equivalent Paper/Fader preset editor. |
| PT-65 | Preferences → Fade Colors → Invert ! | `16A3` | **M** | No onion/fader palette-inversion command; this is not Edit > Invert pixels. |
| PT-66 | Preferences → Fade Colors → Fade In ] | `16B1` | **P** | Onion opacity settings cover the visual intent; no same stepped fade-palette control. |
| PT-67 | Preferences → Fade Colors → Fade Out [ | `16BF` | **P** | Onion opacity settings cover the visual intent; no same stepped fade-palette control. |
| PT-68 | Preferences → Set 0,0 | `1712` | **M** | No user-defined drawing-coordinate origin. |
| PT-69 | Preferences → Coords \| | `171A` | **P** | Coordinates are used internally; no corresponding coordinate-display switch located. |
| PT-70 | Preferences → Show Load | `172C` | **L** | Exact Show Load display semantics need tracing; loading/memory telemetry is not a drawing/timing dependency. |
| PT-71 | Preferences → Toolbox F10 | `1738` | **C** | Tools window visibility control exists. |

### Exposure Sheet menus, inside PT.EXE

| ID | Menu path and original shortcut label | DS text | Gap | Our equivalent / outstanding work |
|---|---|---|---|---|
| XS-01 | Project → New... Alt n | `4AA4` | **M** | No exposure-sheet model to create. |
| XS-02 | Project → Open... Alt o | `4AB6` | **M** | No XPO/EXP sheet loader. |
| XS-03 | Project → Append... | `4AC8` | **M** | No append-sheet operation. |
| XS-04 | Project → Save Alt w | `4AD2` | **M** | No independent exposure-sheet save. |
| XS-05 | Project → Save as... | `4AE4` | **M** | No Save Sheet As action. |
| XS-06 | Project → Save By Sheet → as IFF... | `4A8F` | **M** | No sheet-driven IFF sequence export. |
| XS-07 | Project → Save By Sheet → as Anim... | `4A99` | **M** | No sheet-driven ANIM export. |
| XS-08 | Project → Print | `4B01` | **M** | No printable exposure sheet. |
| XS-09 | Project → Animate Alt a | `4B07` | **P** | General playback exists; it cannot play cel-reference rows, sheet ranges or sound cues. |
| XS-10 | Project → About... | `4B19` | **C** | Application About exists; separate module About is not needed. |
| XS-11 | Project → Pencil Test... F6 | `4B22` | **L** | One integrated application needs no module-return command. |
| XS-12 | Edit → Copy Alt c | `4B34` | **M** | Pixel clipboard exists; exposure-row copying does not. |
| XS-13 | Edit → Cut Alt x | `4B4C` | **M** | No exposure-row cut. |
| XS-14 | Edit → Paste Alt i | `4B64` | **M** | No exposure-row paste. |
| XS-15 | Edit → Sparse Alt f | `4B7C` | **M** | No sparse sheet editor; exact Disney Sparse transformation remains untraced. |
| XS-16 | Edit → Search Alt s | `4B94` | **M** | No search through exposure notes/cel entries. |
| XS-17 | Edit → Search Again Alt g | `4BAC` | **M** | No repeat-search in a sheet. |
| XS-18 | Edit → Block Alt b | `4BC4` | **M** | No sheet block-selection operation; exact legacy selection behavior remains untraced. |
| XS-19 | Edit → First/Last Frame Alt t | `4BDC` | **M** | No exposure-sheet first/last playback-range editor. |
| XS-20 | Preview → Sound Effect | `4BF4` | **M** | No frame-synchronized sound preview. |
| XS-21 | Preview → Flush Sounds | `4C01` | **M** | No sheet sound asset/cache management. |

### Menu interpretation details

- IP's **Camera** menu is Frisket/palette/background work. Its name is not evidence of animated zooms, pans, trucks or a transform timeline.
- PT **Preferences → Fade Colors** changes the onion/fader display palette. Its Invert/Fade In/Fade Out items must not be conflated with destructive pixel inversion, cel opacity keyframes or movie fades. The handlers update `DS:1E2/1E3` and call palette reconstruction at `009B:1935`.
- **Copy/Paste** in the exposure sheet operates on sheet content. Pixel clipboard support does not cover it.
- **Brush → Grab From → Center/Corner** concerns a captured brush's placement reference. General canvas pan/zoom is a separate feature.
- **Free**, **Sparse**, **Block**, **Show Load**, exact Frisket rules and the uncertain tool variants retain explicit semantic limits. Their presence and nesting are confirmed; a full behavioral specification needs additional tracing.
- The exact menu scope includes About/Quit/module-return controls for completeness. Their low implementation cost or redundancy is why the raw command totals are not a development estimate.

## Keyboard-only, palette and dialog controls

These supplement the 140 menu IDs; they are not counted as additional independent feature families.

| Area | Disney control / option | Current Pencil Test comparison |
|---|---|---|
| IP/PT cel navigation | Left/Right previous/next; Up/Down first/last | Equivalent navigation exists with Left/Right, Page Up/Down and Home/End. |
| PT forward/copy | Enter: advance and copy cel; last-cel/loop behavior | Duplicate Frame plus navigation is related; no same combined Enter action. |
| PT onion | F2 toggle | Onion toggle and opacity dialog exist; shortcut differs. |
| PT drawing loop lock | F3 toggle; wraps cel navigation | Whole-animation playback loop exists; drawing-navigation loop lock is distinct. |
| PT pose bookmarks | F4 stores current cel in one of ten digit-key slots | No corresponding key-pose bookmark bank. |
| Modules | F5 IP/PT transfer; F6 Exposure Sheet/PT transfer | Integrated layers replace F5's module boundary; no sheet exists for F6's work. |
| Memory | F1 main/EMS memory monitor and low-memory prompts | No equivalent DOS memory monitor needed; allocation/reporting behavior is separate. |
| PT cleanup | F7 current-cel cleanup | Dedicated cleanup command missing; exact old neighborhood filter remains untraced. |
| Workspace | F8 cursor; F9 menu; F10 menu/toolbox toggles | Tool visibility exists; no claim of all exact toggle combinations. |
| IP palette navigation | `[`/`]` previous/next color; `{`/`}` MCGA palette bank | Color swatches/pages exist; no corresponding keyboard bank workflow located. |
| IP animation palette | Tab toggles color cycling | No palette-cycle playback/authoring. |
| IP color controls | Foreground/background choice; in-use colors | Existing indexed swatches and foreground/background controls are related. |
| IP magnifier | Zoom/magnification state, factors clamped between 2 and 8 in inspected handlers | General zoom, hand pan and magnifier exist; original pixel-editing interaction not fully specified. |
| IP Frisket dialog | Color-selected protection, use/clear/invert-style controls | Some labels/callbacks remain to be traced; current selections/layers are only a partial substitute. |
| IP background loading | Load Background; apply to selected cels/range; palette remapping | Shared Background is implemented; no same batch imported-background/range operation. |
| IP NTSC | Filter low/high; enable flag | Legacy palette filter, not ordinary color correction. |
| PT screen format | 320×200, 640×200, 640×350, 640×480 labels | General image/canvas size controls cover document dimensions. |
| PT ink/paper | Set Colors; Paper/Fader; SET 1..4 | Soft Pencil color, document palette and onion tints exist; no same four fader preset sets. |
| Frame rate dialog | Presets/custom rate; Enforce Frame Rate; Exit; Exit & Animate | Frame-delay playback exists; no completed FPS/duration UI. Exact Disney late-frame policy unverified. |
| Exposure text editing | Home/End line start/end; Page Up/Down page navigation | No sheet text editor. |
| Sound preview | Pitch, Volume, Shift-to-listen, Load Another | Missing synchronized sound-preview asset workflow. |
| Export confirmation | Save exposure sheet too; artist note; overwrite; multi-volume prompts | Overwrite/project save covered generally; sheet and artist-note fields missing; multi-volume floppy UI obsolete. |
| File requester | Path, Parent, Volume, Match Pattern, Free Space | Modern file picker covers basic navigation; exact legacy filters are not a product requirement. |
| Font management | Load/flush soft font, size families, styles | Internal bundled/fallback TTF and size/AA options; user font/style management missing. |
| Release-memory prompts | Free fonts, exposure clipboard, brushes, swap/copy screen | Resource ownership should handle this automatically; corresponding content models are separate gaps. |

### Exposure-sheet instruction vocabulary

| Instruction | Recovered role / evidence | Current gap |
|---|---|---|
| `!BEGIN` / `!END` | Start/end markers inspected in the preflight resolver | No sheet range markers. |
| `!SFX "file.INS" [SPEED n] [LOOP n]` | Sample cue syntax in help; actual sample notes contain `!sfx` | No structured frame sound events or synchronized sample player. |
| `!NOTE "file.INS" [KEY a] [OCT n] [RELEASE]` | Note/instrument syntax in embedded help | No instrument/note event path. Exact legacy synthesis semantics not reconstructed. |
| `!INSTRDIR "path"` | Instrument lookup path; present in SACK-BW sample | No sound-asset path management. |
| `!STOP` | Sound command in help; exact stopping scope untraced | No sound event engine. |

Embedded lesson text and sound instructions were treated as file contents. Nothing in them was executed as an instruction to the assistant.

## How the recovered subsystems work

### Tool events and drawing

Both executables replace a small table of active-tool far pointers when the selected tool changes. Input handling calls those slots for different interaction phases. IP `041A:111D` installs its first tool callbacks; PT `040D:0EAD` installs the corresponding freehand callbacks. PT `040D:3267` generates a raster path between pointer samples, while `085B:02FB` performs clipped one-bit plotting/erasing into the selected video plane.

IP fill is an iterative scanline operation. `041A:2310` creates mask/work storage and `041A:20A4` consumes seven-byte span records with row/endpoints/direction, paints horizontal spans and schedules adjacent spans. Our gap-closing fill constructs temporary barriers and fills painting-plane regions. These are different implementations of related fill work; no old gap-closing mechanism is asserted.

### Drawing storage and onion skin

PT allocates 18-byte cel records at `0FE4:0547`. The inspected record has list links, a payload representation, encoded size, representation/location flags and EMS context. `0FE4:0438` retrieves/maps the payload; `0FE4:0331` restores it. In-memory compressed cels use signed-byte run controls decoded at `085B:0DDD`. A changed flag controls store-before-navigation at `0FE4:06E9/0B35`.

PT `0FE4:0604` loads four neighboring **drawings** into four EGA/VGA bitplanes, selecting write/read planes through `085B:000A`. `009B:1935` builds the palette that shows current ink and faded neighbors. These are not four logical paint layers. IP uses larger 82-byte cel records and separately stored color planes.

Our drawing bundle has a shared Background and independently encoded per-frame Color/Pencil/FX planes. Only active working layers remain decoded. Onion skins use revision-keyed R8 textures and independent opacity for four previous/four following Pencil planes. This foundation can stay when adding exposure references.

### Exposure references and deletion

PT exposure rows store a cel ordinal at record `+0C`, note length at `+12`, and note bytes at `+14`. Preflight at `13A0:3CFE` resolves ordinals to cel pointers at `+08`. Cel zero carries the previous drawing forward; the first frame must specify a valid cel. The same cel can be referenced elsewhere without copying its pixels.

Before deleting a cel, `0FE4:0B66` queries the sheet for references through `13A0:0A2F`. Insert/delete then adjusts sheet ordinals at/above the affected cel index through `13A0:09E1`. A modern implementation should use stable IDs and an explicit reference-aware deletion policy.

### Verified saved examples

| Sample | Stored drawings | Exposure rows | FPS | What it demonstrates |
|---|---:|---:|---:|---|
| `2LEGWALK.CFT` | 16 | 31 | 24 | `1,0,2,0,...,15,0,16`: drawings held for extra exposures. |
| `PATH.CFT` | 8 | 16 | 12 | Alternating cel references and holds. |
| `SACK-BW.CFT` | 21 | 21 | 12 | Instrument-directory note and sound cues at frames 4, 9, 14. |
| `PLUTO.CFT` | Pixel bank not decoded in this pass | 154 | 24 | Repeated references to cels 44..66 and a “Start loop” comment. |

**143 monochrome cels** in the ten plain GUCF files were decoded, with exact column-run/payload consumption and 8,000 output bytes per 320×200 drawing. **1,050 XPOS rows** across all 18 CFT files parsed to exact EOF. Eight disk02 CFTs begin with STDY lesson wrappers; their pixel codec remains unexamined. This corrects the first extraction report's overly broad implication that every CFT begins with GUCF.

### Current timing defect and missing workflow

The `.orion` menu declares `Set FPS...`, but no command handler/dialog is implemented in the audited application. The model and file formats preserve delays; that is storage/playback support, not an editing UI.

New timelines use `fps = 12`, while new frames use `delay_ms = 80`. Playback prefers a positive frame delay, producing 80 ms exposures (12.5 fps) for those defaults. Finish timing authoring with one defined source of truth and deliberate conversion to import/export granularity. Holds can initially use durations; reusable recurring drawings still require references.

## Implementation groups and acceptance criteria

| Priority | Deliverable | Concrete acceptance condition | Relative size |
|---|---|---|---|
| 1 | Timing controls | Set FPS works; holds/durations editable; defaults agree; import/export preserves intended running time. | Small–medium UI/model integration. |
| 2 | Cel bank + exposure references | One drawing reused at three positions updates all three; Make Unique breaks sharing; deleting a referenced cel gives explicit choices; undo/redo preserves reference structure. | Large foundational model/history/file work. |
| 3 | Exposure-sheet view | Edit cel IDs/durations/notes, select/copy/paste/retime blocks, mark playback ranges and navigate separately from drawing-bank flipping. | Large workflow/UI work after group 2. |
| 4 | Sound annotations/player | Structured cues sync with exposures and survive save/load; preview has pitch/volume controls; export has a defined audio policy. | Large new subsystem. |
| 5 | Captured bitmap brushes | Capture selection as brush; define hotspot; stamp; rotate/scale/flip; load/save; history remains correct. | Medium–large distinct tool subsystem. |
| 6 | Palette/Frisket/background workflow | Editable palette, propagation scope, remapping, protected colors and batch backgrounds are explicit; cycles survive playback/export. | Medium–large paint/data work. |
| 7 | Remaining tool/text parity | Confirm original uncertain tools; add wanted curve/axis gestures, cleanup, font/style controls and key-pose bookmarks. | Several focused small–medium features. |
| Optional | Original-format interchange | Validate and import/export selected CFT/XPO/ILBM/ANIM variants, including wrappers, notes, palettes and timing. | Large if full compatibility is required. |

These are dependency/size categories, not calendar promises. No defensible total person-week estimate follows from static menu counts alone. Full parity spans multiple development milestones; usable timing and pose reuse can be delivered before legacy brush, palette, audio and codec completeness.

Keep static UI in `.orion`, use Orion controls/layouts for sheet/bank views, and extend the responsible framework component for reusable gaps. A sheet can use TableView with a datasource; the cel bank is a separate data-driven view. Keep the current sidebar compact. Preserve the document command boundary, failure rollback and encoded storage; shared immutable cel payloads can later reduce duplication in history.

## Source map and reproducibility

| Evidence | Location |
|---|---|
| Visible commands and Pencil Test sidebar | [`imageeditor.orion`](../apps/imageeditor/imageeditor.orion) |
| Live layer/storage/history contracts | [`ARCHITECTURE.md`](../apps/imageeditor/ARCHITECTURE.md) |
| Frame ownership, timing and palette fields | [`anim.h`](../apps/imageeditor/anim/anim.h) |
| Independent plane encoding; duplicate/delete/move | [`anim.c`](../apps/imageeditor/anim/anim.c) |
| Working-layer commit/load/switch | [`anim_document.c`](../apps/imageeditor/anim/anim_document.c) |
| Frame command integration | [`commands_anim.c`](../apps/imageeditor/commands/commands_anim.c) |
| Actual canvas tool handlers and onion rendering | [`win_canvas.c`](../apps/imageeditor/windows/win_canvas.c) |
| Command dispatch and FPS-handler absence | [`win_menubar.c`](../apps/imageeditor/windows/win_menubar.c) |
| Playback delays | [`win_timeline.c`](../apps/imageeditor/windows/win_timeline.c) |
| Tool groups, filled shapes and size/gap controls | [`win_tooloptions.c`](../apps/imageeditor/windows/win_tooloptions.c) |
| Pencil palette UI and build-specific color-edit exclusion | [`win_toolpalette.c`](../apps/imageeditor/windows/win_toolpalette.c) |
| Color fill boundary | [`commands_pencil.c`](../apps/imageeditor/commands/commands_pencil.c) |
| Palette selection | [`commands_palette.c`](../apps/imageeditor/commands/commands_palette.c) |
| Soft-pencil rendering | [`canvas_stroke.c`](../apps/imageeditor/canvas/canvas_stroke.c) |
| Gap fill | [`canvas_fill_gap.c`](../apps/imageeditor/canvas/canvas_fill_gap.c) |
| Text implementation/options | [`canvas_text.c`](../apps/imageeditor/canvas/canvas_text.c), [`win_textdialog.c`](../apps/imageeditor/windows/win_textdialog.c) |
| Whole-document snapshots | [`undo.c`](../apps/imageeditor/core/undo.c) |
| Project/FLIC timing and layer serialization | [`flc.c`](../apps/imageeditor/io/flc.c) |

Original executable SHA-256 identities:

```text
IP.EXE  0112e656002f63a41a70d17f2a5b1a378d4413bf91525c8800ce18b53575bfb9
PT.EXE  41e67638e03a9395fbe00b0eee623d8638411d23c52796617c9b7492e846e041
```

IP data segment is load-relative `20C1`, MZ header length `2A00`; PT data segment is `22D2`, MZ header length `3200`. To locate a table string in the original executable: `header_bytes + data_segment*16 + DS_offset`. To locate code: `header_bytes + CS*16 + IP`. Segment values here precede DOS runtime relocation.

The extracted images, full strings/disassembly, decoder, focused evidence excerpts and decoded sample previews remain in the user's `Downloads/Disney Animation Studio (3.5-720k)/unpacked-analysis/` analysis directory. Commercial binaries, artwork and audio were not copied into the repository. This Markdown file contains the durable catalogue and evidence addresses needed to recheck the findings against the identified executables.

Validation for this documentation change: recovered command IDs are contiguous in each command space (IP 1–48, PT 1–71, XS 1–21), every leaf appears once in the tool/menu tables, totals are generated from the same coverage ledger, and local Markdown links are checked. No application tests are claimed from this documentation-only edit.
