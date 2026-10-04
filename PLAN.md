# Gitclient redesign and action-test plan

## Goal

Turn `apps/gitclient` into a dependable, GitKraken-inspired repository
workspace while keeping Orion's declarative `.orion` forms and database-driven
views. Every visible action should have one stable command ID and one command
execution path, regardless of whether it is invoked from a menu, toolbar,
context menu, keyboard shortcut, or test.

The target is inspired by GitKraken's information hierarchy and workflow, not a
pixel copy:

- repository and current-branch context are always visible;
- the commit graph/history is the primary navigation surface;
- changes and the selected diff are available without losing history context;
- common Git operations are grouped by intent and show their current state;
- destructive and network operations remain explicit and confirmation-backed.

## Proposed main-window layout

```text
┌ Git Client ────────────────────────────────────────────────────────────────┐
│ [Open repo ▾] [current branch ▾] [Fetch] [Pull] [Push] [Refresh] [Search]  │
│ [Changes] [History] [Commit] [Branch ▾] [Stash ▾] [More ▾]                  │
├───────────────────────────────────────────────────────────────────────────┤
│ repo/path › branch                         ahead 0  behind 0  dirty ●       │
├───────────────┬───────────────────────────────────┬────────────────────────┤
│ REPOSITORY    │ GRAPH / HISTORY                   │ DETAILS                │
│               │                                   │                        │
│ Local         │ ●──●──●──●  commit subject        │ Selected commit         │
│  branches     │ │     └──●  author · date         │ metadata                │
│  remotes      │ ●──●        commit subject        │                        │
│               │                                   │ Files in commit        │
│ Tags          │ [graph + commit list]             │ [file list]             │
│ Stashes       │                                   │                        │
├───────────────┴───────────────────────────────────┴────────────────────────┤
│ CHANGES / COMMIT                                                            │
│ [Unstaged] [Staged] [All]     [file list with stage checkboxes]             │
│ commit summary [.........................................................]  │
│ description    [.........................................................]  │
│ [Stage all] [Unstage all] [Stash]                          [Commit]         │
├───────────────────────────────────────────────────────────────────────────┤
│ status: clean / modified · branch · upstream · operation progress           │
└───────────────────────────────────────────────────────────────────────────┘
```

### Layout decisions

1. Use a persistent left repository navigator for local branches, remote
   branches, tags, and stashes. Keep the current branch visually distinct.
2. Make the center history view the primary surface. Add a graph column or a
   graph-aware history control rather than treating commits as a flat table.
3. Make the right side a reusable details pane: commit metadata, changed files,
   and diff. A selected file should update the diff without changing the
   current repository selection.
4. Treat Changes as a bottom workbench that can be collapsed, resized, and
   reused from both Changes and History. Preserve the current staged checkbox
   workflow and commit editor.
5. Put repository/branch selection and network actions in the top command band;
   put destructive or less frequent actions under explicit dropdowns and menus.
6. Keep the status bar for branch/upstream/dirty state and asynchronous Git
   operation progress. Do not use it as the only place to report failures.

The first implementation should extend the existing `SplitView`, `TabView`,
`TableView`, and `DiffView` composition. A graph renderer and a collapsible
bottom workbench are separate framework-sized tasks; do not block command
correctness on them.

## Action model

### One action, many surfaces

Create an explicit action inventory before changing the layout. Each action
needs:

- a stable symbolic name and generated numeric ID;
- label, tooltip, icon, category, and destructive/network/read-only metadata;
- an enabled/disabled predicate derived from repository and selection state;
- a single handler or command function;
- optional default accelerator;
- an expected refresh/invalidation effect.

The canonical path should be:

```text
button/menu/context menu/accelerator
                 ↓
             command ID
                 ↓
       gc_execute_action(action, context)
                 ↓
       backend operation + state refresh
```

`gc_handle_command()` can remain as a compatibility wrapper during migration,
but new code should not put business logic in toolbar-specific branches.

### `.orion` menus as the action manifest

The menu tree is both the action registry and a map of the application. Every
user-invokable capability should be discoverable as a menu command, even when it
is not currently exposed through a toolbar. Other surfaces reference that
declaration using the existing `command="remote.sync"` style. This keeps the app
skeleton visible in one place while allowing the same action to appear in a
menu, toolbar, context menu, command palette, hotkey, or test.

