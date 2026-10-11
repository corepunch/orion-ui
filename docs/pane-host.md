# PaneHost

`PaneHost` manages a resizable pane tree inside a Page. Its branches are ordinary
`SplitView` controls and its leaves are `TabView` controls. Each tab owns a child
window with application content. PageHost still owns navigation between Pages;
PaneHost owns the layout inside one Page.

Declare the initial tree in `.orion`. PaneHost is registered with the framework
and FormEditor; no app-specific preview class is required.

```xml
<PaneHost name="panes" flags="flexspace">
    <SplitView name="initial_split" split-ratio="0.5">
        <TabView name="left" />
        <TabView name="right" />
    </SplitView>
</PaneHost>
```

Use exactly one initial TabView or SplitView subtree. Applications create their
dynamic tab contents from reusable forms, then select them with `tcSetSelection`.
PaneHost enables `TAB_STYLE_RESTORE_FOCUS | TAB_STYLE_DRAG_TABS` on its TabViews.

## Dragging

Drag a TabBar label past the six-pixel threshold. The nearest containing PaneHost
takes mouse capture and displays a themed preview:

- A pane's center accepts the tab into that pane.
- A TabBar shows an insertion marker, allowing moves and reordering.
- The outer quarter of the pane body offers left, right, top, or bottom splits.
  The preview covers the resulting new pane, including its TabBar.

Release to commit. Escape, pointer cancellation, resizing, or removal of the
source/target cancels the preview; releasing outside all panes leaves the tree
unchanged. Hidden panes are skipped by drop targeting and active-pane fallback.
Dragging is local to one PaneHost. A split must leave at least 96 logical
pixels of width or 64 of height per new pane. Splitting a pane's only tab into
itself is rejected. Existing SplitView dividers remain resizable.

Moving a tab reparents its existing child window, preserving running sessions,
scrollback, app state, and remembered descendant focus. The destination selects
the moved tab. An empty source pane collapses: its sibling replaces their split
branch, retaining the sibling's nested tree and divider ratios.

## Runtime API

Declared in `orion/commctl/commctl.h`:

| API | Purpose |
| --- | --- |
| `pane_host_count(host)` | Number of leaf panes |
| `pane_host_pane(host, index)` | Leaf in left-to-right/top-to-bottom tree order |
| `pane_host_active(host)` | Last-focused TabView, with a surviving-pane fallback |
| `pane_host_split(host, pane, edge)` | Create and return an empty TabView beside a leaf |
| `pane_host_move_tab(host, page, target, edge, before)` | Move existing content into a pane or a new edge split |
| `pane_host_close_tab(host, page)` | Destroy a tab and collapse its empty pane |
| `pane_host_get_preview(host, &drop)` | Read the current drop target and host-local preview rectangle |

`PANE_CENTER` moves into an existing pane. The other edges are `PANE_LEFT`,
`PANE_RIGHT`, `PANE_TOP`, and `PANE_BOTTOM`. `before` is an insertion slot from zero
through the destination count, or `-1` to append; it is ignored for edge splits.
Pane indices are traversal positions, not persistent IDs. Do not retain pointers
to empty panes or replaced split branches after moving or closing tabs.

The final empty TabView remains available. Apps choose their final-tab policy;
Terminal creates a replacement session when closing the workspace's last tab.
Use the PaneHost close API instead of `tcDeletePage` when empty-pane collapse is
wanted. PaneHost does not know about terminal processes, documents, or saved layouts.

`window_reparent` owns the underlying list mutation and rejects cycles, root/Page
moves, docked panes, and transfers across root windows. It preserves window IDs
and resources, sends synchronous `evDescendantDetach` to the old ancestors, and
reannounces retained focus to the new ancestors. Like the other descendant
lifecycle notifications, detach handlers may update references but must not
mutate the hierarchy. Containers explicitly arrange the result. `window_map_point`
converts content coordinates between windows, including nested scroll offsets.

Git Client uses a separate PaneHost inside each activity Page. Its initial split
trees live in `gitclient.orion`; existing table and diff windows become tab
contents, retaining their database bindings and command IDs when moved.
