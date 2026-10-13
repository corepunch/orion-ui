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
itself is rejected. `get_theme()->splitter.spacing` controls the layout space
between pane allocations (Modern, Light, Navy: 0; Classic: 2 logical pixels).
The resize hotspot is both adjoining pane margins plus that spacing. Hover
paints a centered two-pixel line using the tab-drop outline color (`brTextInfo`,
alpha 128); it does not reserve additional layout space.
`get_theme()->tab.pane_margin` configures the outside margin of every pane
(default: 2 logical pixels, 0 disables it). It applies to standalone panes,
nested splits, and surviving panes after collapse. Send `evThemeChanged` or
resize the Page after changing the metric to arrange its panes again.
Page supplies margins through ordinary layout metadata and configures each
SplitView's resize band with `svSetPaneMargin`. SplitView arranges its children
through the shared layout API and owns the splitter's actual window rectangle.

Each pane's complete subtree is painted to a reusable window surface and composited
against `brPanelDarker`, using `theme.tab.corner_radius`, the same radius as its
tab faces (Modern: 6, Classic: 0). The tab strip uses the lighter `brTabBarBg` role.
Content, toolbar children, and scrollbars share this silhouette. Page tabs meet the
pane's top edge so the selected first tab follows its top-left corner.

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
and command IDs when moved. Terminal declares its toolbar and initial split in the Workspace Page; each tab
holds an independent session form.

## Migration from PaneHost

Remove the `<PaneHost>` wrapper from `role="page"` forms and call the `page_*` APIs
with the Page window. The old PaneHost class and `pane_host_*` docking APIs have
been removed. PageHost navigation APIs (`page_host_init`, `page_host_select`, etc.)
are unchanged. Applications still own session lifetime and persistence policy.

## Pane toolbars

Every Page pane receives a standard right-aligned Split and Close toolbar, without
an app declaration. Alt changes Split's icon, tooltip, and command from right
to down. With several tabs, Split moves the selected view into a new group;
with one tab, it opens an empty group for docking other views. Close hides the
selected tool view while keeping its window, data bindings, and state alive.
A group containing only hidden views is hidden when another visible group exists;
it is retained so reopening can restore its views. An empty group with no retained
views collapses normally. `page_show_closed_views(page)` restores closed views.

`page_pane_action(page, pane, PANE_ACTION_*)` runs these same actions from menus
or accelerators. Before applying the default, Page sends itself `evPaneAction`
with the action in `wparam` and the originating TabView in `lparam`. An app may
return true after handling it: Terminal creates sessions on split and destroys
them on close, using its final-session policy. No forwarding is required for
apps using the standard behavior.

Optional `<PaneToolbar>` items are added before the standard controls. For example,
Terminal adds only its app-specific New Tab action:

```xml
<PaneToolbar>
    <Button command="tab.new" icon="tab-new" tooltip="New tab in this pane" />
</PaneToolbar>
```

Use `replace="true"` to supply the complete toolbar instead. Git Client keeps only
Close, routed through its existing command handler:

```xml
<PaneToolbar replace="true">
    <Button command="view.close_tab" icon="pane-close" tooltip="Close active view" />
</PaneToolbar>
```

An empty replacement toolbar hides all pane actions. Omitting `replace` keeps
the standard Split and Close actions.

New splits inherit the source pane's current toolbar. Custom items support
`alt-command`, `alt-icon`, and `alt-tooltip`; the shared toolbar owns modifier
handling. FormEditor previews both standard and custom actions.

## Relationship to MFC

Page plays the workspace/frame ownership role; its internal docking state plays
the manager role. TabView groups live child windows, and SplitView provides the
resizable tree. This follows MFC's separation between a frame-owned
[CDockingManager](https://learn.microsoft.com/en-us/cpp/mfc/reference/cdockingmanager-class)
and [CTabbedPane](https://learn.microsoft.com/en-us/cpp/mfc/reference/ctabbedpane-class),
using Orion window procedures and messages rather than C++ inheritance.
PageHost routes commands to the active Page before falling back to the host,
consistent with MFC's [active-view command routing](https://learn.microsoft.com/en-us/cpp/mfc/command-routing).

This is an adaptation, not API compatibility: Page docking currently supports
in-page tiled tab groups. Floating panes, auto-hide strips, persisted layouts,
and separate document/tool-window caption policies are not implemented here.
Orion's existing `dock.c` manages toolbar bands and floating controls separately;
it is not yet unified with the Page split tree.