This gives us a useful invariant: if a user can cause a state mutation or
application operation, there must be a menu-declared command for it. Toolbars
and context menus are ergonomic projections of that map, not separate command
systems. Internal framework notifications, lifecycle messages, and purely
visual state changes are not user actions and do not need menu entries.

Consequences:

- menu categories describe the application’s capability areas;
- command IDs and metadata originate only from menu items;
- every toolbar/context-menu/hotkey reference must resolve to a menu item;
- command tests can enumerate the menu tree to find the complete user-facing
  surface;
- tool-specific shortcuts, such as imageeditor’s tool hotkeys, should either
  reference an existing menu command or become entries in a suitable `Tools`
  menu rather than remaining invisible C-only commands.

The menu item owns the action identity, label, and default shortcut. Use the
established `shortcut` attribute spelling (the generator keeps `hotkey` as a
backward-compatible alias):

```xml
<menu name="repo" label="Repo">
  <item name="refresh" label="Refresh" shortcut="F5" />
  <item name="search" label="Search..." shortcut="Ctrl+Shift+F" />
</menu>

<forms>
  <form name="main" width="800" flags="toolbar">
    <Toolbar>
    <Button name="refresh" command="repo.refresh"
            icon="sysicon_arrow_refresh" text="Refresh" />
    </Toolbar>
  </form>
</forms>
```

The generator should reject duplicate menu action names, unknown `command`
references, duplicate hotkeys, and malformed hotkey strings. It should emit one
stable command ID per menu item, use that ID for every reference, and generate
the accelerator table and action metadata from the menu declarations.
Platform-specific modifier translation belongs in the accelerator layer, not in
individual command handlers.

An action may expose equivalent accelerators with a semicolon-separated
`shortcut` value, such as `shortcut="Delete;Backspace"`. The generator validates
and emits each spelling as a separate accelerator while retaining one command
ID and one menu action.

The current generated `ID_REMOTE_SYNC_SYNC` versus handler `ID_REMOTE_SYNC`
problem is therefore a generator/reference bug, not a reason to introduce a
second action registry. Fix the generator so `command="remote.sync"` resolves to
the menu item's existing `ID_REMOTE_SYNC`.

Suggested initial defaults:

| Action | Default | Notes |
|---|---:|---|
| `repo.refresh` | F5 | Safe, repeatable |
| `repo.search` | Ctrl+Shift+F | Search repository/history |
| `commit.commit` | Ctrl+K | Existing convention; document it |
| `remote.fetch` | Ctrl+F | Consider conflict with future file search |
| `branch.new` | Ctrl+N | New branch |
| `branch.checkout` | Ctrl+P | Quick branch picker, GitKraken-style |
| `view.changes` | Ctrl+1 | Workspace tab |
| `view.history` | Ctrl+2 | Workspace tab |
| `files.stage` / `files.unstage` | Space | Selection-sensitive |
| `files.stage_all` | Ctrl+Shift+A | Explicitly document scope |

Finalize this list only after checking Orion's key constants and resolving
platform conflicts. A default hotkey is part of the action contract and must be
tested like a button.

## Task breakdown

### Phase 0 — Baseline and inventory

- [x] Run the current build and test suite; record failures separately from
  gitclient behavior.
- [ ] Generate a command matrix from `gitclient.orion`, generated IDs, and all
  `case ID_*` handlers in `view_menubar.c`.
- [ ] Mark every action as implemented, partial, missing, selection-dependent,
  destructive, networked, or UI-only.
- [ ] Capture the current layout and toolbar behavior as a manual baseline.

Deliverable: `gitclient_action_matrix.md` or an equivalent checked-in test
fixture, plus a short baseline result in the eventual gitclient README.

### Phase 1 — Make the action manifest correct

- [x] Document that menu items are the sole action declarations and that
  toolbars/context menus reference them with `command="group.action"`.
- [x] Add menu-item shortcut parsing and generated accelerator definitions.
- [x] Fix the reference-generation bug (`*_SYNC`, `*_FETCH`, etc.) and
  regenerate the header; verify every referenced surface uses the menu
  declaration's existing ID.
