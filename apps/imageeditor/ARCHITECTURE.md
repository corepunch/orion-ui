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

## Pencil Test animation files

Pencil Test exposes 16 drawing colors in a two-column docked toolbar. Index 0
remains transparent, with the original ink and paper at indices 1 and 2. The
remaining default colors occupy indices 3–16; the indexed file format is still
capable of retaining larger imported palettes. Selecting a missing swatch in an
older document adds it as an undoable palette command, using only an empty slot
unreferenced by any working layer or stored frame. Existing colors are preserved.
The toolbar keeps pencil/brush, eraser, fill, eyedropper, select, move, hand, and
foreground/background swap. Other tools remain accessible through menus.
The floating options palette stays one column wide beside the docked toolbar.

File > Save / Save As writes `.flc`; Open accepts 8-bit FLC and legacy FLI as
well as PCX/BMP still images. Saving an imported still or FLI first asks for an
FLC filename. GIF/APNG/sprite-sheet exports remain separate commands. The RGBA
Image Editor retains its PNG workflow.

`io/flc.c` writes standard COLOR256 and COPY chunks and a ring frame. Readers
support COLOR256, COLOR64, COPY, BLACK, BRUN, LC and SS2, including palette changes,
empty/repeated frames, prefix chunks and odd dimensions. File dimensions are
physical pixels, independent of the display's Retina scale. Frame delays drive
playback; FLI's 70 Hz time units are converted to milliseconds.

An optional trailer after the ring frame (type `0x7074`, `PTA1` signature at
offset 8 of its 16-byte header) preserves frame names,
exact indexed palette including alpha, background color/visibility, FPS and loop.
It contains one 1084-byte little-endian record per frame, with a `PTF1` signature
at offset 0, a 32-byte name
at 4, FPS at 36, delay at 40, loop/show flags at 44/45, background RGBA at 48,
and 256 packed palette entries at 60; other bytes are reserved. Standard players
finish playback before this trailer and display transparent pixels against the saved paper color.
Files are currently uncompressed, so size grows with pixel count and frame count.
External FLICs are mapped to a common document palette, with index 0 reserved for
transparency; files needing more than 255 distinct opaque colors across the
animation are rejected. True-color FLIC variants are unsupported. Animation pixel
storage is limited to 512 MiB and frame delays to 65535 ms. FLC is a single-layer
format; saving a layered indexed document is rejected rather than losing layers.

Save reads live pixels for the active frame without mutating timeline/history.
It writes and flushes a sibling temporary file, then atomically replaces the
original. Failure leaves the old file, document filename and dirty state intact.
Open decodes the entire timeline before creating a document. Malformed chunks,
truncation and allocation failures release partial results without adding windows.
Regression coverage: `make build/bin/test_penciltest_flc_test` then run that binary.

Format reference: Jim Kent's [The FLIC File Format](https://jacobfilipp.com/DrDobbs/articles/DDJ/1993/9303/9303a/9303a.htm).
