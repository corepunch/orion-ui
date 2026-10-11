# Terminal

Terminal uses Orion's [PageHost and Page](../../docs/page-host.md) navigation.
`terminal.orion` declares the Workspace destination and its title, icons, tooltip,
and initial focus. PageHost creates the page, selects it, restores the last focused
terminal, and places the activity bar left in landscape or bottom in portrait.

The Workspace page owns a `SplitView` with a `TabView` in each pane. Each tab
instantiates the `session_tab` form containing one independent `TerminalView`.
Switching tabs retains the terminals, running PTYs, input drafts, scrollback,
and working directories. The split layout belongs to the Workspace page and
its divider ratio is preserved while either pane changes tabs. The selected pane
regains keyboard focus.
Closing a tab destroys its session; closing a pane's last tab opens a fresh one
in that pane. Startup Lua scripts run in the initial terminal in each pane.

The action row offers New Tab, Close Tab, Previous, and Next for the last-focused
pane. The other pane remains visible and keeps its own selected tab. Shortcuts are
Ctrl+Shift+T, Ctrl+Shift+W, Ctrl+Shift+Tab, and Ctrl+Tab respectively (the framework
uses Command for the primary shortcut modifier on macOS). Previous/Next wrap
around that pane's tab list. TabView reveals the selected tab when its TabBar overflows.
The TabBar selects sessions; it is distinct from the outer window TitleBar.

`main.c` initializes PageHost and routes commands. `pages/workspace/page_workspace.c`
tracks the focused pane and creates/closes its tab forms through TabView.
`view_main.c`, `vgat_screen.c`, and `pty.c` implement terminal behavior; they do not manage tabs or page navigation.
FormEditor uses the registered TerminalView preview from `components/plug.c`.

Build with `make terminal`. The app tests are `test_terminal_app_test` and
`test_terminal_page_host_test` in `build/bin`; build them with the corresponding
`make build/bin/test_…` targets. The PageHost integration test uses offscreen graphics
and covers navigation, focus restoration, split resizing, tab creation/removal,
live PTY retention and cleanup, and landscape/portrait placement.
