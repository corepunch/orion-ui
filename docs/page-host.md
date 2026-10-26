---
layout: default
title: PageHost and Page
nav_order: 7
---

# PageHost and Page

A `PageHost` owns an `ActivityBar` and a persistent `Page` for each destination.
The bar appears on the left when the host client area is wider than tall, and
at the bottom when taller than wide. A square area keeps the previous side
(initially left). Resizing changes the bar orientation without recreating pages. Orion creates and docks the pages, selects exactly one, synchronizes
the activity buttons, projects the active page's toolbar, and restores keyboard
focus. Page contents remain ordinary forms and controls. Switching pages does
not recreate them or reset their data, scroll positions, or split ratios.

## Declarative bindings

A `role="host"` form becomes a PageHost when initialized with `page_host_init`.
Declare its destinations as ordered `Page` references inside a direct `ActivityBar`
child. Each reference points to a `role="page"` form. The page form owns its title,
`icon`, optional `checked-icon`, tooltip, and initial `focus` control. Orion builds
the navigation buttons from this metadata; there is no separate button list.

```xml
<form name="main" title="Example" width="960" height="640"
      role="host" active-page="changes">
    <ActivityBar name="activity">
        <Page form="overview" command="view.overview" />
        <Page form="changes" command="view.changes" />
    </ActivityBar>
</form>
<form name="overview" title="Overview" icon="overview"
      checked-icon="overview-fill" width="900" height="600" role="page">
    <Label text="Overview" />
</form>
<form name="changes" title="Changes" icon="changes"
      checked-icon="changes-fill" tooltip="Working tree" focus="files"
      width="900" height="600" role="page">
    <TableView name="files" flags="flexspace,vscroll" />
</form>
```

`active-page` selects the initial page; omitting it selects the first reference.
An optional `command` connects a destination to an existing menu/action command.
Without it the generator assigns a host-scoped ID using the referenced form name.
The list includes this host's pages, not every page form in the document.

The generator validates page roles, references, focus targets, duplicate bindings,
and the initial page. Forward references work; `orionc --form main` also emits
its referenced pages. FormEditor previews navigation using the referenced page
metadata and retains the declarations when saving; individual forms remain
independently editable. Legacy `Toolbar` / `Button page="..."` declarations still
work, but cannot be mixed with `Page` references in the same ActivityBar.

## Runtime behavior

Initialize the host in its `evCreate`, after app data and services are ready:

```c
static const page_handler_t pages[] = {
  {"overview", overview_proc},
  {"changes",  changes_proc},
};
if (!page_host_init(win, &example_main_form, pages, ARRAY_LEN(pages)))
  return false;
```

The handler table supplies only C behavior for named forms. Omit handlers with
`NULL, 0` for ordinary forms without custom page procedures. Keep layouts, icons,
commands, page bindings, and initial focus in `.orion`. Page procedures receive
`evCreate` with their controls already present and may capture app-specific
outlets. All pages are created before the initial selection notification.

Bound button clicks, menu commands, and accelerator commands are consumed by the
window system before the host's `evCommand` handler. Programmatic navigation
uses `page_host_select(host, command)`. Re-selecting the current page
restores focus without activating it again or sending another notification.

The host receives `evPageChanged` with the selected command in `wparam` and
the page window in `lparam`. Use this for app-specific refresh/status changes;
do not show/hide pages or synchronize buttons there. Existing `evActivate` and
`evDeactivate` messages still reach the selected/departing pages through
`set_host_page`. Use `page_host_page` to obtain a page and
`page_host_selection` to query the current command.

Focus is remembered before a click moves it onto the rail. If the remembered
control is destroyed, hidden, or disabled, Orion uses the declared focus target
or a visible enabled descendant, falling back to the page itself. Host destruction
releases navigation metadata; ordinary child ownership destroys the pages.
Invalid initialization or selection logs a framework diagnostic to stderr.

## History and the sidebar

PageHost records each selection, as a browser records its session history.
`page_host_can_go(host, -1 or +1)` asks whether there is a page behind or
ahead, and `page_host_go()` selects it. Navigating sends `evPageChanged` like
any selection and keeps the recorded entries; a new selection drops the entries
ahead of the current one. Entries whose page was destroyed are skipped. The app
binds its own Back and Forward commands (menu items with accelerators, title-row
buttons) to these calls and enables the buttons from `page_host_can_go()` in its
`evPageChanged` handler.

`page_host_set_sidebar_collapsed(host, true)` collapses the ActivityBar to an
icon rail by hiding its labels; `false` restores them. The bar measures again,
and a merged title row that tracks the sidebar follows its edge (see
[the sidebar column](toolbars.md#the-sidebar-column)).
Navigation tooltips appear in the icon rail. Visible labels suppress them, and
expanding the sidebar dismisses any open hint.

## Page toolbars

Declare `<Toolbar>` inside the `role="page"` form and give the host the `toolbar`
flag. PageHost projects the active Page's items into that standard toolbar band;
the Page itself does not reserve a second toolbar. Items in the host's own
`<Toolbar>` (a `<title />`, say) lead, and the active Page's follow them. Toolbar items use the same
`command` IDs as menus and accelerators. Commands are offered to the active Page
before the host's app handler, including commands absent from the toolbar.
Unhandled commands fall back to the host. A routing guard prevents recursion
when a Page forwards a command back to its host. Control notifications such as edit changes are not routed.

Terminal's Workspace Page uses one row of icon buttons, grouped with separators,
with tooltips for labels and shortcuts. The split tree is its only content child.

## Page content and tiling

PageHost fills the application workspace, owning its ActivityBar and Pages.
Each Page owns its content and layout directly. A Page may contain an ordinary
form or a [dockable pane tree](page-docking.md) made of SplitViews and TabViews.
No intermediate PaneHost window is required.

Docking state is separate from the app's Page userdata. The window system handles
layout, tab drags, previews, and empty-pane collapse without forwarding from app
procedures. Switching activities cancels an in-progress drag and retains the pane
tree and live view state. The Page's ordinary auto-layout also accommodates fixed
controls beside its pane tree.

Terminal's Workspace Page declares its toolbar and a SplitView. Each
pane's TabView owns independent TerminalView sessions. Git Client declares each
activity's default split tree directly in that Page's form. Cross-Page transfers,
floating panes, auto-hide, and layout persistence are not implemented by this API.