- [x] Add generator validation for unknown command references, duplicate action
  names within a menu, duplicate hotkeys, and malformed hotkeys.
- [x] Add generated action metadata derived from menu declarations.
- [x] Normalize application toolbar manifests to fully qualified
  `command="group.action"` references and add missing scener menu declarations
  for toolbar-only editing modes.
- [x] Move imageeditor, scener, and socialfeed application accelerators into
  their `.orion` menu items, including tool commands and equivalent shortcuts.
- [x] Add a generator contract test for multi-shortcut menu actions.
- [ ] Audit form-local submit buttons and custom controls; decide whether each
  is a continuation of a menu command or needs a menu-declared command of its
  own.

Deliverable: menus, toolbar, context menus, and accelerators all emit the same
canonical action ID.

### Repository-wide audit status

The rule is now explicit: application commands and their default shortcuts are
menu declarations in `.orion`; toolbars and context menus only reference those
commands. The current migration status is:

- `gitclient`, `imageeditor`, `scener`, and `socialfeed`: migrated for normal
  application commands and generated accelerators.
- `scener` retains a reduced C accelerator table only for the temporary
  viewport-navigation context, where the active accelerator scope changes.
- `browser`, `formeditor`, `shell`, and `taskmanager`: still use legacy C menu
  and/or accelerator definitions and are follow-up migration work.
- `terminal` raw key forwarding is terminal input, not an application command.
- `vibeoffice`, `filemanager`, and demo-only apps have no menu-backed shortcut
  registry to migrate yet.

### Phase 2 — Extract and harden command execution

- [x] Introduce a small `gc_action`/`gc_commands` module with a command context
  containing repository, selected branch/commit/file/stash, and active view.
- [ ] Move the switch cases from `gc_handle_command()` into action functions;
  retain the switch only as ID-to-action dispatch.
- [ ] Give every action an explicit unavailable-state result instead of silently
  falling through. Show consistent messages for no repository/no selection.
- [ ] Centralize refresh policy: data reload, table refresh, diff refresh,
  selection preservation, and status-bar update.
- [ ] Centralize confirmation policy for discard, undo, branch/tag deletion,
  force push, and other destructive operations.
- [ ] Make async operations expose pending/success/failure state and disable or
  guard duplicate invocations while work is pending.

Deliverable: one callable function per action with deterministic result and
state effects.

### Phase 3 — Redesign the main view

- [ ] Replace the current two-tab-first composition with the proposed persistent
  navigator, history center, details/diff pane, and collapsible changes
  workbench; preserve the existing controls where possible.
- [ ] Add explicit repository and branch selectors in the top command band.
- [ ] Add a graph column/control backed by commit parent data. Extend the schema
  with parent references or a graph layout cache as needed.
- [ ] Make the details pane reusable for commit and working-tree selections.
- [ ] Preserve selection and scroll position across refresh when records still
  exist; add tests for selection invalidation when they do not.
- [ ] Add enabled/disabled or unavailable visual state for actions based on
  current context.
- [ ] Keep event routing in the framework. If hit-testing or scrolling changes,
  follow `ARCHITECTURE.md` and test nested scrolled children through
  `handle_mouse()` rather than compensating in the view.

Deliverable: the new layout is navigable with mouse and keyboard and retains
the existing database-driven data flow.

### Phase 4 — Tests

#### Headless command/action tests (highest value)

- [x] Enumerate menu-declared actions and assert each has a handler and metadata.
- [x] Assert every toolbar item references a known action ID and that its click
  dispatches that exact ID.
- [ ] Invoke each safe action against a temporary Git fixture and assert the
  resulting repository state, database state, selected records, and refresh
  count.
- [ ] Test selection-dependent actions with no selection, invalid selection,
  local branch, remote branch, staged file, unstaged file, stash, and tag.
- [ ] Test destructive actions' confirmation behavior without requiring a real
  dialog; inject a confirmation callback.
- [ ] Test async action completion and failure messages using a fake backend or
  controlled temporary repository.

#### `.orion`/generator contract tests

- [ ] Compile a fixture containing menu-declared actions, toolbar references,
  context-menu references, and hotkeys.
- [ ] Assert every alias/reference produces the menu item's one numeric ID, not
  a suffixed clone such as `ID_REMOTE_SYNC_SYNC`.
