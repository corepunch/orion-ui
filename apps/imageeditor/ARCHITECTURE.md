# Image Editor and Pencil Test document commands

Both applications share the document model, command implementations, and history.
Pencil Test uses indexed pixel storage; history always uses `DOC_BPP` for working
layers and preserves the document palette as well as each stored frame's format.

## Mutation boundary

Every user edit must execute a named command. Menu, toolbar, and accelerator
handlers call the same `cmd_*` implementation. Immediate commands live in
`commands/`; interactive drawing commands use the same lifecycle across pointer
events and tool handlers:

```c
if (!ie_doc_begin_op(doc, "Operation name")) return;
bool success = perform_model_edit(doc);
ie_doc_commit_op(doc, success);
```

`ie_doc_begin_op` captures the document **before any mutation**, including a
rubber-band preview or selection change. Callers must stop if it fails. A drag
begins once on pointer-down and commits once on pointer-up. Polygons commit on
completion; crop previews commit on Enter. Escape, pointer cancellation, or
switching away from a preview rolls back the command. Freehand tool switches
finish the current stroke. Model helpers do not push additional history entries.
A new mutation path needs both its command boundary and a round-trip test.

Each document owns its pending command; there is no global transaction context.
Nested begins are rejected and traced. Another document can have its own command.
Closing a document releases its pending checkpoint without leaving global state.

Read-only operations and navigation (copy, frame/layer selection, zoom, pan,
playback, tool selection, and display settings) do not create history entries.
Frame selection commits the working pixels to that frame's storage before loading
the target frame. Structural frame commands stop playback before capturing state.

## History ownership and failure behavior

`core/undo.c` owns the opaque `doc_snapshot_t`. Callers cannot substitute a raw
pixel buffer for a checkpoint. A checkpoint contains:

- Canvas dimensions, background properties, layers, names, visibility, opacity,
  blending, active layer, and mask editing state.
- Selection mask, bounds, and floating selection pixels and mask.
- Every bank drawing's bytes, format, palette and name, stable cel IDs, exposure references, durations, notes and order,
  active frame, playback rate, and loop setting.
- The indexed document palette, when applicable.

GPU textures, window pointers, tool previews, and playback timers are not restored
from history. Texture caches are regenerated. Playback is stopped before undo or
redo. Resizing/cropping updates stored frames and working layers in one command.

Successful changed commands add one checkpoint and clear redo. Cancellation
restores the checkpoint; a no-op frees it without adding history or clearing redo.
Allocation failure before beginning leaves the document untouched. Undo and redo
allocate the opposite checkpoint before changing either stack, then transfer
already prepared storage into the document without further allocations. Failed
commands restore the complete document, including partially applied model edits.

History is bounded by `UNDO_MAX` entries. The initial implementation uses complete
checkpoints for correctness, rather than independently authored inverse commands.
This can consume substantial memory for large images or long animations. A future
optimization can store shared immutable frames/layers or changed tiles behind the
same opaque checkpoint API. Such an optimization must preserve structural edits,
selection, palette, cancellation, and allocation-failure guarantees; an affected
pixel rectangle alone cannot represent a frame deletion or canvas resize.

The former push/discard-checkpoint API has been removed. All edits, including
tests, use the command transaction API; callers never manipulate history stacks. `doc_undo`/`doc_redo` are model operations; user actions
use `cmd_undo`/`cmd_redo` so all views and command availability are refreshed.

## Presentation and availability

Command completion and history navigation refresh the canvas, layers, timeline,
onion caches, document title, and Undo/Redo availability. Undo/Redo also
refresh the canvas view extent after restoring dimensions. A pending command
disables both history actions; otherwise availability comes from the active
document's corresponding stack. Empty history and no document disable both.

The framework represents disabled menu entries with `menu_item_t.disabled` and
disabled toolbar items with `TBSTATE_DISABLED`, updated through
`tbEnableItem`. Disabled entries are drawn muted and excluded from hit-testing.
Disabling a toolbar item also cancels its pending press. Commands independently
reject unavailable undo/redo, including accelerator and programmatic dispatch.

