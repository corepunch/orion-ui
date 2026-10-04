# Drag placement

The sheet defaults to `GR_DROP_ANCHOR_SAMPLE`. Library drags and moves of
existing clips retain the exact cursor offset inside the card, in logical
pixels. The sheet subtracts that offset and compares the dragged card's center
with the centers of the possible snapped footprints. This keeps the sample in
the same bar and track until it crosses halfway into the next placement. A
sample displaced 80% of a bar to the right and 80% of a row downward snaps to
the next bar and row. Multi-bar samples retain their full width when locating
their center. Pointer anchoring uses the cell under the
cursor instead. The drop preview and committed placement use the same
calculation, including horizontal scrolling. Placement clamps at song edges;
occupied cells reject the drop. The cursor must be inside the sheet grid.

The target can opt into cursor-based placement:

```c
send_message(sheet, shSetDropAnchor, GR_DROP_ANCHOR_POINTER, NULL);
send_message(sheet, shSetDropAnchor, GR_DROP_ANCHOR_SAMPLE, NULL);
```

This setting belongs to the target sheet and lasts for its window lifetime.
`window_set_drag_visual()` continues to preserve the cursor's grab point in
either mode; the setting controls the snapped destination.
