# Drag placement

Positions use integer ticks: one bar is 256 ticks. The default grid step is
64 ticks (one beat), so there are four possible starts per bar. Blocks retain
their original duration of 1, 2 or 4 bars. Clip lookup, cropping, audio
starts and the song's loop endpoint all retain fractional positions. The
ruler shows the subdivisions. A tap on the ruler, or on the arrangement
where no clip is visible, seeks in the same 64-tick steps.

The sheet defaults to `GR_DROP_ANCHOR_SAMPLE`. Library drags and moves of
existing clips retain the exact cursor offset inside the card, in logical
pixels. The sheet subtracts that offset and compares the dragged card's center
with the centers of the possible snapped footprints. This keeps the sample in
the same grid position and track until it crosses halfway into the next
placement. A sample displaced 80% of a grid step to the right and 80% of a row
downward snaps to the next 64-tick position and row. Multi-bar samples retain
their full width when locating their center. Pointer anchoring uses the cell under the
cursor instead. The drop preview and committed placement use the same
calculation, including horizontal scrolling. Placement clamps at song edges;
overlapping drops are allowed. The cursor must be inside the sheet grid.
Library cards stay in the library while a copy follows the cursor. Releasing a
clip that is being moved outside the sheet grid removes it from the song.

On each track, a clip is visible and audible only up to the next clip's start.
This cutoff is derived from the current arrangement; the block, start position,
source audio and original duration are preserved. A shorter later clip does
not cause the earlier tail to resume. Moving or deleting the later clip restores
the earlier clip up to the next remaining start or its natural end. At identical
starts, the most recently added or moved clip covers the others, which remain
in the song and return when the covering clip is removed. Selection, right-click
deletion and the playback loop endpoint follow the visible arrangement. Cropped
waveforms retain their original time scale, and dragging lifts the full clip.

The target can opt into cursor-based placement:

```c
send_message(sheet, shSetDropAnchor, GR_DROP_ANCHOR_POINTER, NULL);
send_message(sheet, shSetDropAnchor, GR_DROP_ANCHOR_SAMPLE, NULL);
```

This setting belongs to the target sheet and lasts for its window lifetime.
`window_set_drag_visual()` continues to preserve the cursor's grab point in
either mode; the setting controls the snapped destination.