## Regression coverage

`tests/history_cases.h` runs in RGBA, Pencil Test, and indexed configurations.
It exercises real canvas input and tool handlers, exact shape pixel round trips,
frame creation/duplication/deletion/reordering, frame navigation between edits,
metadata, cancellation, no-ops, branching history, per-document isolation,
selection, resizing, layers, palettes, and menu/toolbar availability. Existing
stroke, gesture, command, fill, layout, and UI suites cover the other edit paths.
Framework popup/toolbar tests verify disabled items cannot dispatch clicks.

Build the applications with `make build/bin/imageeditor penciltest`. Build tests
with `make build/bin/test_history_test build/bin/test_penciltest_history_test
build/bin/test_history_indexed_test`, then run each executable.

## Timing, reusable cels and exposure sheet

`anim_timeline_t.bank` owns drawings once; `frames` is an array of
`anim_exposure_t` entries that borrow a bank cel and own a duration and a
127-byte note. Drawings have stable, monotonically allocated IDs. Removing or
reordering an exposure does not renumber or destroy its drawing. The bank retains
unused drawings until Delete Unused Cel; deletion rejects any referenced cel.
New Frame creates a blank drawing and an exposure. Duplicate Frame copies pixels
into a new independent drawing. Repeat Current Cel inserts another reference;
Make Cel Unique clones a shared drawing for the active exposure. Editing a shared
cel updates every reference, while durations and notes stay independent.

The Anim menu exposes Set FPS (Ctrl+Shift+F), Frame Duration (Ctrl+Shift+D), and
Cel Bank & Exposure Sheet (F6 in Pencil Test). The two tables and their editing
fields/actions are declared in `imageeditor.orion` and use the `animation`
datasource. `anim/animation_db.c` supplies temporary presentation records for the
modal document binding; the registered database instance has process lifetime,
and releases its row buffers when the dialog closes. Explicit `source="db.table"`
form bindings override the inherited database through the generic form engine.
Sheet refresh suppresses its own selection notifications before restoring the
active row, because TableView selects its first row on refresh.

The sheet edits one row at a time: choose a row, enter a cel ID/duration/note,
then Apply Row. Choosing a bank cel fills the cel ID for Apply Row or Insert Cel.
New Drawing creates a blank cel/exposure; Rename Cel updates its shared name.
Move Up/Down moves complete exposure records. Changes apply immediately through
named document commands, with Undo/Redo available in the dialog. Block selection,
copy/paste, range markers, sheet search and sound events remain future work.

Durations are the playback/export source of truth, in integer milliseconds
(1–65535). FPS (1–1000) sets the default for new exposures as nearest `1000/fps`
ms (12 FPS starts at 83 ms). Changing FPS can retime every duration by
`old_fps/new_fps`, rounded to nearest ms with a minimum of 1 ms, preserving holds
within that granularity. Unchecking Retime keeps existing durations. A retime
that would exceed 65535 ms is rejected before editing. Frame Duration changes
the active exposure or all exposures. Both controls stop playback and are
undoable. GIF export retains its centisecond format granularity.

History clones each bank drawing once and remaps exposure pointers by stable ID;
it preserves both sharing and unused cels. Resize/crop transforms each bank cel
once, including unused drawings. The active cel uses its live working layers,
including when another exposure of that cel is exported. Onion skin follows
exposure order. Existing independent plane compression remains unchanged.

New Pencil Test saves write `PTL6` planes in bank order, preceded by a `0x7076`
`PTB1` trailer. Its 16-byte header contains chunk length/type/magic; payload begins
with little-endian u32 cel count, exposure count and next cel ID. Each bank record
is a u32 ID plus a 32-byte name. Each exposure record is u32 cel ID, u32 duration,
and a 128-byte note. The loader validates exact lengths, IDs, references and
durations before adopting the bank. `PTL6` then contains shared Background followed
by three independent planes per bank cel. Both PTL5 projects (one drawing per
exposure) and external FLIC imports still open. New bank projects require this
version of Pencil Test for editing; their flattened FLC stream remains playable
by other readers. `PTA1` metadata still describes playback exposures.

