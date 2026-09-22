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
- Every animation frame's bytes, format, palette, name, delay, frame order,
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
thumbnail/onion caches, document title, and Undo/Redo availability. Undo/Redo also
refresh the canvas view extent after restoring dimensions. A pending command
disables both history actions; otherwise availability comes from the active
document's corresponding stack. Empty history and no document disable both.

The framework represents disabled menu entries with `menu_item_t.disabled` and
disabled toolbar items with `TOOLBAR_ITEM_FLAG_DISABLED`, updated through
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

## Pencil Test layers and animation files

Pencil Test has four fixed indexed planes, composited bottom to top: Background,
Color, Pencil, FX. Background is shared by all frames. Each `anim_frame_t` stores
three canvas-sized cel planes in `cels` (Color, Pencil, FX); `data` retains a
flattened preview. The live document owns four working layers. `doc_anim_commit`,
`doc_anim_load`, and `doc_anim_switch` manage their transfer. Frame structure edits,
resizing/cropping, and history preserve all cels and the shared background.
Rendering, thumbnails, and export compose against the live shared background,
rather than trusting a preview from before the background changed.

Four sidebar buttons form a 2×2 selector. Pencil hides the color palette and
uses monochrome ink; painting paper on it erases to transparency. Background,
Color, and FX expose 16 swatches, with their last tool/color remembered during
the session. Color fill uses the Pencil plane as a boundary and writes only to
Color. Onion skin reads only neighboring Pencil planes, above Background/Color
and beneath the current Pencil/FX. The timeline retains one shared frame timing.

Index 0 remains transparent; original ink and paper occupy indices 1 and 2.
Default coloring swatches occupy indices 3–16. Imported palettes can be larger.
Selecting a missing swatch adds it as an undoable command, using only an empty
slot unreferenced by any working layer, stored composite, or cel.

File > Save / Save As writes `.ptf` (Pencil Test project). It retains an FLC
playback stream with flattened COLOR256/COPY frames and a ring frame, followed
by versioned editing chunks. Older FLC/FLI and PCX/BMP files open on Pencil with
empty Background/Color/FX planes. Saving an imported file asks for a PTF name.
GIF, APNG and sprite-sheet exports flatten the complete composition. The RGBA
Image Editor retains its existing document workflow.

Readers support COLOR256, COLOR64, COPY, BLACK, BRUN, LC and SS2, palette changes,
repeated frames, prefix chunks and odd dimensions. Dimensions are physical
pixels; FLI timing uses 70 Hz ticks. External FLICs map to a common palette with
index 0 reserved for transparency; animations requiring over 255 opaque colors
are rejected. True-color FLIC variants remain unsupported.

The existing `0x7074` top-level trailer uses `PTA1` at offset 8 in its 16-byte
header. It contains one 1084-byte little-endian record per frame: `PTF1` at 0,
name at 4 (32 bytes), FPS at 36, delay at 40, loop/show-paper at 44/45, paper RGBA
at 48, and 256 packed RGBA palette entries at 60. Other bytes are reserved.

The new `0x7075` top-level trailer follows it. Its 16-byte header holds total
chunk length at 0, type at 4, `PTL2` at 8, layer count 4 at 12, active layer at
13, a four-bit visibility mask at 14, and reserved zero at 15. Its payload is
one shared Background plane, then Color/Pencil/FX planes for every frame in
timeline order. Every plane is exactly width × height bytes. PTF requires this
extension; its size, signature, layer count, and selectors are validated before
opening. Plain legacy FLC can omit it. Older FLC readers can use the flattened
playback stream but cannot preserve the editing layers if they rewrite it.

Files are currently uncompressed. Total composite/cel/background pixel storage
is limited to 512 MiB; frame delays are 1–65535 ms. Save reads live working layers
without mutating history, writes and flushes a sibling temporary file, then
atomically replaces the destination. A failed write preserves the original.
Load validates and decodes the file before opening a document; malformed chunks,
unknown layer versions, truncation and allocation failures discard partial data.

Regression coverage: `test_penciltest_layers_test`, `test_penciltest_flc_test`,
`test_penciltest_layout_test`, and the shared history suites. Layer tests cover
sidebar routing, palette visibility, underpaint fill, frame operations, playback,
resize/undo, PTF round trips, corrupted extensions, and flattened exports.

Format reference: Jim Kent's [The FLIC File Format](https://jacobfilipp.com/DrDobbs/articles/DDJ/1993/9303/9303a/9303a.htm).
