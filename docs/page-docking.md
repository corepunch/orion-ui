# Page docking

A `Page` owns its resizable pane tree, active pane, and drag state. PageHost owns
activity navigation and displays the selected Page. There is no intermediate
layout-host window: SplitView branches and TabView leaves are direct Page content.
This follows the MFC frame-owned docking model, scoped to each activity Page.

Declare the initial tree directly in a `role="page"` form:

```xml
<form name="workspace" title="Workspace" role="page" width="900" height="600"
      flags="notitle,nofill" spacing="0" padding="0">
    <SplitView name="initial_split" flags="flexspace" split-ratio="0.5">
        <TabView name="left" flags="flexspace" />
        <TabView name="right" flags="flexspace" />
    </SplitView>
</form>
```

A Page can also be an ordinary form without docking. For docking, use one direct
TabView or a SplitView tree with TabView leaves. Fixed command rows may be siblings
of that tree; ordinary form auto-layout reserves their space. TabViews nested
inside a view's content remain local controls, outside the Page's docking tree.

Page initializes docking before the application's `evCreate`, enables
`TAB_STYLE_RESTORE_FOCUS | TAB_STYLE_DRAG_TABS` on its panes, and handles docking
messages before the application procedure. Its state lives in window-owned Page
metadata, leaving `userdata` for the application. No app initialization, forwarding,
or custom docking window class is needed. FormEditor preserves and previews the
same `role="page"` definition.

## Dragging

Drag a TabBar label past the six-pixel threshold. The containing Page
takes mouse capture and displays a translucent themed preview without labels:

- A pane's center accepts the tab into that pane.
- A TabBar shows an insertion marker, allowing moves and reordering.
- The outer quarter of the pane body offers left, right, top, or bottom splits.
  The preview covers the resulting new pane, including its TabBar.

Release to commit. Escape, pointer cancellation, resizing, Page deactivation, or removal of the
source/target cancels the preview; releasing outside all panes leaves the tree
unchanged. Hidden panes are skipped by drop targeting and active-pane fallback.
Dragging is local to one Page. A split must leave at least 96 logical
pixels of width or 64 of height per new pane. Splitting a pane's only tab into
itself is rejected. Existing SplitView dividers remain resizable.

Moving a tab reparents its existing child window, preserving running sessions,
scrollback, app state, and remembered descendant focus. The destination selects
the moved tab. An empty source pane collapses: its sibling replaces their split
branch, retaining the sibling's nested tree and divider ratios.

## Runtime API

Declared in `orion/user/page.h` (included by `ui.h`):

| API | Purpose |
| --- | --- |
| `page_pane_count(page)` | Number of leaf panes |
| `page_pane_at(page, index)` | Leaf in left-to-right/top-to-bottom tree order |
| `page_active_pane(page)` | Last-focused TabView, with a surviving-pane fallback |
| `page_split_pane(page, pane, edge)` | Create and return an empty TabView beside a leaf |
| `page_move_tab(page, view, target, edge, before)` | Move existing content into a pane or a new edge split |
| `page_close_tab(page, view)` | Destroy a tab and collapse its empty pane |
| `page_get_drop_preview(page, &drop)` | Read the current drop target and Page-local preview rectangle |

`PANE_CENTER` moves into an existing pane. The other edges are `PANE_LEFT`,
`PANE_RIGHT`, `PANE_TOP`, and `PANE_BOTTOM`. `before` is an insertion slot from zero
through the destination count, or `-1` to append; it is ignored for edge splits.
Pane indices are traversal positions, not persistent IDs. Do not retain pointers
to empty panes or replaced split branches after moving or closing tabs.

The final empty TabView remains available. Apps choose their final-tab policy;
Terminal creates a replacement session when closing the workspace's last tab.
Use the Page close API instead of `tcDeletePage` when empty-pane collapse is
wanted. Page does not know about terminal processes, documents, or saved layouts.

`window_reparent` owns the underlying list mutation and rejects cycles, root/Page
moves, docked panes, and transfers across root windows. It preserves window IDs
and resources, sends synchronous `evDescendantDetach` to the old ancestors, and
reannounces retained focus to the new ancestors. Like the other descendant
lifecycle notifications, detach handlers may update references but must not
mutate the hierarchy. Containers explicitly arrange the result. `window_map_point`
converts content coordinates between windows, including nested scroll offsets.

Git Client declares each activity's initial split tree directly in its Page form.
Existing table and diff windows are tab contents, retaining their database bindings
and command IDs when moved. Terminal declares its command rows and initial split
as siblings in the Workspace Page; each tab holds an independent session form.

## Migration from PaneHost

Remove the `<PaneHost>` wrapper from `role="page"` forms and call the `page_*` APIs
with the Page window. The old PaneHost class and `pane_host_*` docking APIs have
been removed. PageHost navigation APIs (`page_host_init`, `page_host_select`, etc.)
are unchanged. Applications still own session lifetime and persistence policy.