`test_penciltest_exposure_test` covers timing validation/retiming/history,
three-way shared edits, Make Unique, reference-aware deletion, note/duration
reordering, PTF round trips with unused cels, invalid saved references, resize,
and real declarative table bindings/action dispatch.

Validation on 2026-10-15: Pencil Test and Image Editor builds succeed; 195 tests
across 15 targeted suites pass, covering the new features plus history in all
three pixel modes, UI/layout, layers, persistence, compression, large-project
memory, video import and generic form-source binding. Native screenshots were
inspected for the FPS dialog and combined cel-bank/exposure-sheet form.

## Pencil Test layers and animation files

Pencil Test has four fixed byte planes, composited bottom to top: Background,
Pencil, Color, FX. Background is shared by all frames. Each bank drawing (`anim_frame_t`) stores
three independent RLE chunks in `cels` (Color, Pencil, FX), with an encoded
`cels_size`. Layered frames have no retained flattened `data` copy. Single-plane
FLIC imports use `FRAME_FORMAT_RLE_INDEXED` until committed into the fixed layers.
The live document owns four decoded working layers. `doc_anim_commit`,
`doc_anim_load`, and `doc_anim_switch` manage their transfer. Frame structure edits,
resizing/cropping, and history preserve all cels and the shared background.
Rendering and export compose against the live shared background,
rather than trusting a preview from before the background changed.

The tool strip stays one column wide. The four layer buttons are half-size
(`TOOLBAR_ITEM_FLAG_SMALL`) and pack 2×2 into a single button cell; the floating color toolbar
uses the same half-size swatches, two per row. Pencil hides the color palette and
uses a fixed configurable pencil color; painting paper on it erases to transparency. Background,
Color, and FX use the solid indexed-color brush and expose the document palette,
with their last color remembered during the session. Pencil uses only the pencil
drawing tool. The drawing button follows the active layer; keyboard tool commands
use the same constraint. Brush size lives in the left toolbar, and drawing tools
never open the floating options panel. Image Editor exposes pencil, brush, and
spraypaint as separate toolbar buttons beside the shared size control. The canvas shows the active layer and those beneath it in the compositing
order: Background alone, Background/Pencil, Background/Pencil/Color, or all four for FX.
The Background starts as white paper. This editing cutoff does not alter layer visibility
flags or saved/exported compositions. Color fill uses opaque ink on the Color plane as a boundary.
Onion skin decodes neighboring Pencil planes into a temporary byte buffer, uploads
an R8 texture, and frees the buffer. Only the textures for visible onion slots
persist; there is no decoded-frame cache. Document composition and exports likewise
decode one plane at a time, then release the temporary buffer. Resize transforms
and recompresses one stored frame at a time; history copies encoded frame bytes.
Each frame is independently decodable, so arbitrary selection, reordering and
deletion never need a prior frame. Each exposure carries its own duration in milliseconds; drawing pixels are shared by reference.

Pencil Test has no timeline window or numbered frame strip. Its frame controls live
below the drawing tools in the left sidebar: current frame number, previous/next
(`<<` / `>>`), play/pause, onion skin, new, duplicate, and delete. The current frame's
tooltip includes the total frame count. Playback timers target the tool window,
including when frame delays change. Frame edits, navigation, document activation,
and closing refresh the controls and their enabled/checked states. Image Editor
retains its numbered timeline and drag reordering.

Shortcuts come from `imageeditor.orion`: Left/Right (also Page Up/Down) navigate;
Home/End select the first/last frame; Space plays or pauses at the displayed frame;
N adds, D duplicates, and Shift+Delete or Shift+Backspace deletes a frame. Stop during
playback restores the frame selected when playback began. Text-editing controls
suppress these shortcuts.
Frame controls never decode frames, composite full-resolution images over paper,
or retain preview textures. Indexed canvas layers remain R8 textures resolved/tinted
on the GPU. `test_penciltest_render_test` checks this boundary and optionally
benchmarks opening and selecting frames via `PENCILTEST_RENDER_EXAMPLE=/path/to/project.ptf`.
The benchmark stubs drawing/upload calls; its times exclude canvas GPU rendering.