- [ ] Assert unknown menu references, duplicate menu action names, duplicate
  hotkeys, malformed hotkeys, and missing handlers fail generation with
  actionable diagnostics.
- [ ] Assert generated metadata is complete enough to enumerate all surfaces.

#### Focused UI/event tests (not screenshot-heavy)

Test the actual framework contract and user-visible commands, not pixels:

- [ ] Toolbar button down/up reaches the parent and invokes the expected action.
- [ ] Menu item, context-menu item, toolbar button, and accelerator converge on
  the same command execution path.
- [ ] Tab switches update view mode and invalidate the correct panes.
- [ ] Branch, commit, and file selections update dependent views and diff state.
- [ ] Staging checkbox and double-click behavior mutate the intended file.
- [ ] Refresh preserves or clears selection according to the documented rule.
- [ ] Nested scrolled controls deliver the correct content-space row, using the
  framework routing tests described in `ARCHITECTURE.md`.
- [ ] Async completion updates status and refreshes exactly once.

Avoid screenshot tests for ordinary command correctness. Add a small number of
render/layout smoke tests only for stable geometry invariants: required panes
exist, splitters have usable minimum sizes, toolbar controls are visible, and
the commit editor/buttons are not clipped. The existing `test_env` and
event-posting style in `tests/` and `apps/imageeditor/tests/` are the model.

#### Manual QA / exploratory scenarios

- [ ] Open a clean repository, an empty repository, a dirty repository, and a
  repository with no upstream.
- [ ] Exercise local/remote branches, merge/rebase conflicts, tags, stashes,
  untracked files, renamed files, binary files, and large diffs.
- [ ] Run every toolbar button once from Changes and once from History where it
  is applicable; verify it invokes the same action as menus.
- [ ] Exercise default hotkeys with focus in the branch list, history list,
  diff, and commit editor.
- [ ] Run with the Platform headless surface where supported and retain error
  output on failures.

### Phase 5 — Polish and documentation

- [ ] Update `apps/gitclient/README.md` with the new information architecture,
  action manifest rules, and test commands.
- [ ] Add a troubleshooting section explaining message routing and optional
  debug diagnostics.
- [ ] Document destructive/network action semantics and hotkeys in the Help
  menu or command palette.
- [ ] Add a release checklist requiring generated headers to be regenerated and
  `make test` to pass.

## Suggested implementation order

1. Fix and test command identity/reference generation.
2. Add action inventory, metadata, accelerator generation, and command tests.
3. Extract command execution and state/refresh policy.
4. Implement the layout incrementally: top context band, navigator/details
   panes, then graph and collapsible changes workbench.
5. Add the focused UI/event tests and manual QA scenarios.
6. Polish icons, labels, disabled states, and documentation.

This order makes a toolbar failure diagnosable before layout work adds more
surfaces and gives the new UI a tested command contract to consume.

## Acceptance criteria

- Every action declared in `.orion` has one stable generated ID, one handler,
  and at least one automated test.
- Every toolbar button works or is visibly disabled with a documented reason;
  no toolbar action silently falls through the dispatcher.
- Menu, context menu, toolbar, and default-hotkey invocations produce the same
  command result and state transition.
- `make test` passes, including gitclient backend, database, generator, and
  action/event tests.
- The main view exposes repository context, branch navigation, commit graph or
  graph-ready history, selected-file diff, and staged-change commit workflow
  without losing context during refresh.
- Destructive and network operations are confirmation/error-safe and show
  progress or failure state.
- Routine interaction logging is disabled by default; errors remain visible.

## Open decisions to resolve during Phase 0

- Whether graph rendering belongs in a new `ReportView` mode or a dedicated
  `CommitGraphView` control.
- Whether the bottom Changes workbench is a new collapsible framework control or
  an initial `SplitView` approximation.
- Whether hotkey conflicts should be rejected globally or allowed with explicit
  focus scopes.
- Whether action handlers should return a small enum (`done`, `unavailable`,
  `cancelled`, `failed`, `pending`) or a richer result object.
- Which GitKraken-like features are in scope for the first milestone: graph
  visualization, quick branch picker, command palette, or repository tabs.
