# TabView

A TabView contains a TabBar and its selected child page. The TabBar labels come
from child window titles. Selection hides other pages without recreating them,
so controls, processes, and layouts remain owned by their tab. A TabBar selects
content; the enclosing window's TitleBar still controls the window itself.

Static tab content can be nested under `TabView` in `.orion`. For dynamic tabs,
create a child using `create_window_from_form`, then select its index with
`tcSetSelection`. Keep the reusable tab form in `.orion`; C supplies its runtime
name and session behavior. `tcGetCount`, `tcGetPage`, and `tcGetSelection` query
the current children and selection.

`tcDeletePage(index)` destroys the page and its descendants. If it was selected,
the next page is selected, or the previous page when the last page was removed.
Removing an earlier inactive page retains the selected page. The parent receives
`tcnSelChange` as an `evCommand`. A host application decides whether to allow zero
tabs or create a replacement. Use this message for tab removal rather than
manually destroying a child and repairing the selection index.

Set `TAB_STYLE_RESTORE_FOCUS` through `tcSetStyle` when tabs contain interactive
sessions. TabView remembers each page's focused descendant, restores it on
selection, and falls back to a visible enabled child when it is unavailable.
The default behavior leaves keyboard focus on the TabBar for arrow navigation;
the optional style lets session apps supply their own accelerator commands.
Synchronous `evDescendantFocus` and `evDescendantDestroy` notifications from the
window system maintain this bookkeeping, including destruction of inactive
controls. These notifications carry the descendant in `lparam`; handlers should
only update references, without changing the window hierarchy.

The header reveals the selected tab when labels exceed the available width.
`tcGetTabRect(index, &rect)` reports a tab's rectangle in TabView coordinates,
including the current scroll offset. Sidebar style continues to use its standard
scrollbar. Pointer hit-testing and painting use the same offset.

## Moving tabs

`tabview_move_page(source, index, target, before)` transfers the same page window
and its remembered focus to another TabView under the same root. It also supports
reordering within one TabView. The message equivalent is
`tcMovePage(index, &move)` with `tab_move_t {target, before}`. `before` is an insertion slot (0 through the target
count), or -1 to append. Icon indices follow their pages; an empty destination
adopts the source image strip. Nonempty destinations must use the same strip when
moving an icon-bearing tab. Rejected transfers leave the page in its source.

`TAB_STYLE_DRAG_TABS` enables drag initiation from the horizontal TabBar. After a
six-pixel threshold, TabView sends synchronous `tcnBeginDrag` through `evCommand`
with a `tab_drag_t` containing the page and source-local pointer position. Its
parent may take capture to manage the interaction. `tcGetStyle` queries the flags.
The owning [Page](page-docking.md) supplies the reusable drop targets, snap overlay, nested
splits, and empty-pane collapse for applications.

## Header size and pane actions

`tcHideSingleTab(true)` (or `TAB_STYLE_HIDE_SINGLE_TAB` through `tcSetStyle`)
hides the tab chrome when exactly one visible tab remains. Declare it with
`<TabView hide-single-tab="true">` in `.orion`; FormEditor previews the same
policy. Multiple tabs restore the normal TabBar automatically, including after
moving, hiding, or restoring views. New Page splits inherit the source style.
Sidebar mode keeps its navigation list.

Without pane actions, the single-tab header collapses and content fills the
control. With actions, a plain title caption retains the toolbar and drag target.
`tcGetTabRect` returns that caption's bounds, or an empty rectangle when the
header is collapsed. Pass false to `tcHideSingleTab` to restore normal tabs.

Horizontal TabBars use the theme's `tab_header_height` (32 logical pixels in
both bundled themes), `tab_min_width`, and `tab_padding`. Icon strips can grow
the header further. Sidebar rows keep their own metrics.

`tcSetToolbar(count, items)` adds a standard toolbar at the right of the TabBar;
`tcGetToolbar` returns its owner window. Descriptors and strings are copied.
The toolbar is excluded from tab counting, selection, movement, and page layout.
Tabs scroll and paint only in the remaining header space. Sidebar mode hides the
header toolbar. Send a zero count to remove it.

Actions notify the parent with `tcnAction` and a synchronous `tab_action_t`:
`pane`, `source` toolbar window, and packed `command`. SplitView forwards this
notification. A Page activates the originating pane and dispatches the ordinary
command to its app procedure, so clicking another pane's action targets that pane.

Page supplies Split and Close automatically; an optional `<PaneToolbar>` adds
app actions before them. `<PaneToolbar replace="true">` supplies the complete
action list instead. See [Page docking](page-docking.md).

`tcHidePage` removes the indexed tab while retaining its child window and focus
history. `tcGetHiddenCount` reports retained tabs; `tcShowPages` restores them in
child order, including their icons. Hidden tabs are excluded from selection,
dragging, tab counts, and painting. Destroying or detaching a retained view
removes its hidden-tab record.