For the 145-frame, 1168×784 example, retained Pencil/Color/FX frame payloads occupy
50.23 MiB instead of 506.51 MiB for the previous cels plus flattened previews.
The four decoded working layers occupy 3.49 MiB. Render buffers, textures, metadata
and undo checkpoints are additional allocations; these figures are payload sizes,
not total process RSS. Compressed buffers own only their encoded allocation size.

Pencil stores alpha from 0 to 255 in a `GL_R8` texture, tinted with
`pencil_configured_color()`. Dabs add coverage even when a stroke crosses or
retraces itself, saturating at 255. Distance-based dab spacing and flow normalized
by brush width keep a straight pass near the sidebar's opacity (default
`IE_PENCIL_DEFAULT_OPACITY`, 64), independent of input event frequency; a stroke
keeps the opacity it began with. A floating-point accumulator preserves fractional dab
coverage; fade-tail updates add only the change from the previous path length.
The fade distance is 16 logical pixels at each end. Fade distance, dab spacing,
grain and opacity are configured in `imageeditor.h`.
Background and Color store palette indices in `GL_R8`, resolved by the renderer's
palette shader. Index 255 is transparent; ink and paper occupy indices 0 and 1.
New Background pixels contain paper, new Color pixels contain 255, and new
Pencil pixels contain zero alpha. The default palette is compiled from `palette.c`,
with the 16 Disney screenshot-based colors followed by four light-to-dark
ramps of four colors each: gray, brown, rose and violet. Each ramp holds its
hue and saturation nearly constant. Ink and paper remain at indices 0 and 1.
Image Editor, Pencil Test and the converter link the same C table;
projects embed their own palette. No tool swatch is substituted on import.
Color selection chooses an existing document index without adding colors, scanning
frames or touching history. Duplicate RGB entries retain their distinct indices.
Selections are remembered per document and painting layer. Colors occupy a separate
floating toolbar aligned to the upper right of the canvas. Its half-size swatches pack two per row; larger palettes use pages of 32
defined colors, skipping unused/transparent entries. The toolbar hides on Pencil
and shows on Background, Color, and FX. Framework docking preserves a dragged
position across layer changes and display resizing. Static drawing and frame
commands are declared in the `pencil_tools` form in `imageeditor.orion`; C adds the
runtime layer buttons, current-frame display, and document color swatches.

File > Save / Save As writes `.ptf` (Pencil Test project). It retains an FLC
playback stream with a COLOR256 palette, compressed flattened frames and a ring frame, followed
by versioned editing chunks. Older FLC/FLI and PCX/BMP files open on Color with
empty Background/Pencil/FX planes. Saving an imported file asks for a PTF name.
GIF, APNG and sprite-sheet exports flatten the complete composition. The RGBA
Image Editor retains its existing document workflow.

Readers support COLOR256, COLOR64, COPY, BLACK, BRUN, LC and SS2, palette changes,
repeated frames, prefix chunks and odd dimensions. Dimensions are physical
pixels; FLI timing uses 70 Hz ticks. External FLICs map to a common palette with
index 255 reserved for transparency; animations requiring over 255 opaque colors
are rejected. True-color FLIC variants remain unsupported.

The existing `0x7074` top-level trailer uses `PTA1` at offset 8 in its 16-byte
header. It contains one 1084-byte little-endian record per frame: `PTF1` at 0,
name at 4 (32 bytes), FPS at 36, delay at 40, loop/show-paper at 44/45, paper RGBA
at 48, and 256 packed RGBA palette entries at 60. Other bytes are reserved.

