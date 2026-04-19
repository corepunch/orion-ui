# Browser Example Implementation Plan

## Plan Overview

1. Add a new browser example module under `examples/` that uses:
   - `WINDOW_TOOLBAR` for navigation UI
   - Orion async HTTP API for fetch
   - `libxml2` HTML parser for text and links extraction
   - Custom text renderer (no CSS, plain text flow only)
2. Support:
   - Address textbox in toolbar
   - Back and forward buttons
   - Clickable links (blue and underlined)
   - New top-level windows instead of tabs

## Phase 1: Example Skeleton and Window Model

1. Use the structure and pattern from `examples/taskmanager/view_main.c` as the base for a document-style top-level window.
2. Define a per-window browser state:
   - `current_url`
   - history array and `history_index`
   - parsed text runs (plain and link runs)
   - clickable link hit regions
   - fetch request id and loading flag
3. On create:
   - Create browser top-level window with toolbar enabled.
   - Build toolbar controls (back, forward, address).
   - Navigate to an initial URL.

## Phase 2: Toolbar Address Textbox

Current toolbar item support in `user/messages.h` and creation path in `user/message.c` do not include a text edit item.

1. Extend toolbar item model with a text edit type (small framework change, clean WinAPI-style).
2. Implement toolbar child creation for that type in `user/message.c`, using existing `win_textedit` control from `commctl/edit.c`.
3. Wire command handling so Enter in address bar triggers navigation.

## Phase 3: HTTP Fetch Integration

1. Use the async API documented in `docs/http.md`:
   - `http_request_async` on navigate
   - `kWindowMessageHttpDone` to receive full HTML
2. Ignore CSS, JS, and resources; only process the returned HTML body.
3. On back and forward:
   - load URL from history
   - optionally fetch again (simple behavior)
   - keep no cache initially

## Phase 4: HTML to Plain Text and Links (`libxml2`)

1. Parse with `htmlReadMemory` (libxml2 HTML parser mode).
2. Traverse DOM and emit a simple stream of runs:
   - plain text runs
   - link runs (`href`, blue, underlined)
   - block separators (`\n\n` for `p/div/li/h1...`, `\n` for `br`)
3. No CSS/layout engine; use semantic block breaks and inline text only.
4. Resolve relative links against current URL (simple URL-join helper).

## Phase 5: Layout, Paint, and Hit-Testing

1. Build a tiny layout pass in the browser view:
   - wrap text to viewport width
   - track run positions
   - store hit rectangles for link runs
2. Paint rules:
   - normal text: default color
   - links: blue and underlined
   - selection and focus optional later
3. Mouse handling:
   - click in link rect navigates to link URL
   - add to history correctly
   - repaint

## Phase 6: New Window Instead of Tabs

1. Add a New Window action (menu or toolbar button).
2. Action creates another top-level browser window with independent state and history.
3. No shared tabs state.

## Phase 7: Build System and Docs

1. Add `libxml2` compile and link flags for this example path in `Makefile` (prefer `pkg-config`).
2. If `libxml2` is missing:
   - fail browser example build with clear message
   - keep other examples unaffected
3. Update docs:
   - add browser example run and build notes in `examples/README.md`
   - mention `libxml2` dependency and no-CSS limitation

## Phase 8: Verification

1. Manual checks:
   - Enter URL and load
   - back and forward navigation
   - link clicks navigate
   - links render blue and underlined
   - open multiple windows with independent history
2. Basic test target (headless logic):
   - HTML parse-to-runs for links and text
   - history push/back/forward behavior
   - relative URL resolution
