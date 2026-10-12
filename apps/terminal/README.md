# Terminal

Terminal uses Orion's [PageHost and Page](../../docs/page-host.md) navigation.
`terminal.orion` declares the Workspace destination and its title, icons, tooltip,
and initial focus. PageHost creates the page, selects it, restores the last focused
terminal, and places the activity bar left in landscape or bottom in portrait.

The Workspace Page directly owns its docking layout, initially containing a
`SplitView` with a `TabView` in each pane. Each tab
instantiates the `session_tab` form containing one independent `TerminalView`.
Switching tabs retains the terminals, running PTYs, input drafts, scrollback,
and working directories. The split layout belongs to the Workspace page and
its divider ratio is preserved while either pane changes tabs. The selected pane
regains keyboard focus.
Closing a tab destroys its session. Empty panes collapse; closing the entire
workspace's last tab opens a fresh session. Startup Lua scripts run in the initial terminal in each pane.

The Workspace Page declares a standard icon toolbar in `terminal.orion`. PageHost
projects it as one toolbar row with tooltips and separators. New Tab, Close Tab,
Previous, and Next act on the last-focused pane. Other panes keep their own selected tabs. Standalone shortcuts are
Ctrl+Shift+T, Ctrl+Shift+W, Ctrl+Shift+Tab, and Ctrl+Tab respectively (the framework
uses Command for the primary shortcut modifier on macOS). Previous/Next wrap
around that pane's tab list. TabView reveals the selected tab when its TabBar overflows.
The TabBar selects sessions; it is distinct from the outer window TitleBar.

`main.c` initializes PageHost. The framework routes projected toolbar commands,
menu commands, and accelerators to the active Page using the same command IDs. `pages/workspace/page_workspace.c`
creates/closes session forms and routes commands through Page and TabView.
`view_main.c`, `vgat_screen.c`, and `pty.c` implement terminal behavior; they do not manage tabs or page navigation.
FormEditor uses the registered TerminalView preview from `components/plug.c`.

Drag a tab onto a pane's center to move it, onto its TabBar to choose an insertion
position, or near an edge to split it. A highlighted rectangle previews the snap
position before release. Escape or dropping outside the panes cancels the move.
Sessions keep running during moves and splits; empty source panes collapse.

Split Right and Split Down open a new session in a new pane. Move Tab moves the
selected session to the next pane in tree order, and Next Pane changes focus.
Their standalone shortcuts are Ctrl+Shift+E, Ctrl+Shift+O, Ctrl+Shift+M, and
Ctrl+Alt+Tab (Command replaces Ctrl on macOS). The static controls and shortcuts
are declared in `terminal.orion`; [Page docking](../../docs/page-docking.md) owns drag,
snap preview, tab transfers, splitting, and collapse.

Build with `make build/bin/terminal`. The app tests are `test_terminal_app_test` and
`test_terminal_page_host_test` in `build/bin`; build them with the corresponding
`make build/bin/test_…` targets. The PageHost integration test uses offscreen graphics
and covers navigation, focus restoration, split resizing, tab creation/removal,
live PTY retention and cleanup, and landscape/portrait placement.

Each pane has New Tab (+), Split, and Close Tab (×) actions at the right of its
32-pixel TabBar. Close removes the pane's active tab; individual tabs have no
close button. Click Split to split right;
hold Alt to change the icon, tooltip, and action to split down. The button targets
its own pane, even when another pane had focus, as do New Tab and Close Tab.
`PaneToolbar` declares these actions once
in `terminal.orion`, and dynamically created panes inherit it. Both directions
remain available in the Tab menu and through their existing shortcuts.