The legacy `0x7075` PTL5 top-level trailer follows it in older projects. New saves use the PTB1/PTL6 extension described above. Its 16-byte header holds total
chunk length at 0, type at 4, `PTL5` at 8, layer count 4 at 12, active layer at
13, a four-bit visibility mask at 14, and reserved zero at 15. Its payload is
one shared Background plane, then Color/Pencil/FX planes for every frame in
timeline order. Each plane is a complete FLIC pixel subchunk with a 32-bit length,
16-bit type, payload, and optional zero byte for word alignment. Decoded planes
are exactly width × height bytes. The shared `io/flc_codec.h` encoder selects
BLACK (13), scanline byte-run RLE (15), or COPY (16) for independent cel records.
COPY bounds worst-case size for noisy pixels. Earlier PTL5 files with FLI delta
records (12) remain readable: the loader reconstructs one frame at a time through
two temporary cel buffers, then immediately repacks it as independent RLE.
Pencil coverage is never palette-remapped.

The flattened playback stream also supports FLI byte deltas and writes its palette once.
Its ring frame restores the first image from the last. File length, second-frame
offset, and layer-trailer length are patched after encoding. PTF requires the
editing extension; old `PTL2`/`PTL3`/`PTL4` projects are rejected. Plain external
FLC/FLI imports still work. Other FLC readers can use the flattened playback
stream but cannot preserve the editing layers if they rewrite it.

All pixel compression is lossless. Retained encoded frame storage plus the shared
background is limited to 512 MiB; three decoded working cel planes are separately
limited to 512 MiB. The frame-count × decoded-area limit has been removed, allowing
long compressible animations. Frame delays are 1–65535 ms. Save reads live working layers
without mutating history, writes and flushes a sibling temporary file, then
atomically replaces the destination. A failed write preserves the original.
Load validates and decodes the file before opening a document; malformed chunks,
unknown layer versions, truncated packets, out-of-row runs, missing delta bases,
trailing layer data and allocation failures discard partial data. The loader never
retains a decoded movie or flattened preview stream.

### Background picture

Background may also hold one sRGB picture under its palette paint, so a
background is not limited to the document palette. `doc->background.image` is an
immutable, reference-counted straight-alpha RGBA8 `bg_image_t` at canvas size.
Checkpoints retain it instead of copying it; setting, removing or resizing it
replaces the image. `background_cache` holds its texture, keyed by the image's
unique id. The picture draws and composites at the start of the Background
layer, with that layer's visibility and opacity, beneath the indexed plane.

Layer > Background Image... opens the stock browser: an `IconGrid` photo grid
(`RVM_VIEW_THUMBNAIL`) in the `background_stock` form, filled from one texture sheet of the 128 px square
thumbnails in `penciltest/share/thumbs/` (generated by `make -C
apps/imageeditor/penciltest thumbs`). From Photo... and Layer > Background from
Photo... pick a picture through `get_open_image_filename`: on iPad the Photos
library (via `axGetOpenImageName`), elsewhere a JPEG/PNG/BMP from `~/Pictures`. Both cover-fit the
picture to the canvas in linear light and run one command that also clears the
Background plane to transparent; Remove Background Image is undoable as well.
Canvas resize crops the picture and image resize resamples it, in the same command.

PTF saves the picture losslessly as a PNG in a `0x7077` `PTI1` chunk after the
layer chunks. Its 16-byte header holds the chunk length, the type, `PTI1`, and
four reserved zero bytes. The loader requires layered data and the canvas size.
Older readers skip the chunk. The 256-colour flattened FLC playback stream shows
paper where the picture is. PNG, APNG, GIF and image exports include it.
`test_penciltest_background_test` covers resampling, history sharing, composite,
PTF round trips, resize and the stock thumbnails.

Regression coverage: `test_penciltest_layers_test`, `test_penciltest_flc_test`,
`test_penciltest_rle_test`, `test_penciltest_memory_test`, `test_penciltest_video_test`, `test_penciltest_layout_test`,
and the shared history suites. Layer tests cover
sidebar routing, palette visibility, underpaint fill, frame operations, playback,
resize/undo, PTF round trips, corrupted extensions, and flattened exports.

Format reference: Jim Kent's [The FLIC File Format](https://jacobfilipp.com/DrDobbs/articles/DDJ/1993/9303/9303a/9303a.htm).
