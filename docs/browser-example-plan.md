# Browser Example MVP Plan

## Goal

Get to a usable MVP as fast as possible:

1. Type a URL in an address field.
2. Fetch the page HTML.
3. Parse HTML with libxml2.
4. Render readable plain text blocks (no tags, no CSS, no JS).

Everything else is explicitly deferred until after this works.

## MVP Scope (Must-Have)

1. One browser window.
2. One address input.
3. One load action (Enter key in address field).
4. Async fetch via Orion HTTP.
5. HTML to plain text-block conversion via libxml2.
6. Text output area with basic wrapping/scroll.

## MVP Phase 1: Minimal Skeleton

1. Add `examples/browser/` with one main window proc.
2. Keep state minimal:
   - `current_url`
   - `loading` flag
   - `request_id`
   - `html_raw` buffer
   - `render_text` buffer
3. Create window and child controls:
   - top address `win_textedit`
   - content view area for rendered text

Note: for MVP speed, create controls directly as child windows. Do not block on extending `WINDOW_TOOLBAR` item types first.

## MVP Phase 2: URL Entry and Fetch

1. On Enter in address field:
   - normalize URL (prepend `https://` if missing scheme)
   - set `loading = true`
   - call `http_request_async`
2. On `evHttpDone`:
   - store response body in `html_raw`
   - clear `loading`
   - trigger parse step

## MVP Phase 3: libxml2 Parse to Text Blocks

1. Parse with `htmlReadMemory`.
2. Traverse DOM and emit plain text only:
   - text nodes -> append normalized text
   - block elements (`p`, `div`, `li`, `h1..h6`, etc.) -> add `\n\n`
   - `br` -> add `\n`
3. Drop scripts/styles entirely.
4. Output goes to `render_text`.

Definition of done for parser MVP:

1. Visible text is readable.
2. No raw tags shown.
3. Paragraphs are separated as text blocks.

## MVP Phase 4: Paint and Scroll

1. Render `render_text` with existing small-font draw APIs.
2. Basic line-wrap to viewport width.
3. Use built-in vertical scroll support for long pages.
4. Show a simple loading message while request is in flight.

## MVP Build and Dependency

1. Wire libxml2 only for the browser example target (prefer `pkg-config --cflags --libs libxml-2.0`).
2. If libxml2 is missing:
   - browser example fails with clear message
   - rest of project still builds.

## Post-MVP (Deferred)

1. Back/forward history.
2. Link extraction and clickable links.
3. Opening links in new windows.
4. Toolbar integration via `WINDOW_TOOLBAR` item extensions.
5. Relative URL resolution.
6. Any styling beyond plain text.

## Fast Validation Checklist

1. Launch browser example.
2. Type URL and press Enter.
3. Request completes without crash.
4. Content area shows parsed text blocks (not HTML tags).
5. Long pages can be scrolled.
