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

## Page content and tiling

PageHost manages navigation; a Page owns its content and layout. A Page may contain
an ordinary form, a fixed SplitView, or eventually a workspace of tiled panes and
tab groups. Orion supplies reusable layout and docking controls; apps compose them
inside pages. Tiling is optional and scoped to that page, rather than imposed on
every Orion window. This host preserves existing page layouts. Dynamic pane trees,
cross-host transfers, and layout persistence across launches remain separate work.


Terminal demonstrates this composition: its Workspace Page contains a SplitView
with a TabView in each pane. Each tab owns one TerminalView; each pane keeps its
own selected tab while the split layout remains part of the Workspace Page.
See [TabView](tabview.md) for the reusable tab lifecycle and focus APIs.
